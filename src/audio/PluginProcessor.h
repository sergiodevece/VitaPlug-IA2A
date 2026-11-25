#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Capture.h"
#include "Compare.h"
#include "assist/Client.h"
#include <array>
#include <condition_variable>
#include <thread>

namespace ia2a {
struct Snapshot {
    juce::String source,effectiveSource,trackName,prompt,message,provider;
    bool reference{},auxActive{},recording{},busy{},pending{},canApply{},canUndo{},preview{},matched{},hasClip{},midSide{};
    bool needsClarification{},turnFailed{};
    float progress{},inputDb{},outputDb{},reductionDb{};
    int channels{};
    std::array<float,120> grMain{},grSide{};
};
class Processor final : public juce::AudioProcessor {
public:
    explicit Processor(bool demoMode = false);
    ~Processor() override;
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    void updateTrackProperties(const TrackProperties&) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "IA2A"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    Snapshot snapshot() const;
    void setSource(juce::String);
    void setReference(bool);
    void captureAudio();
    void submit(juce::String);
    void listen(bool preview,bool matched);
    void apply(); void discard(); void undo();
private:
    void invalidateLocked(bool resetConversation = false);
    void queueLocked();
    void runWorker();
    void armLocked();
    struct Job {
        Turn turn;
        std::shared_ptr<const Clip> clip;
        Settings applied,seed;
        std::uint64_t ticket=0;
        std::stop_token stop;
    };
    Capture capture;
    Compare compare;
    Session session;
    mutable std::mutex control;
    std::condition_variable wake;
    std::shared_ptr<const Clip> clip;
    juce::String source="auto", inferredSource="other", trackName, prompt, message=juce::String::fromUTF8("Reproduce la pista y pulsa Escuchar."), provider;
    juce::StringArray history;
    juce::var previousGoal, previousResult;
    juce::String lastAction;
    const bool demoMode;
    std::optional<Job> job;
    std::stop_source cancellation;
    bool reference=false, matched=true, busy=false, pendingSend=false, closing=false;
    bool needsClarification=false, turnFailed=false;
    std::uint64_t knownRevision=0;
    std::atomic<std::uint64_t> monitorRevision{0};
    std::atomic<bool> auxActive{false},ms{false};
    std::atomic<int> channels{0},head{0},historySize{0};
    std::array<std::atomic<float>,120> mainHistory{},sideHistory{};
    std::atomic<float> meterIn{-120},meterOut{-120},meterGr{0};
    int interval=1, since=0;
    float peakMain=0,peakSide=0;
    std::optional<juce::int64> expectedPosition;
    bool previouslyPlaying=false;
    std::thread worker;
};
}
