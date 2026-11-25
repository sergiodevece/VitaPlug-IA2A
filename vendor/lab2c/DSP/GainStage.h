#pragma once
#include <algorithm>
#include <cmath>

namespace lab2c::dsp {
// A gain in dB for the processed path (INPUT trim before the optical engines, MAKE-UP after them). The value
// follows a 20 ms linear ramp in dB that lands exactly on the target; the linear gain is recomputed only
// while the ramp moves and is exactly 1 at 0 dB, so a settled 0 dB stage is detectable as neutral.
// No heap allocation or locking.
class SmoothedGainDb final {
public:
    explicit SmoothedGainDb(double minDb = -24.0, double maxDb = 24.0) noexcept : minDb_(minDb), maxDb_(maxDb) {}
    void prepare(double sampleRate) noexcept {
        const double fs = std::isfinite(sampleRate) && sampleRate > 1.0 ? sampleRate : 48000.0;
        rampSamples_ = std::max(1, static_cast<int>(std::lround(fs * 0.020)));
        remaining_ = 0;
        current_ = target_;
        gain_ = toGain(current_);
    }
    // Once per block.
    void setTarget(float db) noexcept {
        double t = std::isfinite(db) ? std::clamp(static_cast<double>(db), minDb_, maxDb_) : 0.0;
        // The 0.1 dB parameter grid stores its 0 dB point as -24 + 0.1f * 240 (about 3.6e-7 dB): anything far
        // below one step is the neutral 0 dB.
        if (std::abs(t) < 1.0e-3) t = 0.0;
        if (t == target_) return; // re-sent every block; must not restart the ramp
        target_ = t;
        step_ = (target_ - current_) / static_cast<double>(rampSamples_);
        remaining_ = rampSamples_;
    }
    // First block after prepare: start at the stored value (no ramp).
    void snap() noexcept { current_ = target_; remaining_ = 0; gain_ = toGain(current_); }
    // Once per sample frame: the linear gain for this sample.
    [[nodiscard]] float next() noexcept {
        if (remaining_ > 0) {
            --remaining_;
            current_ = target_ - step_ * static_cast<double>(remaining_); // lands exactly on the target
            gain_ = toGain(current_);
        }
        return gain_;
    }
    [[nodiscard]] double currentDb() const noexcept { return current_; }
private:
    [[nodiscard]] static float toGain(double db) noexcept { return db == 0.0 ? 1.0f : static_cast<float>(std::pow(10.0, db / 20.0)); }
    double minDb_, maxDb_, target_ = 0.0, current_ = 0.0, step_ = 0.0;
    int rampSamples_ = 960, remaining_ = 0;
    float gain_ = 1.0f;
};
} // namespace lab2c::dsp
