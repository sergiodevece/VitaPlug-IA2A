#pragma once
#include "OpticalDynamicsChain.h"
#include "SidechainHpf.h"
#include <algorithm>
#include <cmath>

namespace lab2c::dsp {
// M/S (phase 2A, isolated: not connected to the processor). Two autonomous optical domains, MID and
// SIDE, each with its own settings, sidechain HPF (on/off ramp and cutoff), two detectors, optical
// envelopes and Memory, and H2B inherited floor; nothing is shared between the domains.
//   M = (L + R) / 2,  S = (L - R) / 2   ->   M' = M gM,  S' = S gS   ->   L' = M' + S',  R' = M' - S'
// Encoding and decoding run in double. With gains <= 1, |L'|, |R'| <= max(|L|, |R|) on every sample.
// When neither domain reduces (both gains exactly 1) the input is returned unchanged (exact
// transparency, also for double input). The DSP ON mix is applied in L/R after decoding with the
// same formula as StereoOpticalBridge; at wet 0 the input is returned unchanged.
// Non-finite input: a non-finite sample is processed as 0, so it never reaches the other channel; that
// channel's output keeps the non-finite input sample. Mono input has no side: processMono() runs the
// MID domain alone (identical to a stereo frame with L = R).
// The ramps per sample are advanced inside processStereo()/processMono(). No heap allocation or locking.
class MidSideOpticalBridge final {
public:
    enum Domain : int { kMid = 0, kSide = 1 };
    struct DomainSettings {
        Settings optical{};
        bool hpfOn = false;
        float hpfHz = 100.f;
    };

    void prepare(double sampleRate) noexcept {
        const double sr = std::isfinite(sampleRate) && sampleRate > 1.0 ? sampleRate : 48000.0;
        scStep_ = 1.0f / static_cast<float>(std::max(1.0, sr * 0.020)); // 20 ms, as the L/R bridge
        for (int d = 0; d < 2; ++d) {
            chains_[d].prepare(sr);
            cutoff_[d].prepare(sr);
            hpf_[d].reset();
            scMix_[d] = targetScMix_[d] = 0.0f;
        }
        clearTransition();
    }
    void reset() noexcept {
        for (int d = 0; d < 2; ++d) { chains_[d].reset(); hpf_[d].reset(); scMix_[d] = targetScMix_[d] = 0.0f; }
        clearTransition();
    }
    // Once per block, per domain.
    void setDomain(int domain, const DomainSettings& s) noexcept {
        if (domain != kMid && domain != kSide) return;
        chains_[domain].setOpticalSettings(s.optical);
        targetScMix_[domain] = s.hpfOn ? 1.0f : 0.0f;
        cutoff_[domain].setTarget(s.hpfHz);
    }
    // First block after prepare: start the sidechain HPFs at their stored state (no 20 ms ramps).
    void snapSidechain() noexcept { for (int d = 0; d < 2; ++d) { scMix_[d] = targetScMix_[d]; cutoff_[d].snap(); } }
    // R0.2G-H2B, per domain: clears detectors, filters and envelopes; the gain reduction applied just
    // before becomes a floor that falls linearly to 0 over transitionSamples.
    void resetDynamics(int transitionSamples) noexcept {
        const int n = std::max(1, transitionSamples);
        for (int d = 0; d < 2; ++d) {
            inheritedDb_[d] = lastGrDb_[d];
            inheritedStepDb_[d] = inheritedDb_[d] / static_cast<float>(n);
            inheritedRemaining_[d] = inheritedDb_[d] > 0.0f ? n : 0;
            if (inheritedRemaining_[d] == 0) inheritedDb_[d] = 0.0f;
            chains_[d].reset();
            hpf_[d].reset();
        }
    }

    template <typename T>
    void processStereo(T& left, T& right, float wet, float* grMid = nullptr, float* grSide = nullptr) noexcept {
        const bool finiteL = std::isfinite(static_cast<double>(left)), finiteR = std::isfinite(static_cast<double>(right));
        const double l = finiteL ? static_cast<double>(left) : 0.0, r = finiteR ? static_cast<double>(right) : 0.0;
        const double m = (l + r) * 0.5, s = (l - r) * 0.5;
        const float grM = domainGrDb(kMid, static_cast<float>(m)), grS = domainGrDb(kSide, static_cast<float>(s));
        if (grMid != nullptr) *grMid = grM * wet;
        if (grSide != nullptr) *grSide = grS * wet;
        if (wet <= 0.0f) return; // DSP OFF: the input is untouched
        const float gM = OpticalEngine::gainLinear(grM), gS = OpticalEngine::gainLinear(grS);
        if (gM == 1.0f && gS == 1.0f) return; // no reduction in either domain: exact transparency
        const double mo = m * static_cast<double>(gM), so = s * static_cast<double>(gS);
        if (finiteL) left = mix(left, mo + so, wet);
        if (finiteR) right = mix(right, mo - so, wet);
    }
    // Mono input: M = x, S = 0 (the SIDE domain is not run).
    template <typename T>
    [[nodiscard]] T processMono(T input, float wet, float* grMid = nullptr) noexcept {
        const bool finite = std::isfinite(static_cast<double>(input));
        const double x = finite ? static_cast<double>(input) : 0.0;
        const float grM = domainGrDb(kMid, static_cast<float>(x));
        if (grMid != nullptr) *grMid = grM * wet;
        if (wet <= 0.0f || !finite) return input;
        const float gM = OpticalEngine::gainLinear(grM);
        if (gM == 1.0f) return input;
        return mix(input, x * static_cast<double>(gM), wet);
    }
    [[nodiscard]] float inheritedGrDb(int domain) const noexcept { return domain == kMid || domain == kSide ? inheritedDb_[domain] : 0.0f; }
    [[nodiscard]] double sidechainCutoffHz(int domain) const noexcept { return domain == kMid || domain == kSide ? cutoff_[domain].currentHz() : 0.0; }

private:
    // One domain, one sample: HPF ramps advance, the detectors and the optical stage run, the H2B floor applies.
    [[nodiscard]] float domainGrDb(int d, float x) noexcept {
        if (scMix_[d] < targetScMix_[d]) scMix_[d] = std::min(targetScMix_[d], scMix_[d] + scStep_);
        else if (scMix_[d] > targetScMix_[d]) scMix_[d] = std::max(targetScMix_[d], scMix_[d] - scStep_);
        const float hp = hpf_[d].process(x, cutoff_[d].next());
        float gr = chains_[d].processSampleSidechain(x, hp, scMix_[d]).gainReductionDb;
        if (inheritedRemaining_[d] > 0) {
            --inheritedRemaining_[d];
            inheritedDb_[d] = inheritedStepDb_[d] * static_cast<float>(inheritedRemaining_[d]);
            gr = std::max(gr, inheritedDb_[d]);
        }
        lastGrDb_[d] = gr;
        return gr;
    }
    // The DSP ON mix of StereoOpticalBridge, in the input's type.
    template <typename T>
    [[nodiscard]] static T mix(T input, double processed, float wet) noexcept {
        return input + (static_cast<T>(processed) - input) * static_cast<T>(wet);
    }
    void clearTransition() noexcept {
        for (int d = 0; d < 2; ++d) { lastGrDb_[d] = inheritedDb_[d] = inheritedStepDb_[d] = 0.0f; inheritedRemaining_[d] = 0; }
    }
    OpticalDynamicsChain chains_[2]{};
    SidechainHpf hpf_[2]{};
    SidechainHpfCutoff cutoff_[2]{};
    float scMix_[2]{}, targetScMix_[2]{}, scStep_ = 1.f;
    float lastGrDb_[2]{}, inheritedDb_[2]{}, inheritedStepDb_[2]{};
    int inheritedRemaining_[2]{};
};
} // namespace lab2c::dsp
