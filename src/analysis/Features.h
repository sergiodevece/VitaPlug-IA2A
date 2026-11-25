#pragma once
#include "audio/Clip.h"

namespace ia2a {
// Energy fields are mean-square FS². Level fields are dBFS or dB differences.
struct Features {
    bool usable = false, hasReference = false, hasReferenceSignal = false;
    int activeWindows = 0, phraseWindows = 0;
    float peakDb = -120, rmsDb = -120, phraseSpreadDb = 0, crestDb = 0;
    float attackDb = 0, decayDb = 0, highRatioDb = -120, highSpreadDb = 0;
    float midEnergy = 0, sideEnergy = 0, midHighEnergy = 0, sideHighEnergy = 0;
    float sideTailRatioDb = -120, highPulseCorrelation = 0, monoFoldDb = 0;
    float referenceContrastDb = 0, referenceOverlap = 0, clippedFraction = 0;
    float monitorDb = -120;
};
Features analyse(const Clip&);
}
