#pragma once
#include "OpticalDynamicsChain.h"
#include "SidechainHpf.h"
#include <algorithm>
#include <cmath>

namespace lab2c::dsp {
// R0.2D: two independent detection/gain paths, shared parameter values.
// Stereo Link (processStereo): optional linking of the two paths; at 0 % they stay independent.
// Sidechain HPF: one filter per channel feeding a second detector per channel; the audio is not
// filtered. Both detectors always run; on/off interpolates their levels in dB over 20 ms. While off
// (mix 0) the level is exactly the original detector's, as before.
// No heap allocation or locking.
class StereoOpticalBridge final {
public:
    void prepare(double sampleRate) noexcept {
        const double sr = std::isfinite(sampleRate) && sampleRate > 1.0 ? sampleRate : 48000.0;
        for (auto& chain : channels_) chain.prepare(sr);
        rampStep_ = 1.0f / static_cast<float>(std::max(1.0, sr * 0.015)); // 15 ms
        wet_ = 0.0f;
        targetWet_ = 0.0f;
        linkStep_ = 1.0f / static_cast<float>(std::max(1.0, sr * 0.020)); // 20 ms
        link_ = 0.0f;
        targetLink_ = 0.0f;
        cutoff_.prepare(sr);
        scStep_ = 1.0f / static_cast<float>(std::max(1.0, sr * 0.020)); // 20 ms
        scMix_ = 0.0f;
        targetScMix_ = 0.0f;
        for (auto& f : hpf_) f.reset();
        clearTransition();
    }
    void reset() noexcept {
        for (auto& chain : channels_) chain.reset();
        wet_ = 0.0f;
        targetWet_ = 0.0f;
        link_ = 0.0f;
        targetLink_ = 0.0f;
        scMix_ = 0.0f;
        targetScMix_ = 0.0f;
        for (auto& f : hpf_) f.reset();
        clearTransition();
    }
    // R0.2G-H2B: clears detector and optical envelopes (audio thread, block boundary) while
    // keeping the DSP ON state. The GR applied just before becomes an inherited floor that
    // falls linearly to 0 over transitionSamples; the applied GR is max(fresh, inherited), so
    // nothing jumps, and once the floor reaches 0 the output is exactly that of a fresh chain.
    void resetDynamics(int transitionSamples) noexcept {
        const int n = std::max(1, transitionSamples);
        for (int ch = 0; ch < 2; ++ch) {
            inheritedDb_[ch] = lastGrDb_[ch];
            inheritedStepDb_[ch] = inheritedDb_[ch] / static_cast<float>(n);
            inheritedRemaining_[ch] = inheritedDb_[ch] > 0.0f ? n : 0;
            if (inheritedRemaining_[ch] == 0) inheritedDb_[ch] = 0.0f;
            channels_[ch].reset();
            hpf_[ch].reset(); // with the detector: after the transition the engine equals a fresh one
        }
    }
    [[nodiscard]] float inheritedGrDb(int channel) const noexcept { return channel == 0 || channel == 1 ? inheritedDb_[channel] : 0.0f; }
    [[nodiscard]] int transitionRemaining(int channel) const noexcept { return channel == 0 || channel == 1 ? inheritedRemaining_[channel] : 0; }
    void setEnabled(bool enabled) noexcept { targetWet_ = enabled ? 1.0f : 0.0f; }
    // R0.2G-H2D: start the DSP ON mix at the target set by setEnabled() instead of fading in from
    // dry. Called once, on the first block with samples after prepare; later toggles keep the
    // 15 ms ramp of nextWet().
    void snapWet() noexcept { wet_ = targetWet_; }
    // Stereo Link amount, 0 (DUAL) .. 1 (fully linked). The applied amount follows it with a 20 ms
    // linear ramp; snapLink() starts it at the target on the first block with samples after prepare.
    void setLink(float amount) noexcept { targetLink_ = std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.0f; }
    void snapLink() noexcept { link_ = targetLink_; }
    [[nodiscard]] float link() const noexcept { return link_; }
    // Sidechain HPF: on/off and cutoff, once per block. The detected level moves between the original
    // and the filtered detector over 20 ms (linear in mix, levels in dB); the cutoff follows a 20 ms
    // log ramp.
    // snapSidechain() starts both at their targets on the first block with samples after prepare.
    void setSidechain(bool on, float cutoffHz) noexcept { targetScMix_ = on ? 1.0f : 0.0f; cutoff_.setTarget(cutoffHz); }
    void snapSidechain() noexcept { scMix_ = targetScMix_; cutoff_.snap(); }
    // Once per sample frame, before processChannel / processStereo: advances the on/off mix and the
    // cutoff, and returns the mix to pass to them.
    [[nodiscard]] float nextSidechainMix() noexcept {
        if (scMix_ < targetScMix_) scMix_ = std::min(targetScMix_, scMix_ + scStep_);
        else if (scMix_ > targetScMix_) scMix_ = std::max(targetScMix_, scMix_ - scStep_);
        (void) cutoff_.next();
        return scMix_;
    }
    [[nodiscard]] double sidechainCutoffHz() const noexcept { return cutoff_.currentHz(); }
    void setDynamics(float thresholdDb, float ratio, float attackMs = 10.f,
                     float fastReleaseMs = 90.f, float slowReleaseMs = 1800.f,
                     float memory = 0.65f, float kneeDb = 6.f) noexcept {
        Settings s{};
        s.kneeDb = kneeDb;
        s.thresholdDb = thresholdDb;
        s.ratio = ratio;
        s.attackMs = attackMs;
        s.fastReleaseMs = fastReleaseMs;
        s.slowReleaseMs = slowReleaseMs;
        s.memory = memory;
        for (auto& chain : channels_) chain.setOpticalSettings(s);
    }
    [[nodiscard]] float nextWet() noexcept {
        if (wet_ < targetWet_) wet_ = std::min(targetWet_, wet_ + rampStep_);
        else if (wet_ > targetWet_) wet_ = std::max(targetWet_, wet_ - rampStep_);
        return wet_;
    }
    template <typename T>
    [[nodiscard]] T processChannel(T input, int channel, float wet, float* measuredGrDb = nullptr, float scMix = 0.0f) noexcept {
        if (channel < 0 || channel > 1) return input;
        const float hp = hpf_[channel].process(static_cast<float>(input), cutoff_.coefficients());
        const auto result = channels_[channel].processSampleSidechain(static_cast<float>(input), hp, scMix);
        float grDb = result.gainReductionDb;
        float output = result.output;
        if (inheritedRemaining_[channel] > 0) {
            --inheritedRemaining_[channel];
            // Derived from the step so the floor lands exactly on 0 after the transition.
            inheritedDb_[channel] = inheritedStepDb_[channel] * static_cast<float>(inheritedRemaining_[channel]);
            if (inheritedDb_[channel] > grDb) {
                grDb = inheritedDb_[channel];
                const float x = std::isfinite(static_cast<float>(input)) ? static_cast<float>(input) : 0.0f;
                output = x * OpticalEngine::gainLinear(grDb);
            }
        }
        lastGrDb_[channel] = grDb;
        if (measuredGrDb != nullptr) *measuredGrDb = grDb * wet;
        if (wet <= 0.0f) return input; // bit-transparent on finite normal input while off
        return input + (static_cast<T>(output) - input) * static_cast<T>(wet);
    }
    // Stereo Link (C+), one stereo frame. While the applied link is 0 this is processChannel() for L
    // then R: the DUAL path, unchanged. Otherwise each channel's target (static curve) moves towards
    // the larger of the two, each channel's own envelopes and Memory process it, the H2B inherited
    // floor applies per channel, and the final gain reduction moves towards the larger of the two.
    // At 100 % both targets and both final reductions are exactly that maximum.
    template <typename T>
    void processStereo(T& left, T& right, float wet, float* grLeft = nullptr, float* grRight = nullptr, float scMix = 0.0f) noexcept {
        const float link = nextLink();
        if (link <= 0.0f) {
            left = processChannel(left, 0, wet, grLeft, scMix);
            right = processChannel(right, 1, wet, grRight, scMix);
            return;
        }
        const T in[2] = {left, right};
        float target[2], gr[2];
        for (int c = 0; c < 2; ++c) {
            const float hp = hpf_[c].process(static_cast<float>(in[c]), cutoff_.coefficients());
            target[c] = channels_[c].targetDbSidechain(static_cast<float>(in[c]), hp, scMix);
        }
        const float targetMax = std::max(target[0], target[1]);
        for (int c = 0; c < 2; ++c) {
            const float linked = link >= 1.0f ? targetMax : target[c] + link * (targetMax - target[c]);
            gr[c] = channels_[c].reductionForTargetDb(linked);
            if (inheritedRemaining_[c] > 0) {
                --inheritedRemaining_[c];
                inheritedDb_[c] = inheritedStepDb_[c] * static_cast<float>(inheritedRemaining_[c]);
                gr[c] = std::max(gr[c], inheritedDb_[c]);
            }
        }
        const float grMax = std::max(gr[0], gr[1]);
        T* out[2] = {&left, &right};
        float* measured[2] = {grLeft, grRight};
        for (int c = 0; c < 2; ++c) {
            const float applied = link >= 1.0f ? grMax : gr[c] + link * (grMax - gr[c]);
            // A host reset inherits the channel's own reduction before the final coupling, so the
            // coupling applies once after a reset as before it (no step at a partial link).
            lastGrDb_[c] = gr[c];
            if (measured[c] != nullptr) *measured[c] = applied * wet;
            const float x = std::isfinite(static_cast<float>(in[c])) ? static_cast<float>(in[c]) : 0.0f;
            const float output = x * OpticalEngine::gainLinear(applied);
            *out[c] = wet <= 0.0f ? in[c] : in[c] + (static_cast<T>(output) - in[c]) * static_cast<T>(wet);
        }
    }
private:
    [[nodiscard]] float nextLink() noexcept {
        if (link_ < targetLink_) link_ = std::min(targetLink_, link_ + linkStep_);
        else if (link_ > targetLink_) link_ = std::max(targetLink_, link_ - linkStep_);
        return link_;
    }
    void clearTransition() noexcept {
        for (int ch = 0; ch < 2; ++ch) { lastGrDb_[ch] = inheritedDb_[ch] = inheritedStepDb_[ch] = 0.0f; inheritedRemaining_[ch] = 0; }
    }
    OpticalDynamicsChain channels_[2]{};
    float wet_ = 0.f, targetWet_ = 0.f, rampStep_ = 1.f;
    float link_ = 0.f, targetLink_ = 0.f, linkStep_ = 1.f;
    SidechainHpf hpf_[2]{};
    SidechainHpfCutoff cutoff_{};
    float scMix_ = 0.f, targetScMix_ = 0.f, scStep_ = 1.f;
    float lastGrDb_[2]{}, inheritedDb_[2]{}, inheritedStepDb_[2]{};
    int inheritedRemaining_[2]{};
};
} // namespace lab2c::dsp
