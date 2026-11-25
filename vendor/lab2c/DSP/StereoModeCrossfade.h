#pragma once
#include <algorithm>
#include <cmath>

namespace lab2c::dsp {
// M/S phase 2C: switching between the L/R and the M/S engines. Both engines run on every stereo sample;
// this blends their outputs (each already through its own DSP ON mix) before AIR and Output Gain:
//   y = yLR + x (yMS - yLR),  x: 0 = L/R, 1 = M/S, linear (the two outputs are strongly correlated).
// x moves towards its target by a fixed step per sample over 30 ms and lands exactly on 0 or 1; a new
// target during a transition continues from the current x (no restart, no jump). At x = 0 the L/R output
// is returned untouched, at x = 1 the M/S output, and where both outputs are equal the L/R one, so no
// arithmetic touches a settled mode. No heap allocation or locking.
class StereoModeCrossfade final {
public:
    void prepare(double sampleRate) noexcept {
        const double fs = std::isfinite(sampleRate) && sampleRate > 1.0 ? sampleRate : 48000.0;
        step_ = 1.0f / static_cast<float>(std::max(1.0, fs * 0.030)); // 30 ms
        x_ = target_ = 0.0f;
    }
    // Once per block (the host's mode value at the start of the block).
    void setTarget(bool midSide) noexcept { target_ = midSide ? 1.0f : 0.0f; }
    // First block after prepare: start at the stored mode (no transition from L/R).
    void snap() noexcept { x_ = target_; }
    // Once per sample frame, before mix().
    [[nodiscard]] float next() noexcept {
        if (x_ < target_) x_ = std::min(target_, x_ + step_);
        else if (x_ > target_) x_ = std::max(target_, x_ - step_);
        return x_;
    }
    [[nodiscard]] float position() const noexcept { return x_; }
    [[nodiscard]] bool target() const noexcept { return target_ >= 1.0f; }

    template <typename T>
    [[nodiscard]] static T mix(T lr, T ms, float x) noexcept {
        if (x <= 0.0f) return lr;
        if (x >= 1.0f) return ms;
        const T d = ms - lr;
        if (d == T(0)) return lr; // identical outputs (e.g. DSP OFF): keep the L/R sample as it is
        return lr + static_cast<T>(x) * d;
    }
private:
    float x_ = 0.0f, target_ = 0.0f, step_ = 1.0f;
};
} // namespace lab2c::dsp
