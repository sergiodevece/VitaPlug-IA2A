#include <AudioToolbox/AudioToolbox.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Manual host integration test for the installed macOS component.
namespace {
void check(OSStatus code,const char* action) {
    if(code)throw std::runtime_error(std::string(action)+" ("+std::to_string(code)+")");
}
struct Input {float amplitude; unsigned calls=0;};
OSStatus provide(void* context,AudioUnitRenderActionFlags*,const AudioTimeStamp*,UInt32,UInt32 frames,AudioBufferList* io) {
    auto& input=*static_cast<Input*>(context);++input.calls;
    for(UInt32 c=0;c<io->mNumberBuffers;++c) {
        auto* dst=static_cast<float*>(io->mBuffers[c].mData);
        if(!dst)return kAudio_ParamError;
        for(UInt32 n=0;n<frames;++n)dst[n]=(n%2==0?1.f:-1.f)*input.amplitude;
    }
    return noErr;
}
}
int main() try {
    AudioComponentDescription id{kAudioUnitType_Effect,'Ia2a','Vtpl',0,0};
    auto component=AudioComponentFindNext(nullptr,&id);
    if(!component)throw std::runtime_error("Install IA2A.component before this test");
    AudioUnit unit{};
    check(AudioComponentInstanceNew(component,&unit),"open AU");
    Input inputs[2]{{0},{.5f}};
    for(UInt32 bus=0;bus<2;++bus) {
        AURenderCallbackStruct callback{provide,&inputs[bus]};
        check(AudioUnitSetProperty(unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,bus,
                                   &callback,sizeof(callback)),"connect input");
    }
    check(AudioUnitInitialize(unit),"initialize");
    struct Buffers {UInt32 count=2;AudioBuffer b[2];} buffers;
    std::array<std::array<float,256>,2> output{};
    for(int ch=0;ch<2;++ch)buffers.b[ch]={1,256*sizeof(float),output[ch].data()};
    for(int block=0;block<16;++block) {
        if(block==8)inputs[0].amplitude=.125f;
        AudioTimeStamp time{};time.mSampleTime=block*256;time.mFlags=kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags flags=0;
        check(AudioUnitRender(unit,&flags,&time,0,256,reinterpret_cast<AudioBufferList*>(&buffers)),"render");
        for(const auto& channel:output)for(int n=0;n<256;++n)
            if(channel[n]!=(n%2==0?1.f:-1.f)*inputs[0].amplitude) throw std::runtime_error("aux leaked or main changed");
    }
    check(AudioUnitUninitialize(unit),"uninitialize");
    check(AudioComponentInstanceDispose(unit),"dispose");
    if(!inputs[1].calls) throw std::runtime_error("aux was not rendered");
    std::cout<<"Installed AU: reference pulled "<<inputs[1].calls<<" times, exact track-only output.\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
