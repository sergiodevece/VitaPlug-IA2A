#include "calibration/Calibrator.h"
#include "calibration/Tuning.h"
#include "dsp/OpticalPath.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

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
float improvement(float change,float target,float scale) {
    return (std::abs(target)-std::abs(target-change))/scale;
}
float evaluate(const Trial& trial,const Trial& seed,const Features& input,const Goal& goal) {
    const auto& f=trial.audio;
    const auto& base=seed.audio;
    if(!f.usable || f.peakDb>tuning::ceilingDb || f.clippedFraction>tuning::maxClipFraction) return -INFINITY;
    const float stereoChange=balance(f)-balance(base);
    if(input.sideEnergy>1.e-10 && (std::abs(stereoChange)>tuning::maxStereoBalanceChangeDb || f.monoFoldDb<base.monoFoldDb-2.5f)) return -INFINITY;
    if(f.crestDb<base.crestDb-6 || (base.highRatioDb>=tuning::brightRatioDb && f.highRatioDb>base.highRatioDb+.7f)) return -INFINITY;

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
                const float target=amount>0 && input.highRatioDb>=tuning::brightRatioDb ? 0 : 3*amount;
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
    if(air!=0 && (air<0 || input.highRatioDb<tuning::brightRatioDb))
        mid.airDb=std::clamp(seed.settings.mid.airDb+3*air*strength,0.f,10.f);
    if(forward>0) {
        float gain=(stable>0?3.f:0.f)*strength+forward*(variant?5.f:2.5f);
        if(input.referenceOverlap>.2f && input.referenceContrastDb>0) gain*=.5f;
        next.outputGainDb=std::clamp(seed.settings.outputGainDb+gain,-24.f,24.f);
    }
    return next;
}
}

Calibration calibrate(const Clip& clip,const Settings& applied,const Settings& seed,const Goal& goal,std::stop_token stop) {
    Calibration result;
    result.settings=applied;
    auto cancel=[&] { result.outcome=Outcome::Cancelled; result.settings=applied; result.explanation.clear(); return result; };
    if(stop.stop_requested()) return cancel();
    const auto input=analyse(clip);
    if(!input.usable || input.clippedFraction>tuning::maxClipFraction) {
        result.outcome=Outcome::Clarify;
        result.explanation=input.usable?"La toma contiene picos recortados. ¿Hay otra toma con más margen?":"Necesito escuchar un fragmento con más señal.";
        return result;
    }
    auto measure=[&](const Settings& s) { ++result.renders; return render(clip,s,stop); };
    auto before=measure(applied);
    if(!before) return cancel();
    auto initial=measure(seed);
    if(!initial) return cancel();
    result.before=before->audio; result.after=before->audio;
    result.explanation="El ajuste actual ya se acerca al resultado pedido.";
    if(goal.priorities.empty()) return result;
    const bool tails=intensity(goal,Objective::ReleaseTails)>0;
    float best=tuning::minimumScore;
    for(bool ms:{false,true}) {
        if((ms && (!goal.allowMS || clip.track.size()!=2)) || (!ms && !goal.allowLR)) continue;
        if(ms) result.triedMS=true; else result.triedLR=true;
        for(float strength:{.45f,.78f,1.f}) for(int variant:{0,1}) {
            auto settings=makeTrial(*initial,input,goal,ms,strength,variant);
            auto trial=measure(settings);
            if(!trial) return cancel();
            if(ms && input.sideEnergy<1.e-10f) continue;
            float score=evaluate(*trial,*initial,input,goal);
            if(score<=best) continue;
            best=score; result.outcome=Outcome::Candidate; result.settings=settings;
            result.after=trial->audio; result.midReductionDb=trial->midGr; result.sideReductionDb=trial->sideGr;
            result.matchGainDb=result.before.monitorDb-result.after.monitorDb;
        }
    }
    if(stop.stop_requested()) return cancel();
    if(result.outcome==Outcome::Candidate) {
        if(tails && result.settings.midSide) result.explanation="La compresión sujeta más el centro que los lados. Escucha la cola del plato después del golpe.";
        else if(intensity(goal,Objective::Air)<0) result.explanation="He reducido el AIR de la propuesta anterior. Compara el brillo de las consonantes.";
        else if(result.after.phraseSpreadDb+.5f<result.before.phraseSpreadDb) result.explanation="Las frases suaves quedan más cerca de las fuertes. Compara si la voz conserva expresión en la mezcla.";
        else result.explanation="Escucha la propuesta primero a volumen igualado y después a su nivel final.";
    } else if(tails) {
        result.outcome=Outcome::Clarify;
        result.explanation="¿Te molesta el brillo del plato o que suba y baje con cada golpe?";
    }
    return result;
}
}
