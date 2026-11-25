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
