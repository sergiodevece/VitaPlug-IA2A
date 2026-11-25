#pragma once
#include "AudioTypes.h"
#include "Clip.h"
#include <atomic>
#include <mutex>
#include <optional>

namespace ia2a {
class Capture {
public:
    enum State { Idle, Armed, Recording, Ready };
    void prepare(double sampleRate, int trackChannels, int auxChannels);
    void arm(bool requestReference);
    void invalidate() noexcept;
    void clearHistory() noexcept { clearRequested.store(true); }
    void push(ConstBlockView track, ConstBlockView aux) noexcept;
    bool finishAvailable();
    std::optional<Clip> take();
    State status() const noexcept { return state.load(); }
    float progress() const noexcept { return fraction.load(); }
    std::uint64_t revision() const noexcept { return generation.load(); }
private:
    using Bank = std::vector<std::vector<float>>;
    std::mutex mutex;
    Bank history, recorded;
    std::atomic<bool> clearRequested{false};
    std::atomic<State> state{Idle};
    std::atomic<float> fraction{0};
    std::atomic<std::uint64_t> generation{0};
    std::uint64_t runningRevision=0;
    int mainCount=0, auxCount=0, prior=0, target=0, head=0, filled=0, used=0, prefix=0;
    double rate=0;
    bool includeAux=false;
};
}
