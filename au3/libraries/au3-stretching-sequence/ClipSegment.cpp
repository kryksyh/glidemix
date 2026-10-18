/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  ClipSegment.cpp

  Matthieu Hodgkinson

**********************************************************************/
#include "ClipSegment.h"
#include "ClipInterface.h"
#include "au3-mixer/PlaybackSpeedCurve.h"
#include "au3-time-and-pitch/StaffPadTimeAndPitch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace {
TimeAndPitchInterface::Parameters
GetStretchingParameters(const ClipInterface& clip)
{
    TimeAndPitchInterface::Parameters params;
    params.timeRatio = clip.GetStretchRatio();
    params.pitchRatio = std::pow(2., clip.GetCentShift() / 1200.);
    params.preserveFormants
        =clip.GetPitchAndSpeedPreset() == PitchAndSpeedPreset::OptimizeForVoice;
    return params;
}

sampleCount
GetTotalNumSamplesToProduce(const ClipInterface& clip, double durationToDiscard)
{
    return sampleCount { clip.GetVisibleSampleCount().as_double()
                         * clip.GetStretchRatio()
                         - durationToDiscard * clip.GetRate() + .5 };
}
} // namespace

ClipSegment::ClipSegment(
    const ClipInterface& clip, double durationToDiscard, PlaybackDirection direction)
    : mTotalNumSamplesToProduce{GetTotalNumSamplesToProduce(
                                    clip, durationToDiscard)}
    , mSource{clip, durationToDiscard, direction}
    , mPreserveFormants{clip.GetPitchAndSpeedPreset()
                        == PitchAndSpeedPreset::OptimizeForVoice}
    , mCentShift{clip.GetCentShift()}
    , mStretcher{std::make_unique<StaffPadTimeAndPitch>(
                     clip.GetRate(), clip.NChannels(), mSource,
                     GetStretchingParameters(clip))}
    , mOnSemitoneShiftChangeSubscription{clip.SubscribeToCentShiftChange(
                                             [this](int cents) {
        mCentShift = cents;
        mUpdateCentShift = true;
    })},
    mOnFormantPreservationChangeSubscription {
    clip.SubscribeToPitchAndSpeedPresetChange(
        [this](PitchAndSpeedPreset preset) {
        mPreserveFormants
            =preset == PitchAndSpeedPreset::OptimizeForVoice;
        mUpdateFormantPreservation = true;
    })
}
    , mSpeedCurve{PlaybackSpeedCurve::Live()}
    , mPlayStartTime{clip.GetPlayStartTime()}
    , mPlayEndTime{clip.GetPlayEndTime()}
    , mDurationToDiscard{durationToDiscard}
    , mRate{clip.GetRate()}
    , mDirection{direction}
{
    if (mSpeedCurve) {
        const double compensation = CompensatedPitchRatio(TrackTimeAt(0));
        if (compensation != 1.0) {
            mStretcher->OnPitchRatioChange(
                std::pow(2., mCentShift / 1200.) * compensation);
            mAppliedPitchRatio = compensation;
        }
    }
}

double ClipSegment::TrackTimeAt(sampleCount produced) const
{
    const double elapsed = mDurationToDiscard + produced.as_double() / mRate;
    return mDirection == PlaybackDirection::forward
           ? mPlayStartTime + elapsed : mPlayEndTime - elapsed;
}

double ClipSegment::CompensatedPitchRatio(double trackTime) const
{
    const double speed = mSpeedCurve ? mSpeedCurve->SpeedAt(trackTime) : 1.0;
    return speed > 0.0 ? 1.0 / speed : 1.0;
}

ClipSegment::~ClipSegment()
{
    mOnSemitoneShiftChangeSubscription.Reset();
    mOnFormantPreservationChangeSubscription.Reset();
}

size_t ClipSegment::GetFloats(float* const* buffers, size_t numSamples)
{
    // Check if formant preservation of pitch shift needs to be updated.
    // This approach is not immune to a race condition, but it is unlikely and
    // not critical, as it would only affect one playback pass, during which the
    // user could easily correct the mistake if needed. On the other hand, we
    // cannot trust that the observer subscriptions do not get called after
    // destruction of this object, so better not do anything too sophisticated
    // there.
    if (mUpdateFormantPreservation.exchange(false)) {
        mStretcher->OnFormantPreservationChange(mPreserveFormants);
    }
    // The live curve: an edit made while playing applies right away.
    mSpeedCurve = PlaybackSpeedCurve::Live();
    bool centShiftChanged = mUpdateCentShift.exchange(false);
    if (!mSpeedCurve && mAppliedPitchRatio != 1.0) {
        mAppliedPitchRatio = 1.0;
        centShiftChanged = true;
    }
    if (centShiftChanged && !mSpeedCurve) {
        mStretcher->OnCentShiftChange(mCentShift);
    }
    const auto numSamplesToProduce = limitSampleBufferSize(
        numSamples, mTotalNumSamplesToProduce - mTotalNumSamplesProduced);
    if (!mSpeedCurve) {
        mStretcher->GetSamples(buffers, numSamplesToProduce);
        mTotalNumSamplesProduced += numSamplesToProduce;
        return numSamplesToProduce;
    }

    // Under a speed curve, produce in short blocks and follow the curve's
    // pitch compensation block by block.
    constexpr size_t blockSize = 256;
    size_t done = 0;
    std::array<float*, 8> offsetBuffers {};
    const size_t nChannels = std::min(NChannels(), offsetBuffers.size());
    while (done < numSamplesToProduce) {
        const size_t n = std::min(blockSize, numSamplesToProduce - done);
        const double compensation = CompensatedPitchRatio(
            TrackTimeAt(mTotalNumSamplesProduced + sampleCount { done + n / 2 }));
        if (centShiftChanged || std::abs(compensation - mAppliedPitchRatio) > 1e-5) {
            mStretcher->OnPitchRatioChange(
                std::pow(2., mCentShift / 1200.) * compensation);
            mAppliedPitchRatio = compensation;
            centShiftChanged = false;
        }
        for (size_t i = 0; i < nChannels; ++i) {
            offsetBuffers[i] = buffers[i] + done;
        }
        mStretcher->GetSamples(offsetBuffers.data(), n);
        done += n;
    }
    mTotalNumSamplesProduced += numSamplesToProduce;
    return numSamplesToProduce;
}

bool ClipSegment::Empty() const
{
    return mTotalNumSamplesProduced == mTotalNumSamplesToProduce;
}

size_t ClipSegment::NChannels() const
{
    return mSource.NChannels();
}
