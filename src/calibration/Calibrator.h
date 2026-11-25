#pragma once
#include "analysis/Features.h"
#include "assist/Goal.h"
#include "dsp/Settings.h"
#include <stop_token>
#include <string>

namespace ia2a {
enum class Outcome { Candidate, Keep, Clarify, Cancelled };
struct Calibration {
    Outcome outcome = Outcome::Keep;
    Settings settings = Settings::transparent();
    Features before, after;
    float matchGainDb = 0, midReductionDb = 0, sideReductionDb = 0;
    std::string explanation;
    int renders = 0;
    bool triedLR = false, triedMS = false;
};
Calibration calibrate(const Clip&,const Settings& applied,const Settings& seed,const Goal&,std::stop_token stop={});
}
