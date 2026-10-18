/*
 * GlideMix: how loud an exported mix is (EBU R128 / ITU BS.1770), measured
 * while it renders: integrated loudness, true peak and sample peak. Streaming
 * sites turn a mix up or down to their own target, so a DJ wants these
 * numbers when the file is written.
 */
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

class EBUR128;

namespace mixtape {
//! The true-peak interpolator: 8x oversampling, a 192-tap Kaiser-windowed
//! sinc split into 8 phases of 24. Each phase, run over the last kTaps
//! samples (oldest first), gives one point between samples. Finer than
//! BS.1770's minimum (4x, 12 taps), which misses peaks in dense music that
//! ffmpeg's meter (and an MP3 encoder) see. Shared by the meter and the
//! master limiter, so the limiter holds exactly what the meter reads.
struct TruePeakFilter {
    static constexpr int kPhases = 8;
    static constexpr int kTaps = 24;
    static const std::vector<std::vector<float> >& Phases();
};

struct LoudnessReport {
    double integratedLufs = -70.0;   //!< -70 for silence
    double truePeakDb = -100.0;      //!< dBTP, 8x oversampled
    double samplePeakDb = -100.0;    //!< dBFS
    double seconds = 0.0;
};

class LoudnessMeter
{
public:
    LoudnessMeter(double rate, size_t channels);
    ~LoudnessMeter();

    void Process(const float* const* channels, size_t frames);
    LoudnessReport Report() const;

private:
    double TruePeakOf(size_t channel, float sample);

    const double mRate;
    const size_t mChannels;
    std::unique_ptr<EBUR128> mR128;
    // 4x oversampling: per channel, the last kTaps input samples.
    std::vector<std::vector<float> > mHistory;
    std::vector<size_t> mHistoryPos;
    std::vector<std::vector<float> > mPhases;   // TruePeakFilter's phases
    float mSamplePeak = 0.0f;
    double mTruePeak = 0.0;
    size_t mFrames = 0;
};
}
