#include "Compare.h"
#include <algorithm>
#include <cmath>
namespace ia2a {
void Compare::prepare(double hz,int channels,int maximum) {
    std::lock_guard lock(mailbox);
    current=next; appliedIndex=0; capacity=std::max(1,maximum);
    for(int i=0;i<2;++i) {
        settings[i]=i==0?current.applied:current.candidate;
        paths[i].setSettings(settings[i]);paths[i].prepare(hz,channels);
        scratch[i].resize(capacity);
    }
    mix=current.preview?1.f:0.f; gain=std::pow(10.f,current.gainDb/20.f);
    step=1.f/std::max(1.f,float(hz*0.015));
}
void Compare::publish(const Monitor& m) {
    std::lock_guard lock(mailbox);
    next=m;
}
void Compare::process(AudioBlockView block,GrMeters& gr,bool forceApplied) noexcept {
    if(capacity==0 || !block.samples) return;
    {
        std::unique_lock lock(mailbox,std::try_to_lock);
        if(lock) current=next;
    }
    if(settings[appliedIndex]!=current.applied && settings[1-appliedIndex]==current.applied)
        appliedIndex=1-appliedIndex;
    const int other=1-appliedIndex;
    auto update=[&](int i,const Settings& s) {
        if(settings[i]!=s) { settings[i]=s; paths[i].setSettings(s); }
    };
    update(appliedIndex,current.applied);
    if(current.hasCandidate) update(other,current.candidate);
    const int destination=current.preview && !forceApplied ? other : appliedIndex;
    const float goal=float(destination);
    const float goalGain=std::pow(10.f,(forceApplied?0.f:current.gainDb)/20.f);
    for(int offset=0;offset<block.samples;offset+=capacity) {
        int count=std::min(capacity,block.samples-offset);
        AudioBlockView views[2]{};
        for(auto& v:views) {v.samples=count;v.channels=block.channels;}
        for(int c=0;c<block.channels;++c) {
            views[0].data[c]=block.data[c]+offset;
            views[1].data[c]=scratch[c].data();
            std::copy_n(views[0].data[c],count,views[1].data[c]);
        }
        GrMeters meters[2];
        paths[0].process(views[0],meters[0]); paths[1].process(views[1],meters[1]);
        gr=meters[destination];
        for(int n=0;n<count;++n) {
            mix+=std::clamp(goal-mix,-step,step);
            gain+=std::clamp(goalGain-gain,-step,step);
            for(int c=0;c<block.channels;++c) {
                const float a=views[0].data[c][n], b=views[1].data[c][n];
                views[0].data[c][n]=(mix==1.f?b:mix==0.f?a:a+(b-a)*mix)*gain;
            }
        }
    }
}
}
