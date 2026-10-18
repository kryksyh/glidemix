#pragma once

#include "global/async/channel.h"

#include "audio/audiotypes.h"

//! NOTE Implemented in Au3Wrap
namespace au::playback {
class IAudioOutput
{
public:
    virtual ~IAudioOutput() = default;

    virtual float playbackVolume() const = 0;
    virtual void setPlaybackVolume(float volume) = 0;
    virtual muse::async::Channel<float> playbackVolumeChanged() const = 0;

    //! GlideMix: the headphone cue's own level, in dB. The audience
    //! output takes playbackVolume(); this is what the preview plays at while
    //! Perform is running, so the two are set independently as on a DJ mixer.
    virtual float cueVolume() const = 0;
    virtual void setCueVolume(float volume) = 0;
    virtual muse::async::Channel<float> cueVolumeChanged() const = 0;

    virtual audio::sample_rate_t sampleRate() const = 0;
    virtual muse::async::Channel<audio::sample_rate_t> sampleRateChanged() const = 0;

    virtual muse::async::Channel<audio::audioch_t, audio::MeterSignal> playbackSignalChanges() const = 0;
    virtual muse::async::Channel<audio::audioch_t, audio::MeterSignal> playbackTrackSignalChanges(int64_t key) const = 0;

    //! Levels of the Perform (audience) output, metered separately from
    //! playbackSignalChanges() so the audience mix and the headphone cue can
    //! be shown side by side: Perform on the top bar, Cue on the side bar.
    virtual muse::async::Channel<audio::audioch_t, audio::MeterSignal> performSignalChanges() const = 0;
};
}
