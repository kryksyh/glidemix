/*
 * GlideMix -- playback engine
 *
 * Plays one clip. When the clip is neither stretched nor pitch-shifted the
 * samples are copied straight through; otherwise one Rubber Band stretcher
 * (the keylock Mixxx decks use, EngineBufferScaleRubberBand) does both at
 * once. Only the render thread touches a voice.
 */
#pragma once

#include <memory>
#include <vector>

#include "MixtapeArrangement.h"
#include "MixtapeLowBand.h"

class PlaybackSpeedCurve;

namespace RubberBand {
class RubberBandStretcher;
}

namespace mixtape {
//! Keylock (Mixxx / rekordbox MT), any thread, for every voice: on, tempo
//! changes keep the pitch; off, pitch follows the playback speed like a
//! turntable. Default on.
void SetKeylock(bool enabled);
bool KeylockEnabled();

class ClipVoice
{
public:
    ClipVoice(double outputRate, size_t outputChannels);
    ~ClipVoice();

    //! Take over a (new copy of a) clip. The next render starts wherever asked.
    //! `alwaysStretch`: use the stretcher even at ratio 1 (a tempo curve is
    //! active, so the speed can change within the clip without a seam).
    void Assign(const ClipSnapshot& clip, bool alwaysStretch = false);
    bool AlwaysStretch() const { return mAlwaysStretch; }
    //! Before Assign. Export: a clip at (nearly) its own tempo is only resampled, as on small machines.
    void SetPreferResample(bool prefer) { mPreferResample = prefer; }
    //! Runs Rubber Band (the costly kind of voice).
    bool Stretches() const { return mStretcher != nullptr; }
    const ClipSnapshot& Clip() const { return mClip; }

    //! The clip's audio is unchanged but its volume line or bass line may have been edited (a
    //! transition applied while it plays): adopt the new lines without restarting the voice. A
    //! bass line appearing or disappearing blends in or out over a few milliseconds, because the
    //! crossover shifts the phase of the bass and switching it on hard would click.
    void UpdateLines(const ClipSnapshot& clip);
    //! Adds `frames` of output covering timeline [time, endTime) into `out`,
    //! scaled by `gains` (one per output channel). endTime - time differs from
    //! frames / rate when a tempo curve speeds playback up or down; the one
    //! stretcher then does tempo and keylock together. Continuing where the
    //! last call ended keeps it running; anything else seeks.
    void RenderAdd(float* const* out, size_t frames, double time, double endTime, const float* gains,
                   const PlaybackSpeedCurve* curve = nullptr);

    //! Timeline time the next render would continue from.
    double NextTime() const { return mNextTime; }

private:
    bool Bypass() const;
    bool Resamples() const;
    //! RenderAdd for a clip that is only resampled (Resamples()).
    void RenderResampled(float* const* out, size_t frames, double time, double endTime, const float* gains);
    void Seek(double time);
    void ReadInput(size_t frames);
    void ApplyEnvelope(size_t frames, double time, double step);
    //! The bass stage, in place on `buffers` from `offset`: a no-op for clips without a bass line.
    void ApplyBass(std::vector<std::vector<float> >& buffers, size_t offset, size_t frames, double time, double step);

    const double mOutputRate;
    const size_t mOutputChannels;
    ClipSnapshot mClip;

    std::unique_ptr<RubberBand::RubberBandStretcher> mStretcher;
    int64_t mInputPosition = 0;   // next source sample fed to the stretcher
    size_t mDropOutput = 0;       // start delay still to throw away after a seek
    double mNextTime = -1.0;
    bool mStarted = false;
    bool mAlwaysStretch = false;
    bool mPreferResample = false;
    bool mResample = false;
    void BuildSincTable(double cutoff);
    std::vector<float> mSincTable;   // (phases + 1) x taps
    double mSincCutoff = 0.0;
    double mAppliedRatio = 0.0;
    double mAppliedPitch = 0.0;
    //! Rubber Band's pitch scale for a time ratio (output / input frames).
    double PitchFor(double ratio) const;

    std::vector<std::vector<float> > mInput;    // per clip channel
    std::vector<SourceCursor> mCursors;         // per clip channel: the decoded blocks being read
    std::vector<std::vector<float> > mOutput;   // per clip channel
    std::vector<float*> mInputPtrs;
    std::vector<float*> mOutputPtrs;
    std::vector<double> mEnvelope;
    std::vector<LowBand> mLowBand;   // one crossover per clip channel, only while the clip has a bass line
    std::vector<double> mLowGain;
    std::vector<double> mMidGain;
    std::vector<double> mHighGain;
    std::vector<double> mLowBlend;   // per sample: 0 = the clip as it is, 1 = through the bass stage
    std::vector<float> mBassDry;
    bool mBassWanted = false;        // the clip has a bass line now
    double mBassBlend = 1.0;         // where the blend between the two has got to
};
}
