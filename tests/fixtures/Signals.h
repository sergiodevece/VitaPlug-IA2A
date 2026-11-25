#pragma once
#include "audio/Clip.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace ia2a::fixtures {
inline constexpr double tau = 6.283185307179586;
inline Clip voice(float quiet = .085f, float loud = .42f) {
    Clip c;
    c.sampleRate = 32000;
    c.track.resize(1);
    for (int n = 0; n < 96000; ++n) {
        double t = n / c.sampleRate;
        double p = std::fmod(t, .6);
        double env = std::min(1., p * 100.) * std::min(1., (.6 - p) * 100.);
        double tone = std::sin(tau * 185 * t) + .16 * std::sin(tau * 740 * t);
        double consonant = p < .06 ? .07 * std::sin(tau * 7500 * t) : 0.;
        c.track[0].push_back(static_cast<float>((static_cast<int>(t / .6) % 2 ? loud : quiet) * env * (tone + consonant)));
    }
    return c;
}
inline Clip drums(bool wide) {
    Clip c;
    c.sampleRate = 32000;
    c.track.resize(2);
    for (int n = 0; n < 76800; ++n) {
        double t = n / c.sampleRate;
        double beat = std::fmod(t, .4);
        double mid = .7 * std::exp(-beat * 18.) * std::sin(tau * 91 * t);
        double cymbal = .085 * std::exp(-beat * 4.2) * (std::sin(tau * 6901 * t) + .33 * std::sin(tau * 9451 * t));
        c.track[0].push_back(static_cast<float>(mid + cymbal));
        c.track[1].push_back(static_cast<float>(mid + (wide ? -cymbal : cymbal)));
    }
    return c;
}
}
