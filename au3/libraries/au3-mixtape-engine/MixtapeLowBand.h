/*
 * GlideMix -- playback engine
 *
 * The bass control used by transitions ("bass swap"): the signal is split at a
 * Linkwitz-Riley crossover (two 2nd-order Butterworth sections per band, 24 dB
 * per octave) and only the low band is scaled,
 *
 *     out = high + low * gain
 *
 * gain 0 removes the bass like the kill switch of a DJ mixer's low EQ, and at
 * gain 1 the two bands sum to a signal with flat magnitude (an allpass: only
 * the phase moves, around the crossover). The upper band is a real high-pass,
 * not "the input minus the low band": that subtraction leaves a large residue
 * at bass frequencies because the low-pass lags in phase, and cuts only about
 * 7 dB at 40 Hz instead of the 60 dB a high-pass gives.
 *
 * GlideMix: the same idea split three ways for the transition EQ: bass | mids
 * at 250 Hz and mids | highs at 2500 Hz (Mixxx's own splits),
 *
 *     out = low' * lowGain + mid * midGain + high * highGain
 *
 * where the mids and highs are the upper band split again, and low' is the low
 * band through the upper crossover's allpass, so that at unity gains the three
 * still sum to a flat allpass.
 *
 * Header only, no dependencies, safe on the render thread (no allocation).
 */
#pragma once

#include <cmath>
#include <algorithm>
#include <cstddef>

namespace mixtape {
class LowBand
{
public:
    //! Where the bass ends. 250 Hz is where Mixxx puts its low/mid split.
    static constexpr double kDefaultCrossoverHz = 250.0;

    //! Where the mids end and the highs start (Mixxx's high split).
    static constexpr double kDefaultUpperCrossoverHz = 2500.0;

    void Configure(double sampleRate, double crossoverHz = kDefaultCrossoverHz,
                   double upperCrossoverHz = kDefaultUpperCrossoverHz)
    {
        Crossover(mLow, mHigh, sampleRate, crossoverHz);
        Crossover(mUpperLow, mUpperHigh, sampleRate, std::min(upperCrossoverHz, sampleRate * 0.45));
        Crossover(mAlignLow, mAlignHigh, sampleRate, std::min(upperCrossoverHz, sampleRate * 0.45));
        Reset();
    }

    //! Three bands, each gain one value per sample (1 = untouched, 0 = removed).
    void Process3(float* samples, const double* lowGain, const double* midGain, const double* highGain, size_t count)
    {
        for (size_t i = 0; i < count; ++i) {
            const double x = samples[i];
            const double low = mLow[1].Run(mLow[0].Run(x));
            const double upper = mHigh[1].Run(mHigh[0].Run(x));
            const double mid = mUpperLow[1].Run(mUpperLow[0].Run(upper));
            const double high = mUpperHigh[1].Run(mUpperHigh[0].Run(upper));
            const double lowAligned = mAlignLow[1].Run(mAlignLow[0].Run(low)) + mAlignHigh[1].Run(mAlignHigh[0].Run(low));
            samples[i] = static_cast<float>(lowAligned * lowGain[i] + mid * midGain[i] + high * highGain[i]);
        }
    }

    //! Forget the signal history (after a seek).
    void Reset()
    {
        for (Biquad* band : { mLow, mHigh, mUpperLow, mUpperHigh, mAlignLow, mAlignHigh }) {
            for (int i = 0; i < 2; ++i) {
                band[i].z1 = band[i].z2 = 0.0;
            }
        }
    }

    //! In place. `gain` holds one value per sample: 1 = untouched, 0 = bass removed.
    void Process(float* samples, const double* gain, size_t count)
    {
        for (size_t i = 0; i < count; ++i) {
            const double x = samples[i];
            const double low = mLow[1].Run(mLow[0].Run(x));
            const double high = mHigh[1].Run(mHigh[0].Run(x));
            samples[i] = static_cast<float>(high + low * gain[i]);
        }
    }

private:
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
        //! Transposed direct form II.
        double Run(double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    //! A Linkwitz-Riley crossover: two Butterworth sections per side.
    static void Crossover(Biquad* lowSide, Biquad* highSide, double sampleRate, double hz)
    {
        constexpr double kPi = 3.14159265358979323846;
        const double w0 = 2.0 * kPi * hz / sampleRate;
        const double cosW = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * 0.70710678118654752440);   // Q = 1/sqrt(2)
        const double a0 = 1.0 + alpha;
        for (int i = 0; i < 2; ++i) {
            Biquad& lp = lowSide[i];
            lp.b0 = (1.0 - cosW) / 2.0 / a0;
            lp.b1 = (1.0 - cosW) / a0;
            lp.b2 = lp.b0;
            lp.a1 = -2.0 * cosW / a0;
            lp.a2 = (1.0 - alpha) / a0;
            Biquad& hp = highSide[i];
            hp.b0 = (1.0 + cosW) / 2.0 / a0;
            hp.b1 = -(1.0 + cosW) / a0;
            hp.b2 = hp.b0;
            hp.a1 = lp.a1;
            hp.a2 = lp.a2;
        }
    }

    Biquad mLow[2];
    Biquad mHigh[2];
    Biquad mUpperLow[2];    // the upper band split into mids ...
    Biquad mUpperHigh[2];   // ... and highs
    Biquad mAlignLow[2];    // the low band through the upper crossover's allpass
    Biquad mAlignHigh[2];
};
}
