#pragma once

#include "Goal.h"
#include <juce_core/juce_core.h>
#include <optional>

namespace ia2a {
// Returns a local edit only when every clause can be interpreted unambiguously.
std::optional<Goal> directRequest(const juce::String& prompt);
}
