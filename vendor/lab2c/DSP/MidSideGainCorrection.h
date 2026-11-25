#pragma once
#include "AirShelf.h"
#include "GainStage.h"
#include <algorithm>
#include <cmath>

namespace lab2c::dsp {
// Phase C (0.1.8): SIDE AIR and SIDE MAKE-UP for the M/S path as a PARALLEL correction. The 0.1.7 chain is not
// touched: the M/S engine output (with the MID MAKE-UP), the L/R <-> M/S crossfade and the global AIR (MID gain)
// stay exactly as they are; this class produces, per stereo frame, the S difference
//   dA = AIR_SIDE(S_side) - AIR_MID'(S_mid)
// that the processor adds (weighted by the crossfade position and by the LINKED -> SEPARATE blend) after the global
// AIR. AIR_MID' is the same AirShelf as the global stage with the same targets in the same blocks, so on S it equals
// the S part of the global AIR to output rounding, whatever its automation: the global AIR's effect on S is removed in
// parallel (never by inverting a moving filter) and AIR_SIDE applies once.
//   S_mid  = (ml - mr) / 2         S of the M/S path after its DSP ON mix, as the global AIR receives it
//   S_dry  = (inL - inR) / 2       S of the untouched input
//   S_side = S_mid + (gSide / gMid - 1) (S_mid - S_dry (1 - wet))
// (S_mid - S_dry (1 - wet)) is the processed share of S (wet x MID MAKE-UP x engine output), so SIDE MAKE-UP acts on
// the processed path only, never on dry, and at DSP OFF (S_mid = S_dry, wet 0) S_side = S_mid.
// Equal SIDE and MID values: S_side = S_mid and, with the same history, dA is exactly 0. A pure MID input gives
// S_mid = S_dry = 0 and dA = 0 exactly.
// The blend (20 ms, linear, lands exactly on 0 / 1) fades the correction in (SEPARATE) or out (LINKED); at 0 the
// processor adds nothing. While LINKED the caller passes the MID values as the SIDE targets, so the SIDE stages share
// MID's history (dA = 0) and a Separate / Link moves blend and SIDE ramps together, monotonically. Both shelves run on
// every stereo frame so they are warm before a mode fade or a blend.
// No heap allocation or locking.
class MidSideGainCorrection final {
public:
    void prepare(double sampleRate) noexcept {
        const double fs = std::isfinite(sampleRate) && sampleRate > 1.0 ? sampleRate : 48000.0;
        airMid_.prepare(fs);
        airSide_.prepare(fs);
        sideMakeup_.prepare(fs);
        blendStep_ = 1.0f / static_cast<float>(std::max(1.0, fs * 0.020)); // 20 ms
        blend_ = blendTarget_;
    }
    // Once per block: the MID AIR (the global stage's target), the SIDE AIR and SIDE MAKE-UP parameters, and the
    // LINKED (false) / SEPARATE (true) state.
    void setTargets(float midAirDb, float sideAirDb, float sideMakeupDb, bool separate) noexcept {
        airMid_.setTarget(midAirDb);
        airSide_.setTarget(sideAirDb);
        sideMakeup_.setTarget(sideMakeupDb);
        blendTarget_ = separate ? 1.0f : 0.0f;
    }
    // First block after prepare: start at the stored values and state (no ramps).
    void snap() noexcept { airMid_.snap(); airSide_.snap(); sideMakeup_.snap(); blend_ = blendTarget_; }

    // Once per sample frame (mono and stereo, so the ramps advance the same way in both): the blend and the SIDE
    // MAKE-UP gain for this frame.
    struct Frame { float blend, sideGain; };
    [[nodiscard]] Frame next() noexcept {
        if (blend_ < blendTarget_) blend_ = std::min(blendTarget_, blend_ + blendStep_);
        else if (blend_ > blendTarget_) blend_ = std::max(blendTarget_, blend_ - blendStep_);
        return {blend_, sideMakeup_.next()};
    }
    // Once per stereo frame, after next(): the S difference dA (see above). midGain is the MID MAKE-UP gain of this
    // frame (never 0: -24 dB at most), wet the DSP ON mix.
    template <typename T>
    [[nodiscard]] double airDifference(T inL, T inR, T ml, T mr, float midGain, float sideGain, float wet) noexcept {
        const double sMid = (static_cast<double>(ml) - static_cast<double>(mr)) * 0.5;
        double sSide = sMid;
        if (sideGain != midGain) {
            const double sDry = (static_cast<double>(inL) - static_cast<double>(inR)) * 0.5;
            const double processed = sMid - sDry * (1.0 - static_cast<double>(wet));
            sSide = sMid + (static_cast<double>(sideGain) / static_cast<double>(midGain) - 1.0) * processed;
        }
        if (!std::isfinite(sSide)) sSide = 0.0; // non-finite input: the shelves treat it as 0 (their own rule)
        return airSide_.process(sSide, wet) - airMid_.process(std::isfinite(sMid) ? sMid : 0.0, wet);
    }
    [[nodiscard]] float blend() const noexcept { return blend_; }
    [[nodiscard]] double sideMakeupDb() const noexcept { return sideMakeup_.currentDb(); }
    [[nodiscard]] double sideAirDb() const noexcept { return airSide_.currentDb(); }

private:
    AirShelf airMid_{}, airSide_{};
    SmoothedGainDb sideMakeup_{};
    float blend_ = 0.0f, blendTarget_ = 0.0f, blendStep_ = 1.0f;
};
} // namespace lab2c::dsp
