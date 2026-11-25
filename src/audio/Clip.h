#pragma once
#include <cstdint>
#include <vector>

namespace ia2a {
struct Clip {
    double sampleRate = 0;
    std::vector<std::vector<float>> track, reference;
    int prerollSamples = 0;
    std::uint64_t generation = 0;
};
}
