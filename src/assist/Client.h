#pragma once

#include "Goal.h"
#include <juce_core/juce_core.h>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace ia2a {

struct Calibration;
struct Features;
struct Settings;
juce::var encodeGoal(const Goal&);
juce::var turnSettings(const Settings&,int channels);
juce::var turnFeatures(const Features&);
juce::var turnResult(const Calibration&);

struct Turn {
    juce::String requestId, source, prompt, sourceOrigin, lastAction, auditioning;
    int channels = 1;
    juce::StringArray history;
    juce::var features {new juce::DynamicObject}, previousGoal, appliedSettings, pendingSettings, previousResult;
    bool hasApplied = false, hasPending = false;
};

struct ClientReply {
    enum class Kind { goal, question, keep, error, cancelled };
    Kind kind = Kind::error;
    juce::String requestId, text, provider;
    Goal goal;
    juce::var wireGoal;
};

class Client {
public:
    using Completion = std::function<void(ClientReply)>;
    explicit Client(bool demoMode = false);
    Client(juce::String endpoint, juce::String token);
    ~Client();

    // Synchronous, for worker threads. Empty URL selects the embedded demo.
    ClientReply request(const Turn&, std::stop_token = {}) const;
    // Replaces prior work. Completion runs on the private worker thread.
    void send(Turn, Completion);
    void cancel();

private:
    struct Job { Turn turn; Completion done; };
    void run();
    juce::String url, bearer;
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<Job> queued;
    std::stop_source active;
    bool closing = false;
    bool usePublishedConnection = false;
    std::thread worker;
};

}
