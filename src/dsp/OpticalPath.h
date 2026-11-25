#pragma once
#include "audio/AudioTypes.h"
#include "dsp/Settings.h"
#include "vendor/lab2c/DSP/AirShelf.h"
#include "vendor/lab2c/DSP/GainStage.h"
#include "vendor/lab2c/DSP/MidSideGainCorrection.h"
#include "vendor/lab2c/DSP/MidSideOpticalBridge.h"
#include "vendor/lab2c/DSP/StereoModeCrossfade.h"
#include "vendor/lab2c/DSP/StereoOpticalBridge.h"

namespace ia2a {
struct GrMeters {
    float reductionDb{}, midDb{}, sideDb{};
    bool midSide{};
};

// A path belongs to one rendering thread. Apply settings at block boundaries.
class OpticalPath {
public:
    void prepare(double sampleRate, int channels) noexcept;
    void setSettings(const Settings& settings) noexcept;
    void reset() noexcept;
    void process(AudioBlockView block, GrMeters& gr) noexcept;

private:
    Settings settings_{};
    lab2c::dsp::StereoOpticalBridge optical_{};
    lab2c::dsp::MidSideOpticalBridge midSide_{};
    lab2c::dsp::StereoModeCrossfade modeFade_{};
    lab2c::dsp::SmoothedGainDb inputTrim_{}, makeupGain_{};
    lab2c::dsp::MidSideGainCorrection msGains_{};
    std::array<lab2c::dsp::AirShelf,2> air_{};
    int channels_{}, resetTransitionSamples_{1};
    float gainSmoothed_{1}, gainStepCoefficient_{};
    bool snapPending_{true};
};
}
