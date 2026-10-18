/*
 * GlideMix
 */
#pragma once

#include <string>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

namespace au::broadcast {
/// Streams what is playing to an Icecast or Shoutcast server: the radio
/// tower in the header. One stream at a time, fed by one of the two outputs
/// the app already has.
class IBroadcaster : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IBroadcaster)

public:
    virtual ~IBroadcaster() = default;

    /// Which output the listeners hear: what the audience hears (Perform), or
    /// what is in the headphones (Cue). The header button says which.
    enum class Source {
        Perform,
        Cue
    };

    enum class State {
        Off,
        Connecting,
        Live,
        Failed
    };

    virtual State state() const = 0;
    /// Where it is streaming to, or why it stopped -- the button's tooltip.
    virtual std::string statusText() const = 0;
    /// Seconds on air (0 when not live).
    virtual int secondsOnAir() const = 0;

    virtual Source source() const = 0;
    virtual void setSource(Source source) = 0;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void toggle() = 0;

    /// The level of the stream itself, in dB: what the listeners get, set
    /// apart from the app's own outputs. 0 = as it comes out of the mix.
    virtual float gainDb() const = 0;
    virtual void setGainDb(float db) = 0;

    /// Peak of what was last handed to the encoder, dBFS (-inf as -100),
    /// channel 0 or 1. Falls back like a meter so it can be read at any rate.
    virtual float peakDbfs(int channel) const = 0;

    virtual muse::async::Notification stateChanged() const = 0;

    //! The song playing on the streamed output, sent to the server when
    //! "Send the playing track's name" is on. Set from the main thread.
    virtual void setNowPlaying(const std::string& title) = 0;
};
}
