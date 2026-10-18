/*
 * GlideMix: the demo version's limits.
 */
#include "MixtapeDemo.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace mixtape {
namespace {
std::atomic<bool> sDemo { false };
constexpr double kFadeSec = 0.03;
constexpr float kNoiseLevel = 0.05f;   // about -26 dBFS: clearly there, never loud
}

void SetDemoMode(bool demo)
{
    sDemo.store(demo, std::memory_order_relaxed);
}

bool DemoMode()
{
    return sDemo.load(std::memory_order_relaxed);
}

void ApplyDemoGaps(float* const* out, size_t channels, size_t frames, int64_t outputFrame, double rate,
                   bool addNoise, uint32_t& noiseState)
{
    const int64_t period = static_cast<int64_t>(kDemoGapPeriodSec * rate);
    const int64_t gap = static_cast<int64_t>(kDemoGapSec * rate);
    const int64_t fade = std::max<int64_t>(1, static_cast<int64_t>(kFadeSec * rate));
    const int64_t gapStart = period - gap;   // the last seconds of each period
    for (size_t i = 0; i < frames; ++i) {
        const int64_t p = (outputFrame + static_cast<int64_t>(i)) % period;
        if (p < gapStart - fade) {
            continue;
        }
        float gain;
        if (p < gapStart) {
            gain = static_cast<float>(gapStart - p) / static_cast<float>(fade);        // fading out
        } else if (p >= period - fade) {
            gain = static_cast<float>(p - (period - fade)) / static_cast<float>(fade); // fading back in
        } else {
            gain = 0.0f;
        }
        const float inGap = p >= gapStart ? 1.0f - gain : 0.0f;
        for (size_t c = 0; c < channels; ++c) {
            float v = out[c][i] * gain;
            if (addNoise && inGap > 0.0f) {
                noiseState = noiseState * 1664525u + 1013904223u;
                v += inGap * kNoiseLevel * (static_cast<float>(noiseState >> 8) / 8388608.0f - 1.0f);
            }
            out[c][i] = v;
        }
    }
}
}
