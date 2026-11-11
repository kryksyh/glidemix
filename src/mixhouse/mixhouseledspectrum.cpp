/*
 * GlideMix: see mixhouseledspectrum.h.
 *
 * Levels: Hann window, 4096-point FFT, the loudest bin of each of 40
 * log-spaced bands (30 Hz - 16 kHz), plus 3 dB per octave above 1 kHz (and
 * minus below) so music reads level across the bands. -46 dB lights nothing,
 * -14 dB all seven rows: measured on real songs (median about half lit, drops
 * near the top).
 */
#include "mixhouseledspectrum.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "au3-mixtape-engine/MixtapeSpectrumTap.h"
#include "dsp/transforms/FFT.h"

namespace au::mixhouse {
namespace {
constexpr int kSize = 4096;
constexpr double kLowHz = 30.0;
constexpr double kHighHz = 16000.0;
constexpr double kFloorDb = -46.0;
constexpr double kTopDb = -14.0;
constexpr double kFallRowsPerSecond = LedSpectrum::kRows / 0.3;
constexpr double kPi = 3.14159265358979323846;
}

struct LedSpectrum::Fft {
    FFTReal fft { kSize };
    std::vector<double> window, in, re, im;
    Fft()
        : window(kSize), in(kSize), re(kSize), im(kSize)
    {
        for (int i = 0; i < kSize; ++i) {
            window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (kSize - 1));
        }
    }
};

LedSpectrum::LedSpectrum()
    : m_fft(std::make_unique<Fft>()), m_samples(kSize)
{
}

LedSpectrum::~LedSpectrum() = default;

bool LedSpectrum::tick(double seconds)
{
    std::array<float, kBands> target {};
    double rate = 44100.0;
    const bool playing = mixtape::SpectrumTap::Instance().Read(m_samples.data(), kSize,
                                                                mixtape::SpectrumTap::Clock::now(), &rate);
    if (playing) {
        Fft& f = *m_fft;
        for (int i = 0; i < kSize; ++i) {
            f.in[i] = m_samples[i] * f.window[i];
        }
        f.fft.forward(f.in.data(), f.re.data(), f.im.data());
        const double binHz = rate / kSize;
        const double fullScale = kSize / 4.0;   // a full-scale sine's peak through the Hann window
        for (int b = 0; b < kBands; ++b) {
            const double lo = kLowHz * std::pow(kHighHz / kLowHz, double(b) / kBands);
            const double hi = kLowHz * std::pow(kHighHz / kLowHz, double(b + 1) / kBands);
            int k0 = std::max(1, static_cast<int>(std::ceil(lo / binHz)));
            int k1 = std::min(kSize / 2 - 1, static_cast<int>(std::ceil(hi / binHz)) - 1);
            if (k1 < k0) {   // a band narrower than a bin: the nearest bin
                k0 = k1 = std::clamp(static_cast<int>(std::lround(std::sqrt(lo * hi) / binHz)), 1, kSize / 2 - 1);
            }
            double peak = 0.0;
            for (int k = k0; k <= k1; ++k) {
                peak = std::max(peak, f.re[k] * f.re[k] + f.im[k] * f.im[k]);
            }
            const double db = 10.0 * std::log10(peak / (fullScale * fullScale) + 1e-12)
                              + 3.0 * std::log2(std::sqrt(lo * hi) / 1000.0);
            target[b] = static_cast<float>(std::clamp((db - kFloorDb) / (kTopDb - kFloorDb), 0.0, 1.0) * kRows);
        }
    }
    bool changed = false;
    const float fall = static_cast<float>(kFallRowsPerSecond * seconds);
    for (int b = 0; b < kBands; ++b) {
        const float next = target[b] >= m_levels[b] ? target[b] : std::max(target[b], m_levels[b] - fall);
        if (std::abs(next - m_levels[b]) > 0.01f) {
            changed = true;
        }
        m_levels[b] = next;
    }
    return changed;
}
}
