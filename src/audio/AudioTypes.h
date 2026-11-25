#pragma once
#include <array>

namespace ia2a {
struct AudioBlockView {
    std::array<float*,2> data{};
    int channels{};
    int samples{};
};
struct ConstBlockView {
    std::array<const float*,2> data{};
    int channels{};
    int samples{};
};
}
