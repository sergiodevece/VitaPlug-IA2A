#pragma once

namespace ia2a::tuning {
// Starting values for the listening trials. Levels use measured sample peaks.
constexpr float ceilingDb = -1.f;
constexpr float brightRatioDb = -14.f;
constexpr float maxStereoBalanceChangeDb = 5.5f;
constexpr float maxClipFraction = .002f;
constexpr int blockSize = 512;
}
