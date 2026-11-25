#include "audio/Capture.h"
#include <array>
#include <cstdio>
using namespace ia2a;
int main() {
    Capture capture;
    capture.prepare(100, 1, 1);
    std::array<float,13> dry{}, aux{};
    dry[2]=0.5f; aux[8]=0.75f;
    ConstBlockView track{{dry.data(),nullptr},1,13}, side{{aux.data(),nullptr},1,13};
    capture.push(track, side);
    capture.arm(true);
    for (int n=0;n<93;++n) capture.push(track,side);
    auto result=capture.take();
    if(!result || result->prerollSamples!=13 || result->track[0].size()!=1213 ||
       result->reference[0][13+8]!=0.75f || result->track[0][13+2]!=0.5f ||
       result->track[0][13+8]!=0.0f) return 1;
    capture.arm(false);
    capture.push(track,side);
    capture.invalidate();
    if(capture.take() || capture.status()!=Capture::Idle) return 2;
    capture.prepare(100,1,0);
    capture.arm(true);
    for(int n=0;n<93;++n) capture.push(track,{});
    auto mono=capture.take();
    if(!mono || !mono->reference.empty()) return 3;
    capture.push(track,{});
    capture.invalidate();
    capture.arm(false);
    for(int n=0;n<93;++n) capture.push(track,{});
    auto recaptured=capture.take();
    if(!recaptured || recaptured->prerollSamples!=0) return 4;
    capture.arm(false);
    capture.push(track,{});
    if(capture.finishAvailable()) return 5;
    for(int n=0;n<10;++n) capture.push(track,{});
    if(!capture.finishAvailable()) return 6;
    auto shortPass=capture.take();
    if(!shortPass || shortPass->track[0].size()!=143) return 7;
    std::puts("AuxRouting OK");
}
