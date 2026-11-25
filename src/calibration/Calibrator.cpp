#include "calibration/Calibrator.h"
#include "calibration/Tuning.h"
#include "dsp/OpticalPath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>

namespace ia2a {
namespace {
struct Trial {
    Settings settings;
    Features audio;
    float midGr = 0, sideGr = 0;
};
float intensity(const Goal& goal, Objective kind) {
    for(auto p:goal.priorities) if(p.objective==kind) return p.intensity;
    return 0;
}
float balance(const Features& f) {
    return static_cast<float>(10*std::log10(std::max(1.e-12f,f.sideEnergy)/std::max(1.e-12f,f.midEnergy)));
}
std::optional<Trial> render(const Clip& input, const Settings& settings, std::stop_token stop) {
    Clip work=input;
    OpticalPath path;
    const int channels=static_cast<int>(input.track.size());
    path.prepare(input.sampleRate,channels);
    path.setSettings(settings);
    Trial out{settings,{}};
    const auto total=work.track.front().size();
    for(size_t n=0;n<total;) {
        if(stop.stop_requested()) return std::nullopt;
        const int count=static_cast<int>(std::min<size_t>(tuning::blockSize,total-n));
        AudioBlockView block{{work.track[0].data()+n,channels==2?work.track[1].data()+n:nullptr},channels,count};
        GrMeters meter{};
        path.process(block,meter);
        if(n+count>static_cast<size_t>(work.prerollSamples)) {
            out.midGr=std::max(out.midGr,settings.midSide?meter.midDb:meter.reductionDb);
            out.sideGr=std::max(out.sideGr,settings.midSide?meter.sideDb:meter.reductionDb);
        }
        n+=count;
    }
    out.audio=analyse(work);
    return out;
}
struct ControlSpec {
    const char* name;
    const char* unit;
    float minimum, maximum, step;
    float DomainSettings::*domainMember = nullptr;
    float Settings::*globalMember = nullptr;
};
constexpr std::array controlSpecs{
    ControlSpec{"Umbral"," dB",-60.f,0.f,3.f,&DomainSettings::thresholdDb},
    ControlSpec{"Ratio",":1",1.f,20.f,1.f,&DomainSettings::ratio},
    ControlSpec{"Ataque"," ms",.1f,200.f,2.f,&DomainSettings::attackMs},
    ControlSpec{"Recuperación rápida"," ms",10.f,3000.f,20.f,&DomainSettings::fastReleaseMs},
    ControlSpec{"Recuperación lenta"," ms",100.f,15000.f,200.f,&DomainSettings::slowReleaseMs},
    ControlSpec{"Memoria","",0.f,1.f,.1f,&DomainSettings::memory},
    ControlSpec{"Rodilla"," dB",0.f,24.f,2.f,&DomainSettings::kneeDb},
    ControlSpec{"Frecuencia del detector"," Hz",20.f,500.f,20.f,&DomainSettings::hpfHz},
    ControlSpec{"AIR"," dB",0.f,10.f,1.f,&DomainSettings::airDb},
    ControlSpec{"Ganancia de compensación"," dB",-24.f,24.f,1.5f,&DomainSettings::makeupDb},
    ControlSpec{"Ganancia de entrada"," dB",-24.f,24.f,1.5f,nullptr,&Settings::inputTrimDb},
    ControlSpec{"Ganancia de salida"," dB",-24.f,24.f,1.5f,nullptr,&Settings::outputGainDb}
};
static_assert(controlSpecs.size()==static_cast<size_t>(Control::OutputGain)+1);

std::string displayValue(float value) {
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text<<std::fixed<<std::setprecision(2)<<(std::abs(value)<.005f?0.f:value);
    auto number=text.str();
    while(number.back()=='0') number.pop_back();
    if(number.back()=='.') number.pop_back();
    else std::replace(number.begin(),number.end(),'.',',');
    return number;
}

void append(std::string& explanation,std::string clause) {
    if(!explanation.empty()) explanation+=" ";
    explanation+=std::move(clause);
}

void changeControl(float& parameter,const ControlSpec& spec,const ControlEdit& edit,
                   const std::string& domain,std::string& summary) {
    const float before=parameter;
    const float amount=std::abs(edit.value.value_or(spec.step));
    const float requested=edit.change==ControlChange::Set?*edit.value:
        before+(edit.change==ControlChange::Increase?amount:-amount);
    parameter=std::clamp(requested,spec.minimum,spec.maximum);
    const std::string name=spec.name+domain;
    const auto value=displayValue(parameter)+spec.unit;
    const auto limited=parameter!=requested?" (límite del control).":".";
    if(parameter==before)
        append(summary,name+" ya está en "+value+limited);
    else
        append(summary,name+" de "+displayValue(before)+" a "+value+limited);
}

Calibration calibrateObjectives(const Clip&,const Settings& applied,const Settings& seed,const Goal&,
                                std::stop_token stop);

void pinDirectSettings(Settings& trial,const Settings& direct,const Goal& goal,bool stereo) {
    for(const auto& edit:goal.edits) {
        if(edit.control==Control::Enabled) {
            trial.enabled=direct.enabled;
            continue;
        }
        if(edit.control==Control::StereoMode || edit.domain==ControlDomain::Side)
            trial.midSide=direct.midSide;
        if(edit.control==Control::StereoMode) continue;
        if(edit.control==Control::InputTrim || edit.control==Control::OutputGain) {
            const auto member=controlSpecs[static_cast<size_t>(edit.control)].globalMember;
            trial.*member=direct.*member;
            continue;
        }
        const auto assign=[&](DomainSettings& target,const DomainSettings& value) {
            if(edit.control==Control::DetectorEnabled) {
                target.hpfOn=value.hpfOn;
            } else {
                const auto member=controlSpecs[static_cast<size_t>(edit.control)].domainMember;
                target.*member=value.*member;
                if(edit.control==Control::DetectorFrequency) target.hpfOn=value.hpfOn;
            }
        };
        if(edit.domain!=ControlDomain::Side)
            assign(trial.mid,direct.mid);
        if(stereo && edit.domain==ControlDomain::Side)
            assign(trial.side,direct.side);
        if(stereo && trial.midSide && edit.domain==ControlDomain::Both)
            assign(trial.side,direct.midSide?direct.side:direct.mid);
    }
}

Calibration calibrateDirect(const Clip& clip,const Settings& applied,const Settings& seed,
                            const Goal& goal,std::stop_token stop) {
    Calibration result;
    result.settings=seed;
    auto cancel=[&] {
        result.outcome=Outcome::Cancelled;
        result.settings=applied;
        result.explanation.clear();
        return result;
    };
    auto& next=result.settings;
    const bool stereo=clip.track.empty()?seed.midSide:clip.track.size()==2;
    const auto specifies=[&](Control control) {
        return std::any_of(goal.edits.begin(),goal.edits.end(),[control](const ControlEdit& edit){ return edit.control==control; });
    };
    const bool explicitOutput=specifies(Control::OutputGain);
    const bool explicitEnabled=specifies(Control::Enabled);
    const bool explicitMode=specifies(Control::StereoMode);
    const auto validEdit=[](const ControlEdit& edit) {
        return (edit.change!=ControlChange::Set || edit.value) && (!edit.value || std::isfinite(*edit.value));
    };
    const auto toggle=[](const ControlEdit& edit) {
        return edit.change==ControlChange::Set?*edit.value>0.5f:edit.change==ControlChange::Increase;
    };
    // An explicit bypass or stereo mode governs the whole proposal.
    for(const auto& edit:goal.edits) {
        if(!validEdit(edit)) continue;
        if(edit.control==Control::Enabled) {
            next.enabled=toggle(edit);
            append(result.explanation,next.enabled?"Compresor activado.":"Compresor en bypass.");
        }
        if(edit.control==Control::StereoMode) {
            const bool ms=toggle(edit);
            if(ms && !stereo) {
                append(result.explanation,"El modo M/S necesita una pista estéreo.");
            } else {
                next.midSide=ms;
                append(result.explanation,ms?"Modo estéreo M/S.":"Modo estéreo L/R.");
            }
        }
    }
    const auto editDomain=[&](DomainSettings& domain,const ControlEdit& edit,ControlDomain band,
                              const std::string& name) {
        if(edit.control==Control::DetectorEnabled) {
            domain.hpfOn=toggle(edit);
            append(result.explanation,"Filtro del detector"+name+(domain.hpfOn?" activado.":" desactivado."));
            return;
        }
        const auto& spec=controlSpecs[static_cast<size_t>(edit.control)];
        changeControl(domain.*spec.domainMember,spec,edit,name,result.explanation);
        if(edit.control!=Control::DetectorFrequency) return;
        const bool explicitFilter=std::any_of(goal.edits.begin(),goal.edits.end(),[&](const ControlEdit& other) {
            return other.control==Control::DetectorEnabled &&
                   (other.domain==ControlDomain::Both || other.domain==band) && validEdit(other);
        });
        if(!explicitFilter && !domain.hpfOn) {
            domain.hpfOn=true;
            append(result.explanation,"Filtro del detector"+name+" activado.");
        }
    };
    for(const auto& edit:goal.edits) {
        if(stop.stop_requested()) return cancel();
        if(!validEdit(edit) || edit.control==Control::Enabled || edit.control==Control::StereoMode) continue;
        if(edit.control==Control::InputTrim || edit.control==Control::OutputGain) {
            const auto& spec=controlSpecs[static_cast<size_t>(edit.control)];
            changeControl(next.*spec.globalMember,spec,edit,"",result.explanation);
            continue;
        }
        if(edit.domain==ControlDomain::Side) {
            if(!stereo) {
                append(result.explanation,"El canal Side necesita una pista estéreo.");
                continue;
            }
            if(!next.midSide && !explicitMode) {
                next.midSide=true;
                append(result.explanation,"Modo M/S activado para ajustar los lados.");
            } else if(!next.midSide) {
                append(result.explanation,"El ajuste Side queda preparado para M/S.");
            }
        }
        if(edit.domain!=ControlDomain::Side)
            editDomain(next.mid,edit,ControlDomain::Main,next.midSide&&stereo?" Mid":"");
        if(stereo && ((next.midSide && edit.domain!=ControlDomain::Main) || edit.domain==ControlDomain::Side))
            editDomain(next.side,edit,ControlDomain::Side," Side");
    }
    if(!explicitEnabled && (next.inputTrimDb!=seed.inputTrimDb || next.mid!=seed.mid ||
                            (next.midSide && next.side!=seed.side)))
        next.enabled=true;
    const bool measureGain=goal.compensateGain && !explicitOutput;
    if(analyse(clip).usable) {
        if(!goal.priorities.empty()) {
            const auto musical=calibrateObjectives(clip,applied,next,goal,stop);
            if(musical.outcome==Outcome::Cancelled) return cancel();
            result.renders+=musical.renders;
            result.triedLR=musical.triedLR;
            result.triedMS=musical.triedMS;
            if(musical.outcome==Outcome::Candidate) {
                next=musical.settings;
                append(result.explanation,musical.explanation);
            } else if(result.explanation.empty()) {
                result.explanation=musical.explanation;
            }
        }
        auto measure=[&](const Settings& settings) { ++result.renders; return render(clip,settings,stop); };
        const auto before=measure(applied);
        if(!before) return cancel();
        const bool soundChange=!goal.edits.empty() || !goal.priorities.empty();
        const auto initial=measureGain && soundChange && applied!=seed?measure(seed):before;
        if(!initial) return cancel();
        auto audition=measure(next);
        if(!audition) return cancel();

        if(measureGain && initial->audio.usable && audition->audio.usable) {
            const float prior=next.outputGainDb;
            const float desired=initial->audio.monitorDb-audition->audio.monitorDb;
            const float room=tuning::ceilingDb-.01f-audition->audio.peakDb;
            next.outputGainDb=std::clamp(prior+std::min(desired,room),-24.f,24.f);
            if(std::abs(next.outputGainDb-prior)<.015f) next.outputGainDb=prior;
            if(next.outputGainDb!=prior) {
                audition=measure(next);
                if(!audition) return cancel();
                append(result.explanation,"Ganancia de salida de "+displayValue(prior)+" a "+
                    displayValue(next.outputGainDb)+" dB para compensar el nivel.");
            }
            if(desired>room+.02f || prior+desired>24.f || prior+desired< -24.f)
                append(result.explanation,"La compensación llega al margen disponible.");
            else if(next.outputGainDb==prior && result.explanation.empty())
                append(result.explanation,"El nivel medido ya coincide con el de referencia.");
        } else if(measureGain) {
            append(result.explanation,"Para compensar el nivel hace falta un fragmento con señal.");
        }
        result.before=before->audio;
        result.after=audition->audio;
        result.midReductionDb=audition->midGr;
        result.sideReductionDb=audition->sideGr;
        if(result.before.usable && result.after.usable)
            result.matchGainDb=result.before.monitorDb-result.after.monitorDb;
        if(result.after.peakDb>=0)
            append(result.explanation,"La salida alcanza "+displayValue(result.after.peakDb)+" dBFS en este fragmento.");
    } else if(measureGain) {
        append(result.explanation,"Para compensar el nivel hace falta un fragmento con señal.");
    }

    if(stop.stop_requested()) return cancel();
    if(next!=seed)
        result.outcome=Outcome::Candidate;
    return result;
}

float improvement(float change,float target,float scale) {
    return (std::abs(target)-std::abs(target-change))/scale;
}
float peakReduction(const Trial& trial) {
    return std::max(trial.midGr,trial.sideGr);
}
float bassChange(const Trial& trial,const Trial& seed,const Features& input) {
    const float lowShare=trial.audio.lowRatioDb-seed.audio.lowRatioDb;
    const float reduction=peakReduction(seed)-peakReduction(trial);
    const float detectorRelief=input.lowRatioDb>-12.f && trial.settings.mid.hpfOn
        && (!seed.settings.mid.hpfOn || trial.settings.mid.hpfHz>seed.settings.mid.hpfHz)
        ? std::max(0.f,reduction) : 0.f;
    return lowShare+.25f*detectorRelief;
}
std::optional<Priority> primaryChange(const Goal& goal) {
    for(auto priority:goal.priorities)
        if(priority.intensity!=0.f && priority.objective!=Objective::PreserveAttack)
            return priority;
    return std::nullopt;
}

float directionalChange(Priority priority,const Trial& trial,const Trial& seed,const Features& input) {
    const auto& f=trial.audio;
    const auto& base=seed.audio;
    switch(priority.objective) {
        case Objective::Stabilise:
            return (base.phraseSpreadDb-f.phraseSpreadDb)/3.f;
        case Objective::BringForward:
            return base.referenceOverlap>.2f && f.referenceOverlap>.2f
                ? (f.referenceContrastDb-base.referenceContrastDb)/2.f : (f.monitorDb-base.monitorDb)/2.f;
        case Objective::Air:
            return std::copysign(1.f,priority.intensity)*(trial.settings.mid.airDb-seed.settings.mid.airDb)/3.f;
        case Objective::ReleaseTails:
            if(input.sideHighEnergy>input.midHighEnergy && input.sideEnergy>1.e-10f)
                return (.65f*(balance(f)-balance(base))+.35f*(f.sideTailRatioDb-base.sideTailRatioDb))/2.f;
            return (base.highPulseCorrelation-f.highPulseCorrelation)/.15f;
        case Objective::ReduceAttack:
            return .65f*(base.crestDb-f.crestDb)/2.f+.35f*(base.attackDb-f.attackDb)/1.5f;
        case Objective::EaseCompression:
            return (peakReduction(seed)-peakReduction(trial))/2.f;
        case Objective::BassWeight:
            return bassChange(trial,seed,input)/2.f;
        case Objective::PreserveAttack:
            return 0.f;
    }
    return 0.f;
}

float evaluate(const Trial& trial,const Trial& seed,const Features& input,const Goal& goal) {
    const auto& f=trial.audio;
    const auto& base=seed.audio;
    if(!f.usable || f.peakDb>tuning::ceilingDb || f.clippedFraction>tuning::maxClipFraction) return -INFINITY;
    const float stereoChange=balance(f)-balance(base);
    if(input.sideEnergy>1.e-10 && (std::abs(stereoChange)>tuning::maxStereoBalanceChangeDb || f.monoFoldDb<base.monoFoldDb-2.5f)) return -INFINITY;
    const auto primary=primaryChange(goal);
    const bool desiredAir=primary && primary->objective==Objective::Air;
    if(f.crestDb<base.crestDb-6 ||
       (!desiredAir && base.highRatioDb>=tuning::brightRatioDb && f.highRatioDb>base.highRatioDb+.7f)) return -INFINITY;

    float score=0,weight=1;
    for(auto priority:goal.priorities) {
        const float amount=priority.intensity;
        float contribution=0;
        switch(priority.objective) {
            case Objective::Stabilise: {
                float target=std::min(std::max(0.f,base.phraseSpreadDb-2),6*amount);
                contribution=improvement(base.phraseSpreadDb-f.phraseSpreadDb,target,3);
                break;
            }
            case Objective::BringForward: {
                float target=3*amount;
                if(base.referenceOverlap>.2f) target=std::clamp(-base.referenceContrastDb,0.f,4*amount);
                float change=f.monitorDb-base.monitorDb;
                if(base.referenceOverlap>.2f && f.referenceOverlap>.2f) change=f.referenceContrastDb-base.referenceContrastDb;
                contribution=improvement(change,target,2);
                break;
            }
            case Objective::Air: {
                const float target=!desiredAir && amount>0 && input.highRatioDb>=tuning::brightRatioDb ? 0 : 3*amount;
                const float settingChange=trial.settings.mid.airDb-seed.settings.mid.airDb;
                contribution=.55f*improvement(settingChange,target,3)+.45f*improvement(f.highRatioDb-base.highRatioDb,target*.4f,1.2f);
                break;
            }
            case Objective::ReleaseTails: {
                if(input.sideHighEnergy>input.midHighEnergy && input.sideEnergy>1.e-10f) {
                    contribution=.65f*improvement(stereoChange,3*amount,2)+
                                 .35f*improvement(f.sideTailRatioDb-base.sideTailRatioDb,2*amount,2);
                } else {
                    contribution=improvement(base.highPulseCorrelation-f.highPulseCorrelation,.15f*amount,.15f);
                }
                break;
            }
            case Objective::PreserveAttack:
                contribution=-amount*(std::max(0.f,base.attackDb-f.attackDb-std::max(.4f,.12f*base.attackDb))/1.5f+std::max(0.f,base.crestDb-f.crestDb-2.f)*.4f);
                break;
            case Objective::ReduceAttack: {
                const float crestChange=base.crestDb-f.crestDb;
                const float attackChange=base.attackDb-f.attackDb;
                contribution=.65f*improvement(crestChange,4.f*amount,2.f)
                             +.35f*improvement(attackChange,2.f*amount,1.5f);
                break;
            }
            case Objective::EaseCompression: {
                const float priorReduction=peakReduction(seed);
                const float target=priorReduction*amount;
                contribution=improvement(priorReduction-peakReduction(trial),target,2.f);
                break;
            }
            case Objective::BassWeight:
                contribution=improvement(bassChange(trial,seed,input),3.f*amount,2.f);
                break;
        }
        score+=weight*contribution;
        weight*=.78f;
    }
    return score;
}
Settings makeTrial(const Trial& seed,const Features& input,const Goal& goal,bool ms,float strength,int variant) {
    Settings next=seed.settings;
    auto& mid=next.mid;
    const float stable=intensity(goal,Objective::Stabilise),tails=intensity(goal,Objective::ReleaseTails);
    const float air=intensity(goal,Objective::Air),forward=intensity(goal,Objective::BringForward);
    const float tame=intensity(goal,Objective::ReduceAttack),ease=intensity(goal,Objective::EaseCompression);
    next.enabled=true; next.midSide=ms;
    if(!seed.settings.enabled) { mid.ratio=1; next.side.ratio=1; }
    if(stable>0 || tails>0) {
        const float drive=std::max(stable,tails)*strength;
        mid.thresholdDb=std::clamp(input.rmsDb+input.crestDb*(tails>0?.55f:.2f)-6*drive,-60.f,0.f);
        mid.ratio=1.5f+5.f*drive;
        mid.attackMs=(tails>0?10.f:4.f)+(variant?8.f:0.f);
        mid.fastReleaseMs=(tails>0?85.f:55.f)+variant*40;
        mid.slowReleaseMs=380.f+variant*320;
        mid.memory=.20f+variant*.18f; mid.kneeDb=3.f+variant*2;
        mid.hpfOn=variant && tails>0; mid.hpfHz=65.f;
        if(ms) {
            next.side=mid;
            next.side.thresholdDb=std::min(0.f,mid.thresholdDb+10);
            next.side.ratio=1.f+.3f*drive;
            next.side.airDb=seed.settings.side.airDb;
            next.side.makeupDb=seed.settings.side.makeupDb;
        }
    }
    if(tame>0) {
        const float drive=tame*strength;
        mid.thresholdDb=std::clamp(input.rmsDb+.38f*input.crestDb-7.f*drive,-60.f,0.f);
        mid.ratio=1.5f+6.f*drive;
        mid.attackMs=variant?2.4f:.15f;
        mid.fastReleaseMs=variant?80.f:35.f;
        mid.slowReleaseMs=variant?380.f:150.f;
        mid.memory=.18f;
        mid.kneeDb=3.f;
    }
    if(ease>0 && seed.settings.enabled) {
        const auto soften=[=](DomainSettings& domain) {
            const float amount=ease*strength;
            domain.ratio=1.f+(domain.ratio-1.f)*(1.f-amount);
            if(variant) domain.thresholdDb=std::min(0.f,domain.thresholdDb+8.f*amount);
        };
        soften(mid);
        if(ms) soften(next.side);
    }
    const float bass=intensity(goal,Objective::BassWeight);
    if(bass>0.f) {
        // Filter only the detector. When starting dry, leave room for upper-band peaks to trigger it.
        if(!seed.settings.enabled && stable==0 && tails==0 && tame==0) {
            mid.ratio=1.8f+2.6f*strength;
            mid.thresholdDb=std::clamp(input.rmsDb-8.f*strength,-60.f,0.f);
            mid.attackMs=variant?12.f:4.f;
            mid.fastReleaseMs=variant?100.f:45.f;
        }
        mid.hpfOn=true;
        const float priorCutoff=seed.settings.mid.hpfOn?seed.settings.mid.hpfHz:45.f;
        const float range=variant?235.f:125.f;
        mid.hpfHz=std::clamp(priorCutoff+range*strength*bass,20.f,500.f);
    }
    const auto primary=primaryChange(goal);
    const bool desiredAir=primary && primary->objective==Objective::Air;
    if(air!=0 && (desiredAir || air<0 || input.highRatioDb<tuning::brightRatioDb))
        mid.airDb=std::clamp(seed.settings.mid.airDb+3*air*strength,0.f,10.f);
    if(forward>0) {
        float gain=(stable>0?3.f:0.f)*strength+forward*(variant?5.f:2.5f);
        if(input.referenceOverlap>.2f && input.referenceContrastDb>0) gain*=.5f;
        next.outputGainDb=std::clamp(seed.settings.outputGainDb+gain,-24.f,24.f);
    }
    return next;
}
Calibration calibrateObjectives(const Clip& clip,const Settings& applied,const Settings& seed,const Goal& goal,
                                std::stop_token stop) {
    Calibration result;
    result.settings=applied;
    auto cancel=[&] { result.outcome=Outcome::Cancelled; result.settings=applied; result.explanation.clear(); return result; };
    if(stop.stop_requested()) return cancel();
    const auto input=analyse(clip);
    if(!input.usable) {
        result.outcome=Outcome::Clarify;
        result.explanation="Necesito escuchar un fragmento con más señal.";
        return result;
    }
    auto measure=[&](const Settings& s) { ++result.renders; return render(clip,s,stop); };
    auto before=measure(applied);
    if(!before) return cancel();
    auto initial=measure(seed);
    if(!initial) return cancel();
    result.before=before->audio; result.after=before->audio;
    const auto primary=primaryChange(goal);
    result.explanation="Las variantes probadas no mueven el sonido en la dirección pedida con el margen de salida disponible.";
    if(!primary) {
        result.explanation="El ajuste de partida ya conserva el sonido.";
        return result;
    }
    const bool tails=intensity(goal,Objective::ReleaseTails)>0;
    const bool tame=intensity(goal,Objective::ReduceAttack)>0;
    const bool ease=intensity(goal,Objective::EaseCompression)>0;
    const bool bass=intensity(goal,Objective::BassWeight)>0;
    const bool keepMode=(ease || bass) && !tails && !tame && intensity(goal,Objective::Stabilise)==0;
    const bool stereo=clip.track.size()==2;
    const bool seedMode=stereo && seed.midSide;
    const bool explicitOutput=std::any_of(goal.edits.begin(),goal.edits.end(),
        [](const ControlEdit& edit){return edit.control==Control::OutputGain;});
    const bool lockedMode=std::any_of(goal.edits.begin(),goal.edits.end(),
        [](const ControlEdit& edit){return edit.control==Control::StereoMode || edit.domain==ControlDomain::Side;});
    float best=-INFINITY;
    for(bool ms:{false,true}) {
        if((keepMode || lockedMode) && ms!=seedMode) continue;
        if((ms && (!goal.allowMS || clip.track.size()!=2)) || (!ms && !goal.allowLR)) continue;
        if(ms) result.triedMS=true; else result.triedLR=true;
        for(float strength:{.45f,.78f,1.f}) for(int variant:{0,1}) {
            auto settings=makeTrial(*initial,input,goal,ms,strength,variant);
            pinDirectSettings(settings,seed,goal,stereo);
            auto trial=measure(settings);
            if(!trial) return cancel();
            if(ms && input.sideEnergy<1.e-10f) continue;
            if(!explicitOutput && trial->audio.peakDb>tuning::ceilingDb && settings.outputGainDb>-24.f) {
                settings.outputGainDb=std::max(-24.f,
                    settings.outputGainDb+tuning::ceilingDb-.01f-trial->audio.peakDb);
                trial=measure(settings);
                if(!trial) return cancel();
            }
            const float direction=directionalChange(*primary,*trial,*initial,input);
            const auto lowChange=trial->audio.lowRatioDb-initial->audio.lowRatioDb;
            const bool bassRequest=primary->objective==Objective::BassWeight;
            const bool detectorChanged=settings.mid.hpfOn &&
                (!initial->settings.mid.hpfOn || settings.mid.hpfHz!=initial->settings.mid.hpfHz);
            const bool credibleBass=bassRequest && detectorChanged && input.lowRatioDb>-12.f
                && std::max(peakReduction(*trial),peakReduction(*initial))>.4f;
            if((bassRequest && lowChange<-.35f) || (direction<=.015f && !credibleBass)) continue;
            const float score=evaluate(*trial,*initial,input,goal);
            if(score<=best) continue;
            best=score; result.outcome=Outcome::Candidate; result.settings=settings;
            result.after=trial->audio; result.midReductionDb=trial->midGr; result.sideReductionDb=trial->sideGr;
            result.matchGainDb=result.before.monitorDb-result.after.monitorDb;
        }
    }
    if(stop.stop_requested()) return cancel();
    if(result.outcome==Outcome::Candidate) {
        if(bass && result.settings.mid.hpfOn &&
           (!seed.mid.hpfOn || result.settings.mid.hpfHz!=seed.mid.hpfHz)) {
            result.explanation="Filtro del detector a "+displayValue(result.settings.mid.hpfHz)+
                " Hz para que los graves disparen menos la compresión.";
            if(result.after.lowRatioDb-initial->audio.lowRatioDb<.2f)
                append(result.explanation,"El cambio de peso medido es pequeño.");
        } else if(tails && result.settings.midSide) result.explanation="La compresión sujeta más el centro que los lados. Escucha la cola del plato después del golpe.";
        else if(ease && peakReduction(*initial)>std::max(result.midReductionDb,result.sideReductionDb)+.15f)
            result.explanation="He suavizado la compresión del ajuste anterior. Compara cómo se mueven los golpes y sus colas.";
        else if(tame && result.after.crestDb+.25f<initial->audio.crestDb)
            result.explanation="Los picos destacan menos sobre el cuerpo de la señal. Compara el inicio de los golpes a volumen igualado.";
        else if(tame && result.after.attackDb+.25f<initial->audio.attackDb)
            result.explanation="La subida de nivel es más suave en los ataques. Compara los golpes a volumen igualado.";
        else if(intensity(goal,Objective::Air)<0) result.explanation="He reducido el AIR de la propuesta anterior. Compara el brillo de las consonantes.";
        else if(result.after.phraseSpreadDb+.5f<result.before.phraseSpreadDb) result.explanation="Las frases suaves quedan más cerca de las fuertes. Compara si la voz conserva expresión en la mezcla.";
        else result.explanation="Escucha la propuesta primero a volumen igualado y después a su nivel final.";
    } else if(ease && peakReduction(*initial)<.15f) {
        result.explanation="En este fragmento el ajuste anterior apenas reduce ganancia.";
    } else if(bass) {
        result.explanation="El filtro del detector apenas cambia el peso del grave en este fragmento. Puedes indicar una frecuencia concreta al detector.";
    } else if(tails) {
        result.outcome=Outcome::Clarify;
        result.explanation=goal.source==Source::DrumBus
            ?"¿Te molesta el brillo del plato o que suba y baje con cada golpe?"
            :"¿Quieres oír más la cola o evitar que cambie de nivel con el ataque?";
    }
    return result;
}
}

Calibration calibrate(const Clip& clip,const Settings& applied,const Settings& seed,const Goal& goal,std::stop_token stop) {
    if(!goal.edits.empty() || goal.compensateGain) return calibrateDirect(clip,applied,seed,goal,stop);
    return calibrateObjectives(clip,applied,seed,goal,stop);
}
}
