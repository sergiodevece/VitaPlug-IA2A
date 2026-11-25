#pragma once
#include <algorithm>
#include <cmath>

namespace lab2c::dsp {
// Sidechain HPF: 2nd-order Butterworth high-pass (12 dB/oct), topology-preserving state-variable
// filter (Zavalishin / Simper), double-precision state. It only feeds the level detector; the
// audio that receives the gain is never filtered. Stable under per-sample coefficient changes.
// No heap allocation or locking.
struct SidechainHpfCoefficients {
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;
    static constexpr double k = 1.4142135623730951; // 1/Q, Q = 1/sqrt(2) (Butterworth)
    static SidechainHpfCoefficients forFrequency(double hz, double sampleRate) noexcept {
        const double g = std::tan(3.14159265358979323846 * hz / sampleRate);
        SidechainHpfCoefficients c;
        c.a1 = 1.0 / (1.0 + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }
};

class SidechainHpf final {
public:
    void reset() noexcept { ic1_ = ic2_ = 0.0; }
    [[nodiscard]] float process(float input, const SidechainHpfCoefficients& c) noexcept {
        const double x = std::isfinite(input) ? static_cast<double>(input) : 0.0;
        const double v3 = x - ic2_;
        const double v1 = c.a1 * ic1_ + c.a2 * v3;
        const double v2 = ic2_ + c.a2 * ic1_ + c.a3 * v3;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        return static_cast<float>(x - SidechainHpfCoefficients::k * v1 - v2);
    }
private:
    double ic1_ = 0.0, ic2_ = 0.0;
};

// Cutoff shared by both channels: moves to a new target along a 20 ms ramp in log frequency
// (constant musical speed), landing exactly on the target. Coefficients are recomputed only while
// the cutoff moves.
class SidechainHpfCutoff final {
public:
    static constexpr double kMinHz = 20.0, kMaxHz = 500.0;
    void prepare(double sampleRate) noexcept {
        sampleRate_ = std::isfinite(sampleRate) && sampleRate > 1.0 ? sampleRate : 48000.0;
        rampSamples_ = std::max(1, static_cast<int>(std::lround(sampleRate_ * 0.020)));
        remaining_ = 0;
        current_ = target_;
        coefficients_ = SidechainHpfCoefficients::forFrequency(std::exp(current_), sampleRate_);
    }
    void setTarget(float hz) noexcept {
        const double t = std::log(std::isfinite(hz) ? std::clamp(static_cast<double>(hz), kMinHz, kMaxHz) : 100.0);
        if (t == target_) return; // re-sent every block; must not restart the ramp
        target_ = t;
        step_ = (target_ - current_) / static_cast<double>(rampSamples_);
        remaining_ = rampSamples_;
    }
    void snap() noexcept {
        current_ = target_;
        remaining_ = 0;
        coefficients_ = SidechainHpfCoefficients::forFrequency(std::exp(current_), sampleRate_);
    }
    const SidechainHpfCoefficients& next() noexcept {
        if (remaining_ > 0) {
            --remaining_;
            current_ = target_ - step_ * static_cast<double>(remaining_); // lands exactly on the target
            coefficients_ = SidechainHpfCoefficients::forFrequency(std::exp(current_), sampleRate_);
        }
        return coefficients_;
    }
    [[nodiscard]] const SidechainHpfCoefficients& coefficients() const noexcept { return coefficients_; }
    [[nodiscard]] double currentHz() const noexcept { return std::exp(current_); }
    [[nodiscard]] bool moving() const noexcept { return remaining_ > 0; }
private:
    double sampleRate_ = 48000.0, target_ = std::log(100.0), current_ = std::log(100.0), step_ = 0.0;
    int rampSamples_ = 960, remaining_ = 0;
    SidechainHpfCoefficients coefficients_ = SidechainHpfCoefficients::forFrequency(100.0, 48000.0);
};
} // namespace lab2c::dsp
