#include "dsp/OpticalPath.h"
#include "Scenario.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
ia2a::Settings state(int phase) {
    ia2a::Settings s;
    s.enabled = true;
    auto& m = s.mid; auto& v = s.side;
    m.thresholdDb=-30; m.ratio=6; m.attackMs=4; m.fastReleaseMs=75; m.slowReleaseMs=900;
    m.kneeDb=4; m.memory=0.5f; m.hpfOn=true; m.hpfHz=120;
    v.thresholdDb=-15; v.ratio=2; v.attackMs=12; v.fastReleaseMs=110; v.slowReleaseMs=1200;
    v.memory=0.4f; v.kneeDb=8; v.hpfOn=true; v.hpfHz=200; v.airDb=5; v.makeupDb=-1;
    if (phase != 0) { s.inputTrimDb=2; s.outputGainDb=-2; m.makeupDb=3; m.airDb=2; }
    s.midSide = phase == 2 || phase == 3;
    return s;
}
bool compare(int hz,int channels,int size,const std::filesystem::path& fixtureDir) {
    std::ifstream expected(fixtureDir/scenario::filename(hz,channels,size),std::ios::binary);
    if (!expected) { std::cerr << "Missing reference fixture\n"; return false; }
    ia2a::OpticalPath processed, dry;
    processed.prepare(hz,channels); dry.prepare(hz,channels);
    dry.setSettings(ia2a::Settings::transparent());
    std::array<std::vector<float>,2> input{std::vector<float>(size),std::vector<float>(size)};
    float maximum=0;
    bool differs=false;
    for (int phase=0;phase<scenario::phases;++phase) {
        processed.setSettings(state(phase));
        if (phase==3) processed.reset();
        for(int start=0;start<scenario::segment;start+=size) {
            int count=std::min(size,scenario::segment-start);
            for (int ch=0;ch<channels;++ch) for (int i=0;i<count;++i)
                input[ch][i]=scenario::input(ch,phase*scenario::segment+start+i);
            ia2a::AudioBlockView block{{input[0].data(),input[1].data()},channels,count};
            ia2a::GrMeters meters;
            dry.process(block,meters);
            for (int ch=0;ch<channels;++ch) for(int i=0;i<count;++i)
                if (input[ch][i]!=scenario::input(ch,phase*scenario::segment+start+i)) return false;
            processed.process(block,meters);
            if (meters.midSide != (channels==2 && state(phase).midSide)) return false;
            for (int i=0;i<count;++i) for(int ch=0;ch<channels;++ch) {
                float sample;
                if (!expected.read(reinterpret_cast<char*>(&sample),sizeof(sample))) return false;
                if (!std::isfinite(input[ch][i])) return false;
                maximum=std::max(maximum,std::abs(sample-input[ch][i]));
                differs |= std::abs(sample-scenario::input(ch,phase*scenario::segment+start+i))>0.01f;
            }
        }
    }
    std::cout << hz << " Hz, channels=" << channels << ", block=" << size << ", peak error=" << maximum << '\n';
    return expected.peek()==std::char_traits<char>::eof() && differs && maximum<=1.0e-6f;
}
}
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    for(int hz:scenario::rates) for(int ch:{1,2}) for(int block:scenario::blocks)
        if(!compare(hz,ch,block,argv[1])) return 1;
}
