#include "assist/Client.h"
#include "calibration/Calibrator.h"
#include <iostream>
#include <stdexcept>

static void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void expectClaudeConversation(const char* endpoint) {
    using namespace ia2a;
    Client claude(endpoint,"test-token");
    Turn turn;
    turn.requestId="native-chat-1";
    turn.source="other";
    turn.sourceOrigin="unknown";
    turn.channels=2;
    turn.prompt=juce::String::fromUTF8("¿Puedes quitarle ese punto que se come el resto?");
    Features features;
    features.usable=true;
    features.attackDb=6;
    features.decayDb=10;
    features.highSpreadDb=4;
    features.sideEnergy=.012f;
    features.sideTailRatioDb=-8;
    features.highPulseCorrelation=.2f;
    turn.features=turnFeatures(features);

    const auto question=claude.request(turn);
    expect(question.kind==ClientReply::Kind::question,"Claude should ask what an unclear source is");

    turn.history.add(turn.prompt);
    turn.history.add(question.text);
    turn.requestId="native-chat-2";
    turn.prompt=juce::String::fromUTF8("Es un bajo con púa. La primera nota de cada frase pega un latigazo.");
    const auto softer=claude.request(turn);
    expect(softer.kind==ClientReply::Kind::goal && softer.goal.priorities.size()==1 &&
           softer.goal.priorities.front().objective==Objective::ReduceAttack,"Claude should use the free-form answer and measured attack");

    Calibration trial;
    trial.outcome=Outcome::Candidate;
    trial.before=features;
    trial.after=features;
    trial.after.attackDb=3.5f;
    trial.midReductionDb=4;
    trial.sideReductionDb=1;
    turn.history.add(turn.prompt);
    turn.history.add(softer.text);
    turn.requestId="native-chat-3";
    turn.prompt=juce::String::fromUTF8("Casi, pero ahora deja que tenga un pelín más de vida.");
    turn.previousGoal=softer.wireGoal;
    turn.previousResult=turnResult(trial);
    turn.lastAction="applied";
    turn.hasApplied=true;
    const auto ease=claude.request(turn);
    expect(ease.kind==ClientReply::Kind::goal && ease.goal.priorities.size()==1 &&
           ease.goal.priorities.front().objective==Objective::EaseCompression,"Claude should refine the preceding measured trial");
}

static void expectDirectControls(const char* endpoint) {
    using namespace ia2a;
    Goal prior;
    prior.allowMS=false;
    prior.edits.push_back({Control::Threshold,ControlDomain::Both,ControlChange::Decrease,3.f});
    prior.compensateGain=true;
    Turn turn;
    turn.requestId="native-controls-1";
    turn.source="drums";
    turn.prompt="Ahora pon el umbral en -32,5 dB, sube el AIR y compensa el volumen.";
    turn.channels=2;
    turn.auditioning="pending";
    turn.hasPending=true;
    turn.pendingSettings=turnSettings(Settings{},2);
    turn.previousGoal=encodeGoal(prior);

    Client claude(endpoint,"test-token");
    const auto reply=claude.request(turn);
    expect(reply.kind==ClientReply::Kind::goal && reply.goal.priorities.empty() &&
           reply.goal.edits.size()==3 && reply.goal.compensateGain,"direct goal with no musical priorities");
    const auto& threshold=reply.goal.edits.front();
    const auto& air=reply.goal.edits[1];
    expect(threshold.control==Control::Threshold && threshold.change==ControlChange::Set &&
           threshold.domain==ControlDomain::Both && threshold.value==-32.5f,"absolute threshold value lost");
    expect(air.control==Control::Air && air.change==ControlChange::Increase &&
           !air.value,"nullable default became a numeric edit");
    const auto& detector=reply.goal.edits.back();
    expect(detector.control==Control::DetectorEnabled && detector.value==1.f,
           "detector filter switch did not survive the wire");

    turn.requestId="native-controls-2";
    turn.prompt="Quiero que conserve más peso en los graves.";
    Features features;
    features.lowRatioDb=-6.f;
    turn.features=turnFeatures(features);
    turn.previousGoal=reply.wireGoal;
    const auto bass=claude.request(turn);
    expect(bass.kind==ClientReply::Kind::goal && bass.goal.priorities.size()==1 &&
           bass.goal.priorities.front().objective==Objective::BassWeight,
           "low end weight was not interpreted as a compressor objective");
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
        if(argc>=3 && juce::String(argv[2])=="claude") {
            expectClaudeConversation(argv[1]);
            expectDirectControls(argv[1]);
        }
    } catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
    return 0;
}
