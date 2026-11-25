#pragma once
#include <vector>

namespace ia2a {
enum class Source { Voice, DrumBus, Other };
enum class Objective { Stabilise, BringForward, Air, PreserveAttack, ReleaseTails };
struct Priority {
    Objective objective;
    float intensity;
};
struct Goal {
    Source source = Source::Other;
    std::vector<Priority> priorities;
    bool allowLR = true;
    bool allowMS = true;
};
}
