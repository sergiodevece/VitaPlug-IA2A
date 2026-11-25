#pragma once

#include <juce_core/juce_core.h>
#include <span>

namespace ia2a {
struct TrackSourceOption {
    const char* id;
    const char* label;
};

std::span<const TrackSourceOption> trackSourceOptions() noexcept;
bool isTrackSourceSelection(const juce::String&);
juce::String inferTrackSource(const juce::String& trackName);
juce::String trackSourceLabel(const juce::String& id);
}
