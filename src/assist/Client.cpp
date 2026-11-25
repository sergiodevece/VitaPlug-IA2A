#include "Client.h"
#include "Connection.h"
#include "analysis/Features.h"
#include "calibration/Calibrator.h"
#include "dsp/Settings.h"
#include "ia2a_turn_data.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace ia2a {
namespace {

using juce::String;
using juce::var;
using namespace std::chrono_literals;

var object() { return var(new juce::DynamicObject); }
void put(var& o, const juce::Identifier& key, var value) { o.getDynamicObject()->setProperty(key, value); }

var encode(const Turn& t) {
    auto out = object();
    put(out,"version",1); put(out,"requestId",t.requestId); put(out,"source",t.source);
    put(out,"prompt",t.prompt); put(out,"channels",t.channels);
    juce::Array<var> history;
    for (const auto& item : t.history) history.add(item);
    put(out,"history",history); put(out,"features",t.features); put(out,"previousGoal",t.previousGoal);
    if(!t.appliedSettings.isVoid())put(out,"appliedSettings",t.appliedSettings);
    if(!t.pendingSettings.isVoid())put(out,"pendingSettings",t.pendingSettings);
    if(!t.previousResult.isVoid())put(out,"previousResult",t.previousResult);
    if(t.sourceOrigin.isNotEmpty())put(out,"sourceOrigin",t.sourceOrigin);
    if(t.lastAction.isNotEmpty())put(out,"lastAction",t.lastAction);
    put(out,"hasApplied",t.hasApplied); put(out,"hasPending",t.hasPending);
    return out;
}

const var& definitions() {
    static const var defs = juce::JSON::parse(juce::String::fromUTF8(schemaJson))["$defs"];
    return defs;
}

bool matches(const var& rule, const var& value) {
    if (rule.hasProperty("$ref"))
        return matches(definitions()[juce::Identifier(rule["$ref"].toString().fromLastOccurrenceOf("/",false,false))],value);
    for (const auto* field : {"oneOf","anyOf"}) {
        if (auto* alternatives=rule[field].getArray()) {
            const auto count=std::count_if(alternatives->begin(),alternatives->end(),[&](const var& v){return matches(v,value);});
            return String(field)=="oneOf" ? count==1 : count>0;
        }
    }
    if (rule.hasProperty("const")) return value.equalsWithSameType(rule["const"]);
    if (auto* options=rule["enum"].getArray())
        return std::any_of(options->begin(),options->end(),[&](const var& v){return value.equalsWithSameType(v);});
    auto type=rule["type"].toString();
    if (type=="null") return value.isVoid();
    if (type=="boolean") return value.isBool();
    if (type=="number") {
        if (!value.isDouble() && !value.isInt() && !value.isInt64()) return false;
        auto n=static_cast<double>(value);
        return std::isfinite(n) && n>=static_cast<double>(rule["minimum"]) && n<=static_cast<double>(rule["maximum"]);
    }
    if (type=="string") {
        const auto s=value.toString();
        if (!value.isString() || s.trim().isEmpty()) return false;
        // requestId is the only pattern in protocol v1.
        if (rule.hasProperty("pattern"))
            return s.length()<=80 && s.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_");
        return s.length()>=static_cast<int>(rule["minLength"]) && s.length()<=static_cast<int>(rule["maxLength"]);
    }
    if (type=="array") {
        const auto* a=value.getArray();
        return a && a->size()>=static_cast<int>(rule["minItems"]) && a->size()<=static_cast<int>(rule["maxItems"])
            && std::all_of(a->begin(),a->end(),[&](const var& v){return matches(rule["items"],v);});
    }
    if (type=="object" && value.getDynamicObject()) {
        const auto& allowed=rule["properties"];
        for (const auto& required : *rule["required"].getArray())
            if (!value.hasProperty(required.toString())) return false;
        for (const auto& p : value.getDynamicObject()->getProperties())
            if (!allowed.hasProperty(p.name) || !matches(allowed[p.name],p.value)) return false;
        return true;
    }
    return false;
}

std::optional<Goal> decodeGoal(const var& wire,const String& source,int channels) {
    if (!matches(definitions()["goal"],wire)) return {};
    Goal goal;
    goal.source=source=="voice"?Source::Voice:source=="drums"?Source::DrumBus:Source::Other;
    goal.allowMS=static_cast<bool>(wire["allowMS"]); goal.allowLR=static_cast<bool>(wire["allowLR"]);
    if ((!goal.allowMS && !goal.allowLR) || (goal.allowMS && channels==1)) return {};
    static constexpr std::pair<const char*,Objective> vocabulary[]={
        {"stabilise",Objective::Stabilise},{"bringForward",Objective::BringForward},{"air",Objective::Air},
        {"preserveAttack",Objective::PreserveAttack},{"releaseTails",Objective::ReleaseTails},
        {"reduceAttack",Objective::ReduceAttack},{"easeCompression",Objective::EaseCompression}};
    for (const auto& p : *wire["priorities"].getArray()) {
        const auto name=p["objective"].toString();
        const auto match=std::find_if(std::begin(vocabulary),std::end(vocabulary),[&](auto entry){return name==entry.first;});
        const auto strength=static_cast<float>(p["intensity"]);
        if (match==std::end(vocabulary) || (strength<0 && match->second!=Objective::Air)) return {};
        if (std::any_of(goal.priorities.begin(),goal.priorities.end(),[&](auto prev){return prev.objective==match->second;})) return {};
        goal.priorities.push_back({match->second,strength});
    }
    return goal;
}

var demoResponse(const Turn& t) {
    static const auto ruleset=juce::JSON::parse(String::fromUTF8(demoJson));
    auto answer=ruleset["otherwise"].clone();
    for (const auto& rule : *ruleset["rules"].getArray()) {
        if(rule.hasProperty("source") && rule["source"].toString()!=t.source) continue;
        auto& words=*rule["any"].getArray();
        if(std::any_of(words.begin(),words.end(),[&](const auto& v){return t.prompt.containsIgnoreCase(v.toString());})) {
            answer=rule["answer"].clone();
            break;
        }
    }
    if (auto* g=answer["goal"].getDynamicObject(); g && t.channels==1) g->setProperty("allowMS",false);
    put(answer,"version",1);put(answer,"requestId",t.requestId);put(answer,"provider","local-demo");
    return answer;
}

var post(const String& url,const String& token,const String& body,std::stop_token stop) {
    juce::WebInputStream stream(juce::URL(url).withPOSTData(body),true);
    String headers="Content-Type: application/json\r\n";
    if(token.isNotEmpty()) headers+="Authorization: Bearer "+token+"\r\n";
    stream.withExtraHeaders(headers).withConnectionTimeout(28000).withNumRedirectsToFollow(0);
    std::stop_callback interrupt(stop,[&]{stream.cancel();});
    std::mutex timerMutex;
    std::condition_variable_any timer;
    std::jthread deadline([&](std::stop_token finished){
        std::unique_lock lock(timerMutex);
        timer.wait_for(lock,finished,30s,[]{return false;});
        if(!finished.stop_requested())stream.cancel();
    });
    if(stop.stop_requested() || !stream.connect(nullptr) || stream.getStatusCode()!=200) return {};
    juce::MemoryOutputStream out;
    std::array<char,1024> chunk{};
    while(!stop.stop_requested() && !stream.isExhausted()) {
        const auto n=stream.read(chunk.data(),static_cast<int>(chunk.size()));
        if(n<=0) break;
        if(out.getDataSize()+static_cast<size_t>(n)>16384)return {};
        out.write(chunk.data(),static_cast<size_t>(n));
    }
    return juce::JSON::parse(out.toUTF8());
}

ClientReply failure(const Turn& t, bool cancelled=false) {
    ClientReply out;
    out.requestId=t.requestId;
    out.kind=cancelled?ClientReply::Kind::cancelled:ClientReply::Kind::error;
    out.text=cancelled?String::fromUTF8("Petición cancelada."):String::fromUTF8("No se pudo obtener una respuesta válida del asistente.");
    return out;
}
}

juce::var turnSettings(const Settings& settings,int channels) {
    auto domain=[](const DomainSettings& d) {
        auto bank=object();
        put(bank,"thresholdDb",d.thresholdDb); put(bank,"ratio",d.ratio);
        put(bank,"airDb",d.airDb); put(bank,"makeupDb",d.makeupDb);
        return bank;
    };
    auto out=object();
    put(out,"enabled",settings.enabled); put(out,"mode",channels==1?"mono":settings.midSide?"ms":"lr");
    put(out,"inputTrimDb",settings.inputTrimDb); put(out,"outputGainDb",settings.outputGainDb);
    put(out,"mid",domain(settings.mid)); put(out,"side",domain(settings.side));
    return out;
}

juce::var turnFeatures(const Features& f) {
    auto out=object();
    put(out,"usable",f.usable); put(out,"hasReference",f.hasReference); put(out,"hasReferenceSignal",f.hasReferenceSignal);
    put(out,"peakDb",f.peakDb); put(out,"rmsDb",f.rmsDb); put(out,"phraseSpreadDb",f.phraseSpreadDb);
    put(out,"crestDb",f.crestDb); put(out,"attackDb",f.attackDb); put(out,"decayDb",f.decayDb);
    put(out,"highRatioDb",f.highRatioDb); put(out,"midHighEnergy",f.midHighEnergy); put(out,"sideHighEnergy",f.sideHighEnergy);
    put(out,"referenceContrastDb",f.referenceContrastDb); put(out,"referenceOverlap",f.referenceOverlap); put(out,"monoFoldDb",f.monoFoldDb);
    put(out,"highSpreadDb",f.highSpreadDb); put(out,"midEnergy",f.midEnergy); put(out,"sideEnergy",f.sideEnergy);
    put(out,"sideTailRatioDb",f.sideTailRatioDb); put(out,"highPulseCorrelation",std::clamp(f.highPulseCorrelation,-1.f,1.f));
    put(out,"clippedFraction",f.clippedFraction); put(out,"monitorDb",f.monitorDb);
    put(out,"activeWindows",f.activeWindows); put(out,"phraseWindows",f.phraseWindows);
    return out;
}

juce::var turnResult(const Calibration& calibration) {
    auto out=object();
    const auto outcome=calibration.outcome;
    put(out,"outcome",outcome==Outcome::Candidate?"candidate":outcome==Outcome::Keep?"keep":
                     outcome==Outcome::Clarify?"clarify":"cancelled");
    if(calibration.before.usable)put(out,"before",turnFeatures(calibration.before));
    if(calibration.after.usable)put(out,"after",turnFeatures(calibration.after));
    put(out,"midReductionDb",calibration.midReductionDb);
    put(out,"sideReductionDb",calibration.sideReductionDb);
    return out;
}

Client::Client(bool demoMode) {
    if(demoMode) return;
    url=juce::SystemStats::getEnvironmentVariable("IA2A_TURN_URL","");
    bearer=juce::SystemStats::getEnvironmentVariable("IA2A_SERVICE_TOKEN","");
    usePublishedConnection=url.isEmpty();
}
Client::Client(String endpoint,String token):url(std::move(endpoint)),bearer(std::move(token)) {}

ClientReply Client::request(const Turn& t,std::stop_token stop) const {
    if(stop.stop_requested())return failure(t,true);
    const auto wire=encode(t);
    if(!matches(definitions()["request"],wire) || (!t.previousGoal.isVoid() && !decodeGoal(t.previousGoal,t.source,2)))return failure(t);
    auto body=juce::JSON::toString(wire,true);
    if(body.getNumBytesAsUTF8()>16384)return failure(t);
    auto endpoint=url;
    auto token=bearer;
    if(usePublishedConnection) {
        const auto local=publishedConnection();
        if(!local) {
            auto result=failure(t);
            result.provider="disconnected";
            result.text=String::fromUTF8("Abre el iniciador de Claude y vuelve a enviar tu petición. El fragmento está listo.");
            return result;
        }
        endpoint=local->turnUrl;
        token=local->token;
    }
    auto reply=endpoint.isEmpty()?demoResponse(t):post(endpoint,token,body,stop);
    if(stop.stop_requested())return failure(t,true);
    if(!matches(definitions()["response"],reply) || reply["requestId"].toString()!=t.requestId) {
        auto result=failure(t);
        if(usePublishedConnection) {
            result.provider="disconnected";
            result.text=String::fromUTF8("El servicio no pudo completar la consulta. Comprueba la ventana de Terminal y vuelve a enviar.");
        }
        return result;
    }
    ClientReply out;
    out.requestId=t.requestId; out.provider=reply["provider"].toString(); out.text=reply["text"].toString();
    const auto kind=reply["kind"].toString();
    if(kind=="goal") {
        auto goal=decodeGoal(reply["goal"],t.source,t.channels);
        if(!goal)return failure(t);
        out.kind=ClientReply::Kind::goal; out.goal=std::move(*goal);out.wireGoal=reply["goal"];
    } else out.kind=kind=="keep"?ClientReply::Kind::keep:ClientReply::Kind::question;
    return out;
}

void Client::send(Turn turn,Completion done) {
    std::lock_guard lock(mutex);
    active.request_stop();
    queued=Job{std::move(turn),std::move(done)};
    if(!worker.joinable())worker=std::thread([this]{run();});
    changed.notify_one();
}

void Client::cancel() {
    std::lock_guard lock(mutex);
    queued.reset();
    active.request_stop();
}

void Client::run() {
    std::unique_lock lock(mutex);
    for(;;) {
        changed.wait(lock,[&]{return closing || queued.has_value();});
        if(closing)return;
        auto job=std::move(*queued);queued.reset();
        active=std::stop_source{};
        auto stop=active.get_token();
        lock.unlock();
        auto result=request(job.turn,stop);
        if(job.done && !stop.stop_requested())job.done(std::move(result));
        lock.lock();
    }
}

Client::~Client() {
    {std::lock_guard lock(mutex);closing=true;queued.reset();active.request_stop();}
    changed.notify_one();
    if(worker.joinable())worker.join();
}

}
