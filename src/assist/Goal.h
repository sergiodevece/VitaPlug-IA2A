#pragma once
#include <optional>
#include <vector>

namespace ia2a {
enum class Source { Voice, DrumBus, Other };
enum class Objective { Stabilise, BringForward, Air, PreserveAttack, ReleaseTails, ReduceAttack, EaseCompression, BassWeight };
struct Priority {
    Objective objective;
    float intensity;
};
enum class Control { Threshold, Ratio, Attack, FastRelease, SlowRelease, Memory, Knee, DetectorFrequency, Air, MakeUp, InputTrim, OutputGain, DetectorEnabled, Enabled, StereoMode };
enum class ControlDomain { Both, Main, Side };
enum class ControlChange { Set, Increase, Decrease };
struct ControlEdit {
    Control control;
    ControlDomain domain = ControlDomain::Both;
    ControlChange change;
    std::optional<float> value;
};
struct Goal {
    Source source = Source::Other;
    std::vector<Priority> priorities;
    bool allowLR = true;
    bool allowMS = true;
    std::vector<ControlEdit> edits;
    bool compensateGain = false;
};
}
