/*
 * GlideMix: loudness of an exported mix.
 */
#include "MixtapeLoudness.h"

#include <algorithm>
#include <cmath>

#include "au3-math/EBUR128.h"

namespace mixtape {
namespace {
// The true peak: the largest absolute value after oversampling (see
// TruePeakFilter).
constexpr int kTaps = TruePeakFilter::kTaps;
constexpr double kPi = 3.14159265358979323846;

double Db(double amplitude)
{
    return amplitude > 1e-5 ? 20.0 * std::log10(amplitude) : -100.0;
}
}

const std::vector<std::vector<float> >& TruePeakFilter::Phases()
{
    static const auto phases = [] {
        // Kaiser window, beta 8: sidelobes far below what a meter shows.
        const auto besselI0 = [](double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k) {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        constexpr double beta = 8.0;
        std::vector<std::vector<float> > p(kPhases, std::vector<float>(kTaps));
        const int length = kTaps * kPhases;
        const double centre = (length - 1) / 2.0;
        for (int n = 0; n < length; ++n) {
            const double x = (n - centre) / kPhases;
            const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double r = (n - centre) / centre;
            const double window = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
            p[n % kPhases][n / kPhases] = static_cast<float>(sinc * window);
        }
        return p;
    }();
    return phases;
}

LoudnessMeter::LoudnessMeter(double rate, size_t channels)
    : mRate(rate)
    , mChannels(channels)
    , mR128(std::make_unique<EBUR128>(rate, channels))
    , mHistory(channels, std::vector<float>(kTaps, 0.0f))
    , mHistoryPos(channels, 0)
    , mPhases(TruePeakFilter::Phases())
{
}

LoudnessMeter::~LoudnessMeter() = default;

double LoudnessMeter::TruePeakOf(size_t channel, float sample)
{
    auto& history = mHistory[channel];
    size_t& pos = mHistoryPos[channel];
    history[pos] = sample;
    pos = (pos + 1) % kTaps;
    double peak = 0.0;
    for (const auto& phase : mPhases) {
        double sum = 0.0;
        // Oldest sample first, matching the filter's tap order.
        for (int k = 0; k < kTaps; ++k) {
            sum += phase[k] * history[(pos + k) % kTaps];
        }
        peak = std::max(peak, std::abs(sum));
    }
    return peak;
}

void LoudnessMeter::Process(const float* const* channels, size_t frames)
{
    for (size_t i = 0; i < frames; ++i) {
        for (size_t c = 0; c < mChannels; ++c) {
            const float x = channels[c][i];
            mR128->ProcessSampleFromChannel(x, c);
            mSamplePeak = std::max(mSamplePeak, std::abs(x));
            mTruePeak = std::max(mTruePeak, TruePeakOf(c, x));
        }
        mR128->NextSample();
    }
    mFrames += frames;
}

LoudnessReport LoudnessMeter::Report() const
{
    LoudnessReport report;
    const double loudness = mR128->IntegrativeLoudness();
    report.integratedLufs = loudness > 0.0 ? std::max(-70.0, mR128->IntegrativeLoudnessToLUFS(loudness)) : -70.0;
    // The true peak is never below the sample peak.
    report.truePeakDb = Db(std::max<double>(mTruePeak, mSamplePeak));
    report.samplePeakDb = Db(mSamplePeak);
    report.seconds = mRate > 0.0 ? mFrames / mRate : 0.0;
    return report;
}
}
