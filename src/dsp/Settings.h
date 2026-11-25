#pragma once
#include <algorithm>
#include <cmath>

namespace ia2a {
struct DomainSettings {
    float thresholdDb{-18}, ratio{4}, attackMs{10}, fastReleaseMs{90}, slowReleaseMs{1800};
    float memory{0.65f}, kneeDb{6}, hpfHz{100}, airDb{0}, makeupDb{0};
    bool hpfOn{false};
    bool operator==(const DomainSettings&) const = default;
};

struct Settings {
    bool enabled{false}, midSide{false};
    float inputTrimDb{0}, outputGainDb{0};
    DomainSettings mid{}, side{};
    static Settings transparent() noexcept { return {}; }
    bool operator==(const Settings&) const = default;
};

inline bool validSettings(const Settings& settings) noexcept {
    auto in = [](float value, float low, float high) { return value >= low && value <= high; };
    auto validDomain = [&](const DomainSettings& d) {
        return in(d.thresholdDb,-60,0) && in(d.ratio,1,20) && in(d.attackMs,0.1f,200)
            && in(d.fastReleaseMs,10,3000) && in(d.slowReleaseMs,100,15000)
            && in(d.memory,0,1) && in(d.kneeDb,0,24) && in(d.hpfHz,20,500)
            && in(d.airDb,0,10) && in(d.makeupDb,-24,24);
    };
    return in(settings.inputTrimDb,-24,24) && in(settings.outputGainDb,-24,24)
        && validDomain(settings.mid) && validDomain(settings.side);
}
}
