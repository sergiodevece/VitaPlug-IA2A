#pragma once
#include <algorithm>
#include <cmath>

namespace lab2c::dsp {

// R0.2B: independent mono detector core. One instance per detection channel.
// This is NOT yet a calibrated LA-2A sidechain and does not process plugin audio.
class LevelDetector final {
public:
    struct Settings {
        float attackMs = 1.0f;
        float releaseMs = 80.0f;
        float peakBlend = 0.5f; // 0: RMS, 1: peak, intermediate: amplitude blend
    };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 1.0) ? sampleRate : 48000.0;
        updateCoefficients();
        reset();
    }
    void reset() noexcept { peak_ = rmsPower_ = 0.0f; }
    void setSettings(Settings settings) noexcept {
        settings_.attackMs = sanitize(settings.attackMs, 0.1f, 500.f, 1.f);
        settings_.releaseMs = sanitize(settings.releaseMs, 1.f, 5000.f, 80.f);
        settings_.peakBlend = sanitize(settings.peakBlend, 0.f, 1.f, 0.5f);
        updateCoefficients();
    }

    // Mono sample -> detector amplitude. No heap allocations or blocking.
    [[nodiscard]] float processSample(float input) noexcept {
        const float x = std::isfinite(input) ? std::clamp(std::abs(input), 0.f, 32.f) : 0.f;
        peak_ = smooth(peak_, x);
        rmsPower_ = smooth(rmsPower_, x * x);
        const float rms = std::sqrt(std::max(0.f, rmsPower_));
        return (1.f - settings_.peakBlend) * rms + settings_.peakBlend * peak_;
    }
    [[nodiscard]] float processSampleDb(float input) noexcept {
        return amplitudeToDb(processSample(input));
    }
    [[nodiscard]] static float amplitudeToDb(float amplitude) noexcept {
        if (!std::isfinite(amplitude) || amplitude <= 1.0e-10f) return -200.f;
        return 20.f * std::log10(amplitude);
    }
private:
    [[nodiscard]] static float sanitize(float value, float lo, float hi, float fallback) noexcept {
        return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
    }
    [[nodiscard]] float coefficient(float ms) const noexcept {
        return static_cast<float>(std::exp(-1.0 / (sampleRate_ * ms * 0.001)));
    }
    void updateCoefficients() noexcept {
        attack_ = coefficient(settings_.attackMs);
        release_ = coefficient(settings_.releaseMs);
    }
    [[nodiscard]] float smooth(float state, float target) const noexcept {
        const float a = target > state ? attack_ : release_;
        return target + a * (state - target);
    }
    Settings settings_{};
    double sampleRate_ = 48000.0;
    float peak_ = 0.f;
    float rmsPower_ = 0.f;
    float attack_ = 0.f;
    float release_ = 0.f;
};

} // namespace lab2c::dsp
