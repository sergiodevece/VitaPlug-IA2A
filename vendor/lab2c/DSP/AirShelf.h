#pragma once
#include <algorithm>
#include <cmath>

namespace lab2c::dsp {
// AIR: a fixed 10 kHz high shelf, 0 to +10 dB, after the optical processing and before Output Gain.
// 2nd-order topology-preserving state-variable filter (A. Simper's high-shelf form, the same family
// as the sidechain HPF), double-precision state, Q = 1/sqrt(2). Linear: no saturation or harmonics.
// The gain follows a 20 ms linear ramp in dB that lands exactly on the target; coefficients are
// recomputed only while the effective gain changes (TPT stays well behaved under modulation). The
// effective gain is AIR x the DSP ON mix, so AIR never acts with DSP OFF and follows its 15 ms ramp.
// At an effective 0 dB the input is returned unchanged (exact bypass) and nothing runs.
// No heap allocation or locking; no latency.
class AirShelf final {
public:
    static constexpr double kFrequencyHz = 10000.0, kMaxDb = 10.0;
    static constexpr double kQ = 0.70710678118654752; // Q = 1/sqrt(2): in this form, the steepest monotonic shelf (RBJ S = 1) at every gain

    void prepare(double sampleRate) noexcept {
        const double fs = std::isfinite(sampleRate) && sampleRate > 1.0 ? sampleRate : 48000.0;
        tanBase_ = std::tan(3.14159265358979323846 * std::min(kFrequencyHz, 0.45 * fs) / fs);
        rampSamples_ = std::max(1, static_cast<int>(std::lround(fs * 0.020)));
        remaining_ = 0;
        current_ = target_;
        coefficientDb_ = -1.0;
        cold_ = true;
    }
    // Once per block: AIR gain in dB (0..10).
    void setTarget(float db) noexcept {
        const double t = std::isfinite(db) ? std::clamp(static_cast<double>(db), 0.0, kMaxDb) : 0.0;
        if (t == target_) return; // re-sent every block; must not restart the ramp
        target_ = t;
        step_ = (target_ - current_) / static_cast<double>(rampSamples_);
        remaining_ = rampSamples_;
    }
    // First block after prepare: start at the stored gain (no ramp).
    void snap() noexcept { current_ = target_; remaining_ = 0; }
    [[nodiscard]] double currentDb() const noexcept { return current_; }

    template <typename T>
    [[nodiscard]] T process(T input, float wet) noexcept {
        // Off and settled: exact bypass, kept small so it inlines into the render loop.
        if (remaining_ == 0 && current_ <= 0.0) { cold_ = true; return input; }
        return processActive(input, wet);
    }
private:
    template <typename T>
    [[gnu::noinline]] T processActive(T input, float wet) noexcept {
        if (remaining_ > 0) {
            --remaining_;
            current_ = target_ - step_ * static_cast<double>(remaining_); // lands exactly on the target
        }
        const double db = current_ * static_cast<double>(wet);
        if (db <= 0.0) { cold_ = true; return input; } // exact bypass (0 dB or DSP OFF)
        if (cold_) { ic1_ = ic2_ = 0.0; cold_ = false; } // the gain starts from 0 dB here, so the restart is silent
        if (db != coefficientDb_) setCoefficients(db);
        const bool finite = std::isfinite(static_cast<double>(input));
        const double x = finite ? static_cast<double>(input) : 0.0;
        const double v3 = x - ic2_;
        const double v1 = a1_ * ic1_ + a2_ * v3;
        const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        if (!finite) return input; // non-finite samples stay as they are; the state continues from 0
        return static_cast<T>(m0_ * x + m1_ * v1 + m2_ * v2);
    }
    void setCoefficients(double db) noexcept {
        const double A = std::pow(10.0, db / 40.0), k = 1.0 / kQ, g = tanBase_ * std::sqrt(A);
        a1_ = 1.0 / (1.0 + g * (g + k));
        a2_ = g * a1_;
        a3_ = g * a2_;
        m0_ = A * A;            // gain above the shelf: 10^(db/20)
        m1_ = k * (1.0 - A) * A;
        m2_ = 1.0 - A * A;      // below the shelf: m0 + m2 = 1 (0 dB)
        coefficientDb_ = db;
    }
    double tanBase_ = 0.0, target_ = 0.0, current_ = 0.0, step_ = 0.0, coefficientDb_ = -1.0;
    double a1_ = 1.0, a2_ = 0.0, a3_ = 0.0, m0_ = 1.0, m1_ = 0.0, m2_ = 0.0, ic1_ = 0.0, ic2_ = 0.0;
    int rampSamples_ = 960, remaining_ = 0;
    bool cold_ = true;
};
} // namespace lab2c::dsp
