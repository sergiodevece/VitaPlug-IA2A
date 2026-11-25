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
    auto centred=calibrate(fixtures::drums(false),start,start,drums);
    check(centred.outcome!=Outcome::Candidate || !centred.settings.midSide,"centred cymbal");
    auto balanced=calibrate(fixtures::voice(.25f,.25f),start,start,Goal{Source::Voice,{{Objective::Stabilise,.8f}}});
    check(balanced.outcome==Outcome::Keep,"preserve steady voice");
    auto clipped=track;
    for(auto& x:clipped.track[0]) x=std::clamp(x*10,-1.f,1.f);
    check(calibrate(clipped,start,start,foreground).outcome==Outcome::Clarify,"printed clipping");
    auto sibilant=track;
    for(size_t n=0;n<sibilant.track[0].size();++n) sibilant.track[0][n]+=.4f*std::sin(fixtures::tau*8900*n/sibilant.sampleRate);
    auto ess=calibrate(sibilant,start,start,foreground);
    check(ess.settings.mid.airDb==0,"protect bright consonants");
    std::stop_source stop;stop.request_stop();
    check(calibrate(track,start,start,foreground,stop.get_token()).outcome==Outcome::Cancelled,"cancelled request");
    check(kit.renders<=48 && voice.renders<=48,"bounded search");
}
