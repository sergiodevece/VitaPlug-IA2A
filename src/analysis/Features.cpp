#include "analysis/Features.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace ia2a {
namespace {
constexpr double silence = 6.31e-8; // -72 dBFS mean square
float powerDb(double power) { return static_cast<float>(10.0 * std::log10(std::max(power, 1.e-12))); }
float spread(std::vector<float>& levels) {
    if (levels.size()<2) return 0;
    std::sort(levels.begin(),levels.end());
    auto last=static_cast<double>(levels.size()-1);
    return levels[std::lround(last*.90)]-levels[std::lround(last*.10)];
}
struct Window { double energy, mid, high, sideHigh, reference, peak; int size; };
bool validChannels(const std::vector<std::vector<float>>& channels, size_t count) {
    return !channels.empty() && channels.size()<=2 &&
           std::all_of(channels.begin(),channels.end(),[count](const auto& ch){ return ch.size()==count; });
}
}
Features analyse(const Clip& clip) {
    Features out;
    if (!std::isfinite(clip.sampleRate) || clip.sampleRate<1000 || clip.sampleRate>768000 || clip.track.empty()) return out;
    const size_t frames=clip.track.front().size();
    if (!validChannels(clip.track,frames) || clip.prerollSamples<0 || static_cast<size_t>(clip.prerollSamples)>=frames) return out;
    out.hasReference=validChannels(clip.reference,frames);
    if (!clip.reference.empty() && !out.hasReference) return out;

    const int windowSize=static_cast<int>(std::lround(.020*clip.sampleRate));
    const bool stereo=clip.track.size()==2;
    const double filter=std::exp(-6.283185307179586*std::min(4000.,clip.sampleRate*.35)/clip.sampleRate);
    std::vector<Window> windows;
    windows.reserve(frames/windowSize+1);
    double lowMid=0,lowSide=0,sumMid=0,sumSide=0,sumMidHigh=0,sumSideHigh=0,peak=0;
    size_t clipped=0;
    Window w{};
    const size_t first=clip.prerollSamples;
    for(size_t n=0;n<frames;++n) {
        const double left=clip.track[0][n], right=stereo?clip.track[1][n]:left;
        if(!std::isfinite(left)||!std::isfinite(right)) return {};
        const double mid=(left+right)*.5, side=(left-right)*.5;
        lowMid+=(1-filter)*(mid-lowMid);
        lowSide+=(1-filter)*(side-lowSide);
        if(n<first) continue;
        const double midPower=mid*mid, sidePower=side*side;
        const double midHigh=(mid-lowMid)*(mid-lowMid), sideHigh=(side-lowSide)*(side-lowSide);
        const double samplePeak=std::max(std::abs(left),std::abs(right));
        clipped+=samplePeak>=.999;
        peak=std::max(peak,samplePeak);
        sumMid+=midPower; sumSide+=sidePower; sumMidHigh+=midHigh; sumSideHigh+=sideHigh;
        w.energy+=midPower+sidePower; w.mid+=midPower; w.high+=midHigh+sideHigh; w.sideHigh+=sideHigh;
        w.peak=std::max(w.peak,samplePeak);
        for(const auto& channel:clip.reference) {
            const double x=channel[n];
            if(!std::isfinite(x)) return {};
            w.reference+=x*x/clip.reference.size();
        }
        ++w.size;
        if(w.size==windowSize || n+1==frames) {
            w.energy/=w.size; w.high/=w.size; w.mid/=w.size; w.sideHigh/=w.size; w.reference/=w.size;
            windows.push_back(w); w={};
        }
    }
    const double count=static_cast<double>(frames-first);
    out.midEnergy=sumMid/count; out.sideEnergy=sumSide/count;
    out.midHighEnergy=sumMidHigh/count; out.sideHighEnergy=sumSideHigh/count;
    out.clippedFraction=static_cast<float>(clipped/count);
    out.peakDb=powerDb(peak*peak); out.rmsDb=powerDb((sumMid+sumSide)/count);
    out.crestDb=out.peakDb-out.rmsDb; out.highRatioDb=powerDb((sumMidHigh+sumSideHigh)/std::max(1.e-12,sumMid+sumSide));
    out.monoFoldDb=powerDb(sumMid/std::max(1.e-12,sumMid+sumSide));
    // Relative audition level: gated energy with extra weight for the high band.
    double gated=0,attack=0,decay=0,coRef=0,coTrack=0,tailSide=0,tailMid=0;
    int up=0,down=0,coincident=0;
    std::vector<float> highs, phrases;
    const double gate=std::max(silence, (sumMid+sumSide)/count*.001);
    double maxMid=0;
    for(const auto& v:windows) maxMid=std::max(maxMid,v.mid);
    double x=0,y=0,xx=0,yy=0,xy=0;
    for(size_t k=0;k<windows.size();++k) {
        const Window& v=windows[k];
        if(v.reference>silence) out.hasReferenceSignal=true;
        if(v.energy<gate) continue;
        ++out.activeWindows;
        gated+=v.energy+1.5*v.high;
        highs.push_back(powerDb(v.high));
        x+=v.mid; y+=v.high; xx+=v.mid*v.mid; yy+=v.high*v.high; xy+=v.mid*v.high;
        if(v.mid<maxMid*.1) { tailSide+=v.sideHigh; tailMid+=v.mid; }
        if(v.reference>silence) { ++coincident; coRef+=v.reference; coTrack+=v.energy; }
        if(k>0 && windows[k-1].energy>=gate) {
            float delta=powerDb(v.energy)-powerDb(windows[k-1].energy);
            if(delta>0) {attack+=delta; ++up;} else {decay-=delta; ++down;}
        }
    }
    // 400 ms windows, stepped at 200 ms, ignore fragments with fewer than 100 ms of active audio.
    for(size_t start=0;start<windows.size();start+=10) {
        size_t end=std::min(start+20,windows.size());
        if(end-start<5 && start!=0) break;
        double energy=0; int active=0;
        for(size_t j=start;j<end;++j) if(windows[j].energy>=gate) {energy+=windows[j].energy; ++active;}
        if(active>=5) phrases.push_back(powerDb(energy/active));
    }
    out.phraseWindows=static_cast<int>(phrases.size()); out.phraseSpreadDb=spread(phrases); out.highSpreadDb=spread(highs);
    out.usable=out.activeWindows>=5 && out.phraseWindows>0;
    if(!out.activeWindows) return out;
    const double nw=out.activeWindows;
    const double denom=std::sqrt(std::max(0.,(nw*xx-x*x)*(nw*yy-y*y)));
    if(denom>1.e-16) out.highPulseCorrelation=static_cast<float>((nw*xy-x*y)/denom);
    out.sideTailRatioDb=powerDb(tailSide/std::max(tailMid,1.e-12));
    out.monitorDb=powerDb(gated/nw); out.referenceOverlap=coincident/nw;
    if(coincident) out.referenceContrastDb=powerDb(coTrack/coRef);
    if(up) out.attackDb=attack/up;
    if(down) out.decayDb=decay/down;
    return out;
}
}
