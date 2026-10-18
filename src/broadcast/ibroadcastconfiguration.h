/*
 * GlideMix
 */
#pragma once

#include <string>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

#include "ibroadcaster.h"

namespace au::broadcast {
/// What the Live broadcasting preferences page edits: one server to stream to.
struct BroadcastServer {
    //! The numbers are what gets written to the settings file, so they stay
    //! put; the preferences page decides the order they are shown in.
    enum class Protocol {
        /// Icecast 2.4 and later: the stream goes up as an HTTP PUT to a mount
        /// point, with the older SOURCE verb as a fallback for 2.3 and the KH
        /// branch.
        Icecast2 = 0,
        /// Shoutcast DNAS 1: password first, one stream per server, source port
        /// one above the player port.
        Shoutcast = 1,
        /// Icecast 1 and the xaudiocast servers: the SOURCE verb over ICE/1.0,
        /// no PUT, no HTTP Basic.
        Icecast1 = 2,
        /// Shoutcast DNAS 2: the same handshake as DNAS 1, but a server can
        /// carry several streams, so the stream id and an optional DJ name ride
        /// along in the password field -- which is how DNAS 2 asks for them.
        Shoutcast2 = 3
    };

    Protocol protocol = Protocol::Icecast2;
    std::string host;
    int port = 8000;
    /// Icecast only; a Shoutcast server has one stream per stream id.
    std::string mount = "/stream";
    /// Icecast's source user, or the DJ name on Shoutcast 2 (optional there).
    std::string login = "source";
    std::string password;
    /// Shoutcast 2 only: which of the server's streams this is. 1 unless the
    /// host says otherwise.
    int streamId = 1;

    std::string streamName;
    std::string description;
    std::string genre;
    std::string website;
    bool publicStream = false;

    /// MP3 only for now -- the encoder the app already ships.
    int bitrateKbps = 128;
    int sampleRate = 44100;
    int channels = 2;

    bool isShoutcast() const { return protocol == Protocol::Shoutcast || protocol == Protocol::Shoutcast2; }
    bool isIcecast() const { return protocol == Protocol::Icecast1 || protocol == Protocol::Icecast2; }

    bool valid() const { return !host.empty() && port > 0 && !password.empty(); }
};

class IBroadcastConfiguration : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IBroadcastConfiguration)

public:
    virtual ~IBroadcastConfiguration() = default;

    virtual BroadcastServer server() const = 0;
    virtual void setServer(const BroadcastServer& server) = 0;

    /// Which output feeds the stream.
    virtual IBroadcaster::Source source() const = 0;
    virtual void setSource(IBroadcaster::Source source) = 0;

    /// The stream's own level in dB, kept between sessions.
    virtual float gainDb() const = 0;
    virtual void setGainDb(float db) = 0;

    /// Go back on air by itself when the server drops the connection --
    /// BUTT's behaviour, and what anyone running a show expects.
    virtual bool reconnect() const = 0;
    virtual void setReconnect(bool reconnect) = 0;
    /// Seconds between attempts.
    virtual int reconnectDelaySec() const = 0;
    virtual void setReconnectDelaySec(int seconds) = 0;

    /// Send the playing track's title and artist to the server as it changes.
    virtual bool sendMetadata() const = 0;
    virtual void setSendMetadata(bool send) = 0;
    //! GlideMix: the order the title goes out in -- false "Artist - Title"
    //! (the radio norm), true "Title - Artist".
    virtual bool titleFirst() const = 0;
    virtual void setTitleFirst(bool titleFirst) = 0;

    virtual muse::async::Notification changed() const = 0;
};
}
