#include "DirectRequest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <locale>
#include <regex>
#include <sstream>
#include <string>

namespace ia2a {
namespace {
enum class Unit { Db, Milliseconds, Ratio, Fraction, Hertz };

struct ControlWords {
    Control control;
    Unit unit;
    std::regex words;
};

const auto& controlWords() {
    static const std::array table{
        ControlWords{Control::Threshold,Unit::Db,std::regex(R"(\b(umbral|threshold)\b)")},
        ControlWords{Control::Ratio,Unit::Ratio,std::regex(R"(\b(ratio|relacion de compresion)\b)")},
        ControlWords{Control::Attack,Unit::Milliseconds,std::regex(R"(\b(ataque|attack)\b)")},
        ControlWords{Control::SlowRelease,Unit::Milliseconds,std::regex(R"(\b(release lento|slow release|liberacion lenta)\b)")},
        ControlWords{Control::FastRelease,Unit::Milliseconds,std::regex(R"(\b(release rapido|fast release|liberacion rapida|release|liberacion)\b)")},
        ControlWords{Control::Memory,Unit::Fraction,std::regex(R"(\b(memoria|memory)\b)")},
        ControlWords{Control::Knee,Unit::Db,std::regex(R"(\b(codo|rodilla|knee)\b)")},
        ControlWords{Control::DetectorFrequency,Unit::Hertz,std::regex(R"(\b(hpf|filtro (?:del )?detector|detector frequency|detector hpf)\b)")},
        ControlWords{Control::Air,Unit::Db,std::regex(R"(\bair\b)")},
        ControlWords{Control::MakeUp,Unit::Db,std::regex(R"(\b(make.?up gain|make.?up|ganancia de compensacion|gain de compensacion)\b)")},
        ControlWords{Control::InputTrim,Unit::Db,std::regex(R"(\b(ganancia de entrada|gain de entrada|input gain|input trim|trim de entrada)\b)")},
        ControlWords{Control::OutputGain,Unit::Db,std::regex(R"(\b(ganancia de salida|gain de salida|nivel de salida|output gain|output level)\b)")}
    };
    return table;
}

std::optional<float> quantity(const std::smatch& match,Unit expected) {
    std::istringstream stream(match[1].str());
    stream.imbue(std::locale::classic());
    float value=0;
    if(!(stream>>value) || !stream.eof() || !std::isfinite(value)) return {};
    const auto suffix=match[2].str();
    if(suffix.empty()) return value;
    switch(expected) {
        case Unit::Db:
            if(suffix=="db"||suffix=="dbfs") return value;
            break;
        case Unit::Milliseconds:
            if(suffix=="ms"||suffix.starts_with("milisegundo")||suffix.starts_with("millisecond")) return value;
            if(suffix=="s"||suffix.starts_with("sec")||suffix.starts_with("segundo")) return value*1000;
            break;
        case Unit::Ratio:
            if(suffix.front()==':') return value;
            break;
        case Unit::Fraction:
            if(suffix=="%") return value*.01f;
            break;
        case Unit::Hertz:
            if(suffix=="hz") return value;
            if(suffix=="khz") return value*1000;
            break;
    }
    return {};
}

std::optional<ControlDomain> domainOf(const std::string& text) {
    static const std::regex mainWords(R"(\b(mid|center|centre|centro)\b)");
    static const std::regex sideWords(R"(\b(side|sides|laterales|lado|lados)\b)");
    const bool main=std::regex_search(text,mainWords), side=std::regex_search(text,sideWords);
    if(main&&side) return {};
    return main?ControlDomain::Main:side?ControlDomain::Side:ControlDomain::Both;
}

bool switchesOn(const std::string& text) {
    static const std::regex active(R"(^(activa|habilita|enciende|enable|turn on)\b)");
    return std::regex_search(text,active);
}

std::optional<ControlEdit> parseSwitch(const std::string& clause) {
    static const std::regex routing(R"(^(?:ponlo|pon el(?: compresor)?|cambia|cambialo|usa|use|set it|switch)(?:\s+(?:a|en|to|in))?\s+(?:modo\s+|mode\s+)?(m\s*/\s*s|mid\s*/\s*side|l\s*/\s*r|left\s*/\s*right)$)");
    static const std::regex compressor(R"(^(activa|enciende|habilita|desactiva|apaga|enable|disable|turn on|turn off)\s+(?:(?:el|the)\s+)?(?:compresor|compressor)$)");
    static const std::regex detector(R"(^(activa|enciende|habilita|desactiva|apaga|enable|disable|turn on|turn off)\s+(?:(?:el|the)\s+)?(?:(?:sc|sidechain|detector)\s+)?(?:hpf|filtro (?:del )?detector|filtro paso altos|high[- ]pass filter)(?:\s+(?:(?:del|de|en|on)\s+)?(?:el |the )?(?:mid|side|sides|center|centre|centro|lado|lados|laterales))?$)");
    std::smatch match;
    if(std::regex_match(clause,match,routing))
        return ControlEdit{Control::StereoMode,ControlDomain::Both,ControlChange::Set,match[1].str().front()=='m'?1.f:0.f};
    if(std::regex_match(clause,compressor))
        return ControlEdit{Control::Enabled,ControlDomain::Both,ControlChange::Set,switchesOn(clause)?1.f:0.f};
    if(!std::regex_match(clause,detector)) return {};
    const auto domain=domainOf(clause);
    if(!domain) return {};
    return ControlEdit{Control::DetectorEnabled,*domain,ControlChange::Set,switchesOn(clause)?1.f:0.f};
}

std::optional<ControlEdit> parseEdit(const std::string& clause) {
    static const std::regex decrease(R"(\b(baja|bajame|bajar|bajes|reduce|reducir|disminuye|disminuir|lower|decrease|turn down|less|menos|mas baj[oa]|mas cort[oa]|mas rapid[oa]|shorter|faster)\b)");
    static const std::regex increase(R"(\b(sube|subeme|subir|subas|aumenta|aumentar|incrementa|incrementar|eleva|raise|increase|turn up|mas alt[oa]|mas larg[oa]|mas lent[oa]|higher|longer|slower)\b)");
    static const std::regex absoluteVerb(R"(\b(pon|poner|fija|fijar|lleva|llevar|deja|dejar|set)\b)");
    static const std::regex intent(R"(^(?:(?:por favor|please|puedes|podrias|can you|could you|would you|quiero(?: que)?|quisiera|me gustaria|i want(?: you to)?|i would like)\s+)*(?:baja|bajame|bajar|bajes|reduce|reducir|disminuye|disminuir|lower|decrease|turn down|sube|subeme|subir|subas|aumenta|aumentar|incrementa|incrementar|eleva|raise|increase|turn up|pon|poner|fija|fijar|lleva|llevar|deja|dejar|set|ajusta|adjust|el|la|un|una|the|umbral|threshold|activa|habilita|enciende|enable|turn on|hpf|sc|sidechain)\b)");
    static const std::regex number(R"((?:^|[^a-z0-9_.])([+-]?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+))\s*(:\s*1\b|dbfs\b|db\b|ms\b|milisegundos?\b|milliseconds?\b|segundos?\b|seconds?\b|secs?\b|s\b|khz\b|hz\b|%)?)");
    static const std::regex target(R"((?:\b(?:a|to|at)\s+|=\s*)$)");
    static const std::regex en(R"(\ben\s+$)");
    if(auto edit=parseSwitch(clause)) return edit;
    if(!std::regex_search(clause,intent)) return {};

    const ControlWords* selected=nullptr;
    for(const auto& spec:controlWords()) {
        if(spec.control==Control::FastRelease && selected && selected->control==Control::SlowRelease) continue;
        if(!std::regex_search(clause,spec.words)) continue;
        if(selected) return {};
        selected=&spec;
    }
    if(!selected) return {};

    const bool down=std::regex_search(clause,decrease), up=std::regex_search(clause,increase);
    const bool absolute=std::regex_search(clause,absoluteVerb);
    const auto domain=domainOf(clause);
    if((down&&up)||!domain) return {};
    if(*domain!=ControlDomain::Both && (selected->control==Control::InputTrim||selected->control==Control::OutputGain)) return {};
    if(switchesOn(clause) && selected->control!=Control::DetectorFrequency) return {};

    std::smatch match;
    if(std::regex_search(clause,match,number)) {
        const auto remainder=match.suffix().str();
        if(std::regex_search(remainder,number)) return {};
        const auto value=quantity(match,selected->unit);
        if(!value) return {};
        const auto prefix=clause.substr(0,static_cast<std::size_t>(match.position(1)));
        if(absolute || std::regex_search(prefix,target) || (selected->control==Control::DetectorFrequency && switchesOn(clause)) || (*value<0 && std::regex_search(prefix,en)))
            return ControlEdit{selected->control,*domain,ControlChange::Set,value};
        if((down||up) && *value>0)
            return ControlEdit{selected->control,*domain,down?ControlChange::Decrease:ControlChange::Increase,value};
        return {};
    }

    if(absolute || (!down&&!up)) return {};
    // Bare attack phrasing may refer to what the instrument sounds like rather than the knob.
    static const std::regex attackTime(R"(\b(tiempo de ataque|attack time)\b)");
    if(selected->control==Control::Attack && !std::regex_search(clause,attackTime)) return {};
    return ControlEdit{selected->control,*domain,down?ControlChange::Decrease:ControlChange::Increase,{}};
}
}

std::optional<Goal> directRequest(const juce::String& prompt) {
    static const std::regex punctuation(R"([.!?]+$)");
    static const std::regex decimalComma(R"(([0-9]),([0-9]))");
    static const std::regex separators(R"(\s*[,;]\s*|\.\s+|\s+(?:y|and)\s+)");
    static const std::regex politeRequest(R"(^(puedes|podrias|can you|could you|would you)\b)");
    static const std::regex defer(R"(\b(no|nunca|ni|sin|not|never|dont|don't|without|avoid|evita|evitar|si|if|hipoteticamente|hypothetically|pero|but|aunque|however|salvo|except|cuando|when)\b)");
    static const std::regex compensate(R"(^(?:compensa|compensar|equilibra|equilibrar|iguala|igualar|match|balance|compensate)(?:\s+(?:con|with))?\s+(?:(?:el|la|the)\s+)?(?:gain|ganancia|volumen|nivel|level)(?:\s+(?:de salida|final))?(?:\s+por favor)?$)");
    static const std::regex stronger(R"(^(?:(?:para )?que se note |quiero |i want )?(?:mas (?:la )?compresion|more compression)$)");

    const auto normalized=prompt.toLowerCase().trim()
        .replaceCharacters(juce::String::fromUTF8("áéíóúüñ−–’"),"aeiouun--'")
        .replace(juce::String::fromUTF8("¿"),"");
    auto text=std::regex_replace(normalized.toStdString(),decimalComma,"$1.$2");
    if(std::regex_search(text,defer) || (text.find('?')!=std::string::npos && !std::regex_search(text,politeRequest))) return {};
    Goal goal;
    bool wantsStronger=false;
    for(std::sregex_token_iterator part(text.begin(),text.end(),separators,-1), end; part!=end; ++part) {
        const auto trimmed=juce::String::fromUTF8(part->str().c_str()).trim().toStdString();
        const auto clause=std::regex_replace(trimmed,punctuation,"");
        if(clause.empty() || clause=="por favor" || clause=="please") continue;
        if(std::regex_match(clause,compensate)) {
            goal.compensateGain=true;
        } else if(std::regex_match(clause,stronger)) {
            wantsStronger=true;
        } else if(const auto edit=parseEdit(clause)) {
            auto append=[&](const ControlEdit& next) {
                const auto overlaps=[&](const ControlEdit& previous) {
                    return previous.control==next.control &&
                        (previous.domain==next.domain || previous.domain==ControlDomain::Both || next.domain==ControlDomain::Both);
                };
                if(std::any_of(goal.edits.begin(),goal.edits.end(),overlaps)) return false;
                goal.edits.push_back(next);
                return true;
            };
            if(!append(*edit)) return {};
            if(edit->control==Control::DetectorFrequency && switchesOn(clause) &&
                !append({Control::DetectorEnabled,edit->domain,ControlChange::Set,1})) return {};
        } else {
            return {};
        }
    }
    if(goal.edits.empty() || goal.edits.size()>26) return {};
    if(wantsStronger) {
        const auto strengthens=[](const ControlEdit& edit) {
            return (edit.control==Control::Threshold && edit.change==ControlChange::Decrease)
                || (edit.control==Control::Ratio && edit.change==ControlChange::Increase);
        };
        if(std::none_of(goal.edits.begin(),goal.edits.end(),strengthens)) return {};
    }
    return goal;
}
}
