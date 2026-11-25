#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace scenario {
constexpr int segment = 1024;
constexpr int phases = 5;
constexpr std::array rates{44100, 48000, 96000};
constexpr std::array blocks{37, 256};

inline std::string filename(int hz, int channels, int block) {
    return std::to_string(hz) + "-" + std::to_string(channels) + "-" + std::to_string(block) + ".f32";
}
inline float input(int channel, int sample) {
    // Deterministic, exactly representable floats. Peak bursts alternate with quiet spans.
    std::uint32_t index = static_cast<std::uint32_t>(sample + 1);
    auto noise = (index * (channel ? 2654435761u : 2246822519u)) >> 18;
    float pulse = sample % 640 < 200 ? 0.75f : 0.0625f;
    return (static_cast<float>(noise) - 8192.0f) * (pulse / 8192.0f);
}
}
