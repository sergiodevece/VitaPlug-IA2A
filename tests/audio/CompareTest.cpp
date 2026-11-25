#include "audio/Compare.h"
#include <array>
#include <cmath>
#include <iostream>
using namespace ia2a;
int main() {
    Settings candidate;
    candidate.enabled=true;candidate.mid.thresholdDb=-34;candidate.outputGainDb=3;
    Compare live;live.prepare(48000,1,37);
    OpticalPath oracle;oracle.prepare(48000,1);oracle.setSettings(candidate);
    live.publish({Settings::transparent(),candidate,true,true,0});
    double peakError=0;
    for(int block=0;block<320;++block) {
        if(block==170) live.publish({candidate,candidate,false,false,0});
        std::array<float,37> main{},expected{};
        for(int i=0;i<37;++i)main[i]=expected[i]=.35f*std::sin(float(i+block*37)*.067f);
        AudioBlockView a{{main.data(),nullptr},1,37}, b{{expected.data(),nullptr},1,37};
        GrMeters gr; live.process(a,gr);oracle.process(b,gr);
        if(block>40)for(int i=0;i<37;++i)peakError=std::max(peakError,std::abs(double(main[i])-expected[i]));
    }
    if(peakError>1e-6) return 1;
    std::cout<<"Warm final preview and confirmation agree, peak error "<<peakError<<'\n';
}
