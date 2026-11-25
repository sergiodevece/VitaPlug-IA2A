#pragma once
#include "LevelDetector.h"
#include "OpticalEngine.h"
#include <algorithm>
#include <cmath>

namespace lab2c::dsp {
// R0.2C: mono proof-of-concept chain; NOT connected to JUCE/audio callback.
// A separate chain per independent channel is required in future stereo integration.
class OpticalDynamicsChain final {
public:
    struct Result {
        float output = 0.0f;
        float detectorDb = -200.0f;
        float gainReductionDb = 0.0f;
        float linearGain = 1.0f;
    };
    void prepare(double sampleRate) noexcept {
        detector_.prepare(sampleRate);
        filteredDetector_.prepare(sampleRate);
        optical_.prepare(sampleRate);
    }
    void reset() noexcept { detector_.reset(); filteredDetector_.reset(); optical_.reset(); }
    void setDetectorSettings(LevelDetector::Settings s) noexcept { detector_.setSettings(s); filteredDetector_.setSettings(s); }
    void setOpticalSettings(Settings s) noexcept { optical_.setSettings(s); }
    [[nodiscard]] Result processSample(float input) noexcept {
        // Invalid floating point input cannot be propagated to the output.
        const float x = std::isfinite(input) ? input : 0.0f;
        const float detectorDb = detector_.processSampleDb(x);
        const float reductionDb = optical_.processDetectorDb(detectorDb);
        const float gain = OpticalEngine::gainLinear(reductionDb);
        // Finite input is not amplitude-limited on this audio path; protection from
        // host-provided extreme floats is a separate future plugin policy.
        return { x * gain, detectorDb, reductionDb, gain };
    }
    // Stereo Link: processSample() split in two so a stereo bridge can link the targets between
    // the static curve and the optical envelopes. processSample() is unchanged.
    [[nodiscard]] float targetDb(float input) noexcept {
        const float x = std::isfinite(input) ? input : 0.0f;
        return std::clamp(optical_.targetReductionDb(detector_.processSampleDb(x)), 0.f, 96.f);
    }
    [[nodiscard]] float reductionForTargetDb(float target) noexcept { return optical_.processTargetDb(target); }
    // Sidechain HPF: a second detector reads the filtered input. Both detectors always run, so
    // either is warm when the HPF switches. The detected levels are interpolated in dB,
    // level = (1 - mu) * original + mu * filtered, then follow the unchanged curve and envelopes.
    // mu <= 0 uses the original detector's level exactly (the path of processSample()); mu >= 1
    // uses the filtered detector's level exactly. The gain applies to `input`, never filtered.
    [[nodiscard]] Result processSampleSidechain(float input, float filtered, float mu) noexcept {
        const float x = std::isfinite(input) ? input : 0.0f;
        const float detectorDb = sidechainDetectorDb(x, filtered, mu);
        const float reductionDb = optical_.processDetectorDb(detectorDb);
        const float gain = OpticalEngine::gainLinear(reductionDb);
        return { x * gain, detectorDb, reductionDb, gain };
    }
    [[nodiscard]] float targetDbSidechain(float input, float filtered, float mu) noexcept {
        const float x = std::isfinite(input) ? input : 0.0f;
        return std::clamp(optical_.targetReductionDb(sidechainDetectorDb(x, filtered, mu)), 0.f, 96.f);
    }
private:
    [[nodiscard]] float sidechainDetectorDb(float x, float filtered, float mu) noexcept {
        const float originalDb = detector_.processSampleDb(x);
        const float filteredDb = filteredDetector_.processSampleDb(std::isfinite(filtered) ? filtered : 0.0f);
        if (mu <= 0.0f) return originalDb; // HPF off: bypass, no arithmetic on the original level
        if (mu >= 1.0f) return filteredDb;
        return (1.0f - mu) * originalDb + mu * filteredDb;
    }
    LevelDetector detector_{};
    LevelDetector filteredDetector_{}; // Sidechain HPF; same settings as detector_
    OpticalEngine optical_{};
};
} // namespace lab2c::dsp
