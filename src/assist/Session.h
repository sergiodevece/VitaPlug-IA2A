#pragma once
#include "dsp/Settings.h"
#include <cstdint>
#include <optional>

namespace ia2a {
struct Monitor {
    Settings applied, candidate;
    bool hasCandidate=false, preview=false;
    float gainDb=0;
};
// All control-side calls are serialized by the processor.
class Session {
public:
    std::uint64_t begin() { working=true; return ++generation; }
    bool current(std::uint64_t ticket) const { return ticket==generation; }
    void finish(std::uint64_t ticket) { if(current(ticket)) working=false; }
    bool offer(std::uint64_t ticket, const Settings&, float gainDb);
    void invalidate();
    void discard() { invalidate(); }
    void listen(bool candidate,bool matched);
    bool apply();
    bool undo();
    void restore(const Settings&);
    Settings applied() const { return saved; }
    Settings seed() const { return proposed.value_or(saved); }
    bool pending() const { return proposed.has_value(); }
    bool canApply() const { return pending() && !working; }
    bool canUndo() const { return previous.has_value(); }
    Monitor monitor() const;
private:
    Settings saved;
    std::optional<Settings> proposed, previous;
    std::uint64_t generation=0;
    bool preview=false, match=true, working=false;
    float compensation=0;
};
}
