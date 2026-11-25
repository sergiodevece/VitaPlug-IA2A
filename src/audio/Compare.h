#pragma once
#include "assist/Session.h"
#include "dsp/OpticalPath.h"
#include <atomic>
#include <mutex>
#include <vector>

namespace ia2a {
// The audio thread owns both warm paths. Control publishes complete snapshots.
class Compare {
public:
    void prepare(double hz,int channels,int maximum);
    void publish(const Monitor&);
    void process(AudioBlockView,GrMeters&,bool forceApplied=false) noexcept;
private:
    OpticalPath paths[2];
    Settings settings[2];
    int appliedIndex=0,capacity=0;
    float mix=0, gain=1,step=1;
    Monitor next,current;
    std::mutex mailbox;
    std::vector<float> scratch[2];
};
}
