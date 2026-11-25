#include "Capture.h"
#include <algorithm>
#include <cmath>
namespace ia2a {
void Capture::prepare(double hz,int channels,int aux) {
    std::lock_guard guard(mutex);
    invalidate();
    rate=hz;
    mainCount=channels;
    auxCount=aux;
    prior=std::max(1,int(std::round(hz*2)));
    target=std::max(1,int(std::round(hz*12)));
    history.assign(channels+aux,std::vector<float>(prior));
    recorded.assign(channels+aux,std::vector<float>(target+prior));
    head=filled=used=0;
    runningRevision=generation.load();
}
void Capture::invalidate() noexcept {
    clearHistory();
    generation.fetch_add(1);
    state.store(Idle);
    fraction.store(0);
}
void Capture::arm(bool ref) {
    std::lock_guard guard(mutex);
    includeAux=ref;
    generation.fetch_add(1);
    fraction.store(0);
    state.store(Armed);
}
void Capture::push(ConstBlockView track,ConstBlockView aux) noexcept {
    std::unique_lock guard(mutex,std::try_to_lock);
    if (!guard || prior==0 || track.channels!=mainCount) return;
    const auto revision=generation.load();
    auto stage=state.load();
    if(clearRequested.exchange(false) && stage!=Ready) head=filled=0;
    if (runningRevision!=revision) {
        runningRevision=revision;
        if (stage!=Armed) head=filled=0;
    }
    if (stage==Armed) {
        prefix=filled;
        used=0;
        // Keep the pre-roll ring frozen while the scored interval is recorded.
        if (!state.compare_exchange_strong(stage,Recording)) return;
        stage=Recording;
    }
    if (stage==Ready) return;
    const int count=stage==Recording ? std::min(track.samples,target-used) : track.samples;
    for(int ch=0;ch<mainCount+auxCount;++ch) {
        const float* input=ch<mainCount ? track.data[ch] :
            (ch-mainCount<aux.channels ? aux.data[ch-mainCount] : nullptr);
        auto copy=[input](float* dest,int offset,int len) {
            if(input) std::copy_n(input+offset,len,dest);
            else std::fill_n(dest,len,0.0f);
        };
        if(stage==Recording) copy(recorded[ch].data()+used,0,count);
        else {
            int offset=0, pos=head;
            while(offset<count) {
                const int n=std::min(count-offset,prior-pos);
                copy(history[ch].data()+pos,offset,n);
                offset+=n; pos=(pos+n)%prior;
            }
        }
    }
    if(stage==Recording) {
        used+=count;
        fraction.store(float(used)/target);
        if(used==target && generation.load()==revision)
            state.compare_exchange_strong(stage,Ready);
    } else { head=(head+count)%prior; filled=std::min(prior,filled+count); }
}
bool Capture::finishAvailable() {
    std::lock_guard guard(mutex);
    State expected=Recording;
    return used>=rate && state.compare_exchange_strong(expected,Ready);
}
std::optional<Clip> Capture::take() {
    std::lock_guard guard(mutex);
    if(state.load()!=Ready) return {};
    Clip clip;
    clip.sampleRate=rate; clip.prerollSamples=prefix; clip.generation=runningRevision;
    for(int ch=0;ch<mainCount+(includeAux?auxCount:0);++ch) {
        auto& dest=(ch<mainCount?clip.track:clip.reference).emplace_back(prefix+used);
        const int begin=(head-prefix+prior)%prior, first=std::min(prefix,prior-begin);
        std::copy_n(history[ch].data()+begin,first,dest.data());
        std::copy_n(history[ch].data(),prefix-first,dest.data()+first);
        std::copy_n(recorded[ch].data(),used,dest.data()+prefix);
    }
    head=filled=0;
    state.store(Idle);
    return clip.generation==generation.load()?std::optional<Clip>{std::move(clip)}:std::nullopt;
}
}
