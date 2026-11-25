#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace ia2a {
struct LocalConnection {
    juce::String turnUrl;
    juce::String token;
};

std::optional<LocalConnection> publishedConnection();
}
