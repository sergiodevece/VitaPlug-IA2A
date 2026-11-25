#include "Connection.h"

namespace ia2a {
std::optional<LocalConnection> publishedConnection() {
    auto path = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/Application Support/VitaPlug/IA2A/connection.json");
    const auto size = path.getSize();
    if (size <= 0 || size > 1024)
        return std::nullopt;
    const auto settings = juce::JSON::parse(path.loadFileAsString());
    const auto port = settings["port"];
    const auto token = settings["token"].toString();
    if (!port.isInt() || static_cast<int>(port) < 1 || static_cast<int>(port) > 65535
        || token.length() != 64 || !token.containsOnly("0123456789abcdef"))
        return std::nullopt;
    return LocalConnection{"http://127.0.0.1:" + port.toString() + "/v1/turn", token};
}
}
