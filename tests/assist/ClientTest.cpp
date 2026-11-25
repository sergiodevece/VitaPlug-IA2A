#include "assist/Client.h"
#include <iostream>
#include <stdexcept>

static void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    using namespace ia2a;
    try {
        const auto cases = juce::JSON::parse(juce::File(IA2A_EXAMPLES).loadFileAsString());
        expect(cases.isArray(), "missing shared examples");
        Client local("", "");
        for (const auto& example : *cases.getArray()) {
            const auto request = example["request"];
            Turn turn;
            turn.requestId = request["requestId"].toString();
            turn.source = request["source"].toString();
            turn.channels = (int)request["channels"];
            turn.prompt = request["prompt"].toString();
            turn.features = request["features"];
            turn.previousGoal = request["previousGoal"];
            turn.hasPending = (bool)request["hasPending"];
            const auto reply = local.request(turn);
            const auto expected = example["expectedKind"].toString();
            expect(reply.requestId == turn.requestId && reply.provider == "local-demo", "id or provider");
            expect((expected == "goal" && reply.kind == ClientReply::Kind::goal)
                || (expected == "question" && reply.kind == ClientReply::Kind::question)
                || (expected == "keep" && reply.kind == ClientReply::Kind::keep), "wrong decision");
            if (example["name"].toString() == "refine") {
                bool reduced = false;
                for (auto p : reply.goal.priorities) reduced |= p.objective == Objective::Air && p.intensity < 0;
                expect(reduced, "refinement lost");
            }
            if (argc >= 2) {
                Client remote(argv[1], "test-token");
                const auto result = remote.request(turn);
                if (argc >= 3) expect(result.provider == argv[2], "wrong connected provider");
                expect(result.kind == reply.kind && result.requestId == reply.requestId, "HTTP client integration");
            }
            std::stop_source stop; stop.request_stop();
            expect(local.request(turn, stop.get_token()).kind == ClientReply::Kind::cancelled, "cancel");
            if (argc>=2) {
                Client wrongToken(argv[1], "incorrect");
                expect(wrongToken.request(turn).kind==ClientReply::Kind::error, "server error fell back to local answer");
            }
            turn.features = juce::var(new juce::DynamicObject);
            turn.features.getDynamicObject()->setProperty("pcm","forbidden");
            expect(local.request(turn).kind == ClientReply::Kind::error, "invalid request accepted");
        }
    } catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
    return 0;
}
