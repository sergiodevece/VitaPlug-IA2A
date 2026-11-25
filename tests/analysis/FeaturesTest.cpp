#include "analysis/Features.h"
#include "tests/fixtures/Signals.h"
#include <iostream>
#include <limits>
#include <stdexcept>
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    using namespace ia2a;
    auto even = fixtures::voice(.2f,.2f);
    auto uneven = fixtures::voice(.1f,.2f);
    auto steady = analyse(even), jumps = analyse(uneven);
    check(steady.usable && std::abs(jumps.phraseSpreadDb-steady.phraseSpreadDb-6.02f)<.45f,"phrase level in dB");
    auto wide = analyse(fixtures::drums(true)), centre = analyse(fixtures::drums(false));
    check(wide.sideHighEnergy>centre.sideHighEnergy && wide.sideHighEnergy>wide.midHighEnergy,"side highs");
    check(centre.sideHighEnergy==0 && std::isfinite(centre.monoFoldDb),"centred stereo");
    check(jumps.attackDb>=0 && jumps.crestDb>0 && !jumps.hasReference,"voice alone");
    auto lowTone=even, highTone=even;
    for(std::size_t n=0;n<even.track[0].size();++n) {
        lowTone.track[0][n]=.2f*std::sin(fixtures::tau*70.*n/even.sampleRate);
        highTone.track[0][n]=.2f*std::sin(fixtures::tau*3000.*n/even.sampleRate);
    }
    const auto lows=analyse(lowTone), highs=analyse(highTone);
    check(lows.usable && highs.usable && lows.lowRatioDb>highs.lowRatioDb+16.f,
        "low-band weight distinguishes bass from equal-level high tones");
    even.reference = even.track;
    for(std::size_t n=0;n<even.track[0].size();++n) {
        if(n>=32000) even.track[0][n]=0;
        if(n>=32000) even.reference[0][n]=0;
    }
    auto together=analyse(even);
    std::rotate(even.reference[0].begin(),even.reference[0].begin()+32000,even.reference[0].end());
    auto apart=analyse(even);
    check(together.hasReferenceSignal && together.referenceOverlap>.98f && apart.referenceOverlap<.01f,"aligned reference");
    for(auto* channels:{&even.track,&even.reference}) (*channels)[0].insert((*channels)[0].begin(),6400,0.f);
    check(std::abs(analyse(even).referenceOverlap-apart.referenceOverlap)<.01f,"common shift");
    std::fill(even.track[0].begin(),even.track[0].end(),0.f);
    check(!analyse(even).usable,"silence");
    uneven.track[0][1]=std::numeric_limits<float>::infinity();
    check(!analyse(uneven).usable,"non-finite capture");
    std::cout<<"Phrase change "<<jumps.phraseSpreadDb<<" dB; reference overlap "<<together.referenceOverlap<<" / "<<apart.referenceOverlap<<"\n";
}
