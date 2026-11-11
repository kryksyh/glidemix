/*
 * GlideMix: the LED meter above the clock -- 40 bands of what the speakers
 * play now (mixtape::SpectrumTap), as lit rows of a 40 x 7 dot matrix.
 */
#pragma once

#include <array>
#include <memory>
#include <vector>

namespace au::mixhouse {
class LedSpectrum
{
public:
    static constexpr int kBands = 40;
    static constexpr int kRows = 7;

    LedSpectrum();
    ~LedSpectrum();

    /// Called ~30 times a second. Reads the tap, updates the lit rows
    /// (instant rise, ~300 ms fall). True when anything changed on screen.
    bool tick(double seconds);

    /// 0..kRows per band, fractional (the top dot of a band fades in).
    const std::array<float, kBands>& levels() const { return m_levels; }

private:
    struct Fft;
    std::unique_ptr<Fft> m_fft;
    std::vector<float> m_samples;
    std::array<float, kBands> m_levels {};
};
}
