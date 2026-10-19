/*
 * GlideMix: the demo version's limits.
 */
#include "MixtapeDemo.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>

#include "MixtapeDemoVoice.h"

namespace mixtape {
namespace {
std::atomic<bool> sDemo { false };
constexpr double kDuckSec = 0.08;        // the music dips this fast, and comes back
constexpr float kDuckGain = 0.35f;       // about -9 dB under the voice
constexpr float kVoiceGain = 0.6f;       // voice peak about -4.4 dBFS: room for the ducked music
constexpr size_t kVoiceLength = std::size(kDemoVoice);
}

void SetDemoMode(bool demo)
{
    sDemo.store(demo, std::memory_order_relaxed);
}

bool DemoMode()
{
    return sDemo.load(std::memory_order_relaxed);
}

double DemoVoiceSeconds()
{
    return static_cast<double>(kVoiceLength) / kDemoVoiceRate;
}

void ApplyDemoVoice(float* const* out, size_t channels, size_t frames, int64_t outputFrame, double rate)
{
    if (rate <= 0.0) {
        return;
    }
    const int64_t first = static_cast<int64_t>(kDemoVoiceFirstSec * rate);
    const int64_t period = static_cast<int64_t>(kDemoVoicePeriodSec * rate);
    const int64_t duck = std::max<int64_t>(1, static_cast<int64_t>(kDuckSec * rate));
    const int64_t voice = static_cast<int64_t>(DemoVoiceSeconds() * rate);
    const double step = kDemoVoiceRate / rate;   // voice samples per output frame
    for (size_t i = 0; i < frames; ++i) {
        const int64_t f = outputFrame + static_cast<int64_t>(i);
        // Position in the current tag window: it starts `duck` before the voice.
        const int64_t since = f - (first - duck);
        if (since < 0) {
            continue;
        }
        const int64_t p = since % period;
        if (p >= voice + 2 * duck) {
            continue;
        }
        // The music dips before the voice and comes back after it.
        float gain = kDuckGain;
        if (p < duck) {
            gain = 1.0f - (1.0f - kDuckGain) * static_cast<float>(p) / static_cast<float>(duck);
        } else if (p >= voice + duck) {
            gain = kDuckGain + (1.0f - kDuckGain) * static_cast<float>(p - voice - duck) / static_cast<float>(duck);
        }
        // The voice, resampled with linear interpolation (speech, 24 kHz).
        float v = 0.0f;
        const int64_t vp = p - duck;
        if (vp >= 0 && vp < voice) {
            const double pos = static_cast<double>(vp) * step;
            const size_t k = static_cast<size_t>(pos);
            if (k + 1 < kVoiceLength) {
                const double frac = pos - static_cast<double>(k);
                v = static_cast<float>((kDemoVoice[k] * (1.0 - frac) + kDemoVoice[k + 1] * frac) / 32768.0) * kVoiceGain;
            }
        }
        for (size_t c = 0; c < channels; ++c) {
            out[c][i] = out[c][i] * gain + v;
        }
    }
}
}
