#include "calibration/Calibrator.h"
#include "tests/fixtures/Signals.h"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char* message){ if(!ok) throw std::runtime_error(message); }
int main() {
    using namespace ia2a;
    auto start=Settings::transparent();
    Goal foreground{Source::Voice,{{Objective::Stabilise,1},{Objective::BringForward,.8f},{Objective::Air,.7f}}};
    auto track=fixtures::voice();
    auto voice=calibrate(track,start,start,foreground);
    std::cout<<"Voz: "<<voice.explanation<<" "<<voice.before.phraseSpreadDb<<" -> "<<voice.after.phraseSpreadDb<<" dB\n";
    check(voice.outcome==Outcome::Candidate && voice.after.phraseSpreadDb<voice.before.phraseSpreadDb-.5f,"voice stability");
    check(voice.settings.mid.airDb>0 && voice.after.peakDb<=-1 && !voice.triedMS,"headroom and mono");
    auto less=calibrate(track,start,voice.settings,Goal{Source::Voice,{{Objective::Air,-1}}});
    check(less.outcome==Outcome::Candidate && less.settings.mid.airDb<voice.settings.mid.airDb,"refinement from pending settings");
    Goal drums{Source::DrumBus,{{Objective::ReleaseTails,1},{Objective::PreserveAttack,.8f}}};
    auto kit=calibrate(fixtures::drums(true),start,start,drums);
    std::cout<<"Batería: "<<kit.explanation<<" GR "<<kit.midReductionDb<<" / "<<kit.sideReductionDb<<"\n";
    check(kit.triedLR && kit.triedMS && kit.outcome==Outcome::Candidate && kit.settings.midSide && kit.midReductionDb>kit.sideReductionDb,"centre and sides");
    const auto drumsClip=fixtures::drums(true);
    auto spiky=fixtures::voice(.15f,.15f);
    for(size_t n=0;n<spiky.track[0].size();++n) {
        const double seconds=n/spiky.sampleRate;
        const double hit=std::fmod(seconds,.3);
        const double envelope=hit<.04?std::pow(std::sin(fixtures::tau*hit/.08),2):0;
        spiky.track[0][n]+=.62f*envelope*std::sin(fixtures::tau*290*seconds);
    }
    Goal tameAttack{Source::Other,{{Objective::ReduceAttack,.85f}},true,false};
    const auto softened=calibrate(spiky,start,start,tameAttack);
    check(softened.outcome==Outcome::Candidate,"transient request creates a candidate");
    check(softened.after.crestDb<softened.before.crestDb-.65f,"compression tames peak prominence");
    check(softened.after.peakDb<=-1,"transient candidate stays within headroom");
    std::cout<<"Ataques: cresta "<<softened.before.crestDb<<" -> "<<softened.after.crestDb<<" dB\n";

    auto coloredKit=kit.settings;
    coloredKit.mid.airDb=.7f;
    coloredKit.side.airDb=.3f;
    coloredKit.outputGainDb=-1.f;
    Goal ease{Source::DrumBus,{{Objective::EaseCompression,.5f}}};
    const auto relaxed=calibrate(drumsClip,start,coloredKit,ease);
    check(relaxed.outcome==Outcome::Candidate,"refinement acts on unconfirmed candidate");
    check(relaxed.midReductionDb<kit.midReductionDb-.5f,"refinement eases measured gain reduction");
    check(relaxed.settings.midSide==coloredKit.midSide && !relaxed.triedLR,"ease keeps stereo routing");
    check(relaxed.settings.mid.airDb==coloredKit.mid.airDb && relaxed.settings.side.airDb==coloredKit.side.airDb &&
          relaxed.settings.outputGainDb==coloredKit.outputGainDb &&
          relaxed.settings.mid.attackMs==coloredKit.mid.attackMs &&
          relaxed.settings.mid.fastReleaseMs==coloredKit.mid.fastReleaseMs,
          "ease keeps tone, output and timing");
    const auto easedApplied=calibrate(drumsClip,coloredKit,coloredKit,ease);
    check(easedApplied.outcome==Outcome::Candidate && easedApplied.settings==relaxed.settings,
          "ease also refines the confirmed setting");
    check(calibrate(drumsClip,start,start,ease).outcome==Outcome::Keep,"ease keeps a dry signal dry");
    std::cout<<"Refinamiento: reducción Mid "<<kit.midReductionDb<<" -> "<<relaxed.midReductionDb<<" dB\n";
    // An explicit refinement follows the user's controls even when the previous setting scores well.
    Goal lowerThreshold{Source::DrumBus};
    lowerThreshold.edits.push_back({Control::Threshold,ControlDomain::Both,ControlChange::Decrease,{}});
    lowerThreshold.compensateGain=true;
    auto directSeed=coloredKit;
    directSeed.mid.thresholdDb=-22.f;
    directSeed.side.thresholdDb=-17.f;
    const auto lower=calibrate(drumsClip,start,directSeed,lowerThreshold);
    check(lower.outcome==Outcome::Candidate && lower.settings.mid.thresholdDb<directSeed.mid.thresholdDb &&
          lower.settings.side.thresholdDb<directSeed.side.thresholdDb,"lower both thresholds from the pending proposal");
    check(lower.settings.midSide && lower.settings.mid.attackMs==directSeed.mid.attackMs &&
          lower.settings.side.airDb==directSeed.side.airDb,"direct refinement keeps mode and untouched controls");
    check(lower.settings.outputGainDb>directSeed.outputGainDb && lower.after.peakDb<=-1.0f,
          "compensation becomes an actual bounded output gain");
    check(std::abs(lower.matchGainDb-(lower.before.monitorDb-lower.after.monitorDb))<0.01f,
          "A/B matches the applied setting separately from the proposed output gain");
    check(lower.explanation.find("Umbral")!=std::string::npos && lower.explanation.find("Ganancia")!=std::string::npos,
          "preview reports the controls that moved");

    const auto unmeasured=calibrate(Clip{},start,directSeed,lowerThreshold);
    check(unmeasured.outcome==Outcome::Candidate && unmeasured.settings.mid.thresholdDb<directSeed.mid.thresholdDb &&
          unmeasured.settings.outputGainDb==directSeed.outputGainDb && unmeasured.matchGainDb==0.f,
          "a manual edit without a capture still prepares a proposal");
    check(unmeasured.explanation.find("hace falta un fragmento")!=std::string::npos,
          "explain the remaining measurement only");

    lowerThreshold.edits[0].value=1000.f;
    lowerThreshold.compensateGain=false;
    const auto floor=calibrate(drumsClip,start,directSeed,lowerThreshold);
    check(floor.outcome==Outcome::Candidate && floor.settings.mid.thresholdDb==-60 && floor.settings.side.thresholdDb==-60,
          "threshold stops at the DSP minimum");
    const auto atFloor=calibrate(drumsClip,start,floor.settings,lowerThreshold);
    check(atFloor.outcome==Outcome::Keep && atFloor.explanation.find("límite")!=std::string::npos,
          "report the floor when a further reduction is impossible");

    Goal directGain{Source::Voice};
    directGain.edits.push_back({Control::OutputGain,ControlDomain::Both,ControlChange::Set,12.f});
    directGain.compensateGain=true;
    const auto loud=calibrate(track,start,start,directGain);
    check(loud.outcome==Outcome::Candidate && loud.settings.outputGainDb==12.f && !loud.settings.enabled,
          "explicit output level is exact and leaves the compressor bypassed");
    check(loud.after.peakDb>0 && loud.explanation.find("dBFS")!=std::string::npos,
          "explicit level above full scale remains auditionable and carries a peak note");

    Goal routing{Source::DrumBus};
    routing.edits = {
        {Control::StereoMode,ControlDomain::Both,ControlChange::Set,1.f},
        {Control::DetectorFrequency,ControlDomain::Side,ControlChange::Set,150.f},
        {Control::DetectorEnabled,ControlDomain::Side,ControlChange::Set,0.f},
        {Control::Enabled,ControlDomain::Both,ControlChange::Set,0.f}
    };
    const auto routed=calibrate(drumsClip,start,start,routing);
    check(routed.outcome==Outcome::Candidate && routed.settings.midSide && !routed.settings.enabled &&
          routed.settings.side.hpfHz==150.f && !routed.settings.side.hpfOn && !routed.settings.mid.hpfOn,
          "explicit mode, bypass and detector switch govern the complete edit");
    const auto monoRouting=calibrate(track,start,start,routing);
    check(!monoRouting.settings.midSide && monoRouting.explanation.find("pista estéreo")!=std::string::npos,
          "explain the stereo dependency of an M/S command");

    auto detectorSeed=start;
    detectorSeed.enabled=true;
    detectorSeed.mid.thresholdDb=-29.f;
    detectorSeed.mid.ratio=5.f;
    detectorSeed.mid.attackMs=3.f;
    const Goal weight{Source::DrumBus,{{Objective::BassWeight,1.f}},true,false};
    const auto heavier=calibrate(drumsClip,detectorSeed,detectorSeed,weight);
    check(heavier.outcome==Outcome::Candidate && heavier.settings.mid.hpfOn &&
          heavier.settings.mid.thresholdDb==detectorSeed.mid.thresholdDb &&
          heavier.settings.mid.ratio==detectorSeed.mid.ratio,
          "a more-bass request explores the real detector filter without rewriting the existing compression");
    check(heavier.after.lowRatioDb>heavier.before.lowRatioDb || heavier.midReductionDb>0.f,
          "bass request comes from a real audio trial");
    std::cout<<"Graves: proporción "<<heavier.before.lowRatioDb<<" -> "<<heavier.after.lowRatioDb<<" dB\n";
    auto mixed=weight;
    mixed.edits = {{Control::Threshold,ControlDomain::Both,ControlChange::Set,-31.f}};
    mixed.compensateGain=true;
    const auto mixedBass=calibrate(drumsClip,detectorSeed,detectorSeed,mixed);
    check(mixedBass.outcome==Outcome::Candidate && mixedBass.settings.mid.thresholdDb==-31.f &&
          mixedBass.settings.mid.hpfOn && mixedBass.explanation.find("Filtro del detector")!=std::string::npos,
          "mixed request keeps the explicit threshold and still explores the sonic objective");
    check(mixedBass.after.peakDb<=-1.f,"mixed proposal preserves headroom through compensation");

    auto tameWithGain=tameAttack;
    tameWithGain.compensateGain=true;
    const auto compensatedTame=calibrate(spiky,start,start,tameWithGain);
    check(compensatedTame.outcome==Outcome::Candidate && compensatedTame.after.crestDb<compensatedTame.before.crestDb &&
          compensatedTame.settings.outputGainDb>0.f && std::abs(compensatedTame.matchGainDb)<0.08f,
          "qualitative changes still run when automatic gain compensation is requested");
    auto centred=calibrate(fixtures::drums(false),start,start,drums);
    check(centred.outcome!=Outcome::Candidate || !centred.settings.midSide,"centred cymbal");
    auto balanced=calibrate(fixtures::voice(.25f,.25f),start,start,Goal{Source::Voice,{{Objective::Stabilise,.8f}}});
    check(balanced.outcome!=Outcome::Candidate || balanced.after.phraseSpreadDb<balanced.before.phraseSpreadDb,
          "a stable clip is changed only if a trial advances the requested direction");
    auto clipped=track;
    for(auto& x:clipped.track[0]) x=std::clamp(x*10,-1.f,1.f);
    const auto clippedTrial=calibrate(clipped,start,start,foreground);
    check(clippedTrial.before.clippedFraction>0.002f &&
          (clippedTrial.outcome!=Outcome::Candidate || clippedTrial.after.peakDb<=-1.f),
          "hot input is assessed and proposals respect output headroom");
    Goal slight{Source::Other,{{Objective::ReduceAttack,.01f}},true,false};
    const auto gentleFallback=calibrate(spiky,start,start,slight);
    check(gentleFallback.outcome==Outcome::Candidate && gentleFallback.after.crestDb<gentleFallback.before.crestDb,
          "offer a trial that advances the request even for a small target");
    auto sibilant=track;
    for(size_t n=0;n<sibilant.track[0].size();++n) sibilant.track[0][n]+=.4f*std::sin(fixtures::tau*8900*n/sibilant.sampleRate);
    auto ess=calibrate(sibilant,start,start,foreground);
    check(ess.settings.mid.airDb==0,"protect bright consonants");
    std::stop_source stop;stop.request_stop();
    check(calibrate(track,start,start,foreground,stop.get_token()).outcome==Outcome::Cancelled,"cancelled request");
    check(kit.renders<=48 && voice.renders<=48,"bounded search");
}
