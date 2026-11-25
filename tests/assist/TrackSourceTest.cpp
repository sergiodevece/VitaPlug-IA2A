#include "assist/TrackSource.h"
#include "audio/PluginProcessor.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testTrackNameInference() {
    const std::array cases{
        std::pair{"", "other"},
        std::pair{"Audio 4", "other"},
        std::pair{"Snowdrums", "other"},
        std::pair{"60s Songwriter Drums 01", "drums"},
        std::pair{"Bus de batería", "drums"},
        std::pair{"Bateri\u0301a Main", "drums"},
        std::pair{"Kick / Snare", "drums"},
        std::pair{"Kick_In", "kick"},
        std::pair{"Kick01", "kick"},
        std::pair{"Caja Top", "snare"},
        std::pair{"Hi-Hats", "percussion"},
        std::pair{"Sub Bass", "bass"},
        std::pair{"Bass Guitar DI", "bass"},
        std::pair{"Guitarra rítmica", "guitar"},
        std::pair{"Lead VOX", "voice"},
        std::pair{"Lead Synth", "synth"},
        std::pair{"Grand Piano", "keys"},
        std::pair{"Violín legato", "strings"},
        std::pair{"Trombón", "brass"},
        std::pair{"Stereo Out", "mix"}
    };

    for (const auto [trackName, expected] : cases)
        if (ia2a::inferTrackSource(juce::String::fromUTF8(trackName)) != expected)
            throw std::runtime_error(std::string("Unexpected source for ") + trackName);
}

void testSourceSelectionAndRestore() {
    ia2a::Processor processor;
    expect(processor.snapshot().source == "auto", "A new instance should infer the source");
    processor.updateTrackProperties({juce::String("Kick In"), {}});
    expect(processor.snapshot().effectiveSource == "kick", "Host metadata did not set the source");

    processor.setSource("brass");
    processor.updateTrackProperties({juce::String("Vocals"), {}});
    expect(processor.snapshot().effectiveSource == "brass", "Track rename overrode a manual choice");

    juce::MemoryBlock state;
    processor.getStateInformation(state);
    ia2a::Processor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    expect(restored.snapshot().source == "brass", "Manual source was not restored");
    restored.updateTrackProperties({juce::String("Guitar"), {}});
    restored.setSource("auto");
    expect(restored.snapshot().effectiveSource == "guitar", "Returning to auto did not use the current name");

    processor.setSource("auto");
    processor.getStateInformation(state);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    expect(restored.snapshot().source == "auto" && restored.snapshot().effectiveSource == "guitar",
           "Auto mode should persist while using the host's current track name");

    // The previous version stored a version number and a byte for voice, drums or other.
    juce::MemoryBlock legacyState;
    {
        juce::MemoryOutputStream legacy(legacyState, false);
        legacy.writeInt(0x49413241);
        legacy.writeInt(1);
        legacy.writeByte(1);
        legacy.write(static_cast<const char*>(state.getData()) + 9, state.getSize() - 9);
    }
    restored.setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));
    expect(restored.snapshot().source == "drums", "A legacy session lost its source selection");
}
}

int main() {
    try {
        testTrackNameInference();
        testSourceSelectionAndRestore();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
