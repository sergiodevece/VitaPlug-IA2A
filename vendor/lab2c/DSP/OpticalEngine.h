#pragma once
#include <algorithm>
#include <cmath>

namespace lab2c::dsp {

struct Settings {
    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float kneeDb = 6.0f;
    float attackMs = 10.0f;
    float fastReleaseMs = 90.0f;
    float slowReleaseMs = 1800.0f;
    float memory = 0.65f; // contribution of the slow recovery path, [0, 1]
};

// Dependency-free proof-of-concept engine. Not calibrated to a physical T4 cell.
class OpticalEngine {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 1.0) ? sampleRate : 48000.0;
        reset();
        updateCoefficients();
    }
    void reset() noexcept {
        fastDb_ = slowDb_ = 0.0f;
        // Envelopes are at rest, so the next Memory value can apply immediately.
        memoryNow_ = s_.memory;
        memoryRampRemaining_ = 0;
        snapNextMemory_ = true;
    }
    void setSettings(Settings incoming) noexcept {
        s_.thresholdDb = safeClamp(incoming.thresholdDb, -80.f, 0.f, -18.f);
        s_.ratio = safeClamp(incoming.ratio, 1.f, 40.f, 4.f);
        s_.kneeDb = safeClamp(incoming.kneeDb, 0.f, 24.f, 6.f);
        s_.attackMs = safeClamp(incoming.attackMs, 0.1f, 200.f, 10.f);
        s_.fastReleaseMs = safeClamp(incoming.fastReleaseMs, 10.f, 3000.f, 90.f);
        s_.slowReleaseMs = safeClamp(incoming.slowReleaseMs, 100.f, 15000.f, 1800.f);
        setMemoryTarget(safeClamp(incoming.memory, 0.f, 1.f, 0.65f));
        updateCoefficients();
    }
    // Memory actually applied to the envelope mix (equals settings memory once a ramp ends).
    [[nodiscard]] float effectiveMemory() const noexcept { return memoryNow_; }
    [[nodiscard]] float targetReductionDb(float detectorDb) const noexcept {
        if (!std::isfinite(detectorDb)) return 0.f;
        const float over = detectorDb - s_.thresholdDb;
        const float factor = 1.f - 1.f / s_.ratio;
        if (s_.kneeDb <= 0.f) return std::max(0.f, over) * factor;
        const float half = s_.kneeDb * 0.5f;
        if (over <= -half) return 0.f;
        if (over >= half) return over * factor;
        const float x = over + half;
        return factor * x * x / (2.f * s_.kneeDb);
    }
    // One sample of detector level in dBFS. Returns positive gain reduction in dB.
    [[nodiscard]] float processDetectorDb(float detectorDb) noexcept {
        const float target = std::clamp(targetReductionDb(detectorDb), 0.f, 96.f);
        // Rising gain reduction tracks the attack envelope; recovery has two states.
        if (target > fastDb_) fastDb_ = target + attackCoeff_ * (fastDb_ - target);
        else fastDb_ = target + fastReleaseCoeff_ * (fastDb_ - target);
        if (target > slowDb_) slowDb_ = target + attackCoeff_ * (slowDb_ - target);
        else slowDb_ = target + slowReleaseCoeff_ * (slowDb_ - target);
        if (memoryRampRemaining_ > 0) {
            --memoryRampRemaining_;
            // Derived from the target so the ramp lands exactly on it without accumulated error.
            memoryNow_ = s_.memory - memoryStep_ * static_cast<float>(memoryRampRemaining_);
        }
        return std::clamp((1.f - memoryNow_) * fastDb_ + memoryNow_ * slowDb_, 0.f, 96.f);
    }
    // Stereo Link: the envelope stage of processDetectorDb() for a target already computed (and
    // clamped to 0..96 dB) by the caller. processDetectorDb() is left as it was; tests prove that
    // processTargetDb(clamp(targetReductionDb(d), 0, 96)) == processDetectorDb(d) bit for bit.
    [[nodiscard]] float processTargetDb(float target) noexcept {
        if (target > fastDb_) fastDb_ = target + attackCoeff_ * (fastDb_ - target);
        else fastDb_ = target + fastReleaseCoeff_ * (fastDb_ - target);
        if (target > slowDb_) slowDb_ = target + attackCoeff_ * (slowDb_ - target);
        else slowDb_ = target + slowReleaseCoeff_ * (slowDb_ - target);
        if (memoryRampRemaining_ > 0) {
            --memoryRampRemaining_;
            memoryNow_ = s_.memory - memoryStep_ * static_cast<float>(memoryRampRemaining_);
        }
        return std::clamp((1.f - memoryNow_) * fastDb_ + memoryNow_ * slowDb_, 0.f, 96.f);
    }
    [[nodiscard]] static float gainLinear(float reductionDb) noexcept {
        return std::pow(10.f, -std::clamp(reductionDb, 0.f, 96.f) / 20.f);
    }
private:
    static float safeClamp(float x, float lo, float hi, float fallback) noexcept {
        return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
    }
    [[nodiscard]] float coefficient(float ms) const noexcept {
        return static_cast<float>(std::exp(-1.0 / (sampleRate_ * ms * 0.001)));
    }
    // R0.2G-H1: Memory only reweights the two envelopes, so a jump in it is a jump in GR.
    // A new target starts a linear ramp from the value currently applied; retargeting
    // mid-ramp restarts from that value, so the applied Memory never jumps.
    void setMemoryTarget(float target) noexcept {
        if (snapNextMemory_) {
            snapNextMemory_ = false;
            s_.memory = memoryNow_ = target;
            memoryRampRemaining_ = 0;
            return;
        }
        if (target == s_.memory) return; // re-sent every block; must not restart the ramp
        s_.memory = target;
        const int samples = std::max(1, static_cast<int>(std::lround(sampleRate_ * kMemoryRampSeconds)));
        memoryStep_ = (target - memoryNow_) / static_cast<float>(samples);
        memoryRampRemaining_ = samples;
    }
    void updateCoefficients() noexcept {
        attackCoeff_ = coefficient(s_.attackMs);
        fastReleaseCoeff_ = coefficient(s_.fastReleaseMs);
        slowReleaseCoeff_ = coefficient(s_.slowReleaseMs);
    }
    Settings s_{};
    double sampleRate_ = 48000.;
    float fastDb_ = 0.f, slowDb_ = 0.f;
    float attackCoeff_ = 0.f, fastReleaseCoeff_ = 0.f, slowReleaseCoeff_ = 0.f;
    static constexpr double kMemoryRampSeconds = 0.020;
    float memoryNow_ = s_.memory, memoryStep_ = 0.f;
    int memoryRampRemaining_ = 0;
    bool snapNextMemory_ = true;
};

} // namespace lab2c::dsp
