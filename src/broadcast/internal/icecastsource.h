/*
 * GlideMix
 */
#pragma once

#include <atomic>
#include <string>
#include <vector>

#include <QTcpSocket>

#include "broadcast/ibroadcastconfiguration.h"

namespace au::broadcast {
/// A source client: what a DJ app is to an Icecast or Shoutcast server. Talks
/// the protocol directly over a socket -- Icecast 2's HTTP PUT, Icecast 1's
/// SOURCE verb, or Shoutcast's password-first handshake (DNAS 1 and 2) -- so
/// the app needs no streaming library of its own.
///
/// Lives on the broadcaster's worker thread and blocks on it; never touch it
/// from the GUI thread.
class IcecastSource
{
public:
    IcecastSource() = default;
    ~IcecastSource();

    /// Watched while connecting: when it goes false the attempt is given up at
    /// once, so stopping the stream does not wait on a server that is not
    /// answering. Set before connectToServer().
    void setKeepGoing(const std::atomic<bool>* keepGoing) { m_keepGoing = keepGoing; }

    /// Connects and gets the server to accept the stream. On failure `error()`
    /// says what the server said.
    bool connectToServer(const BroadcastServer& server);
    void disconnect();
    bool isConnected() const;
    //! Gives the socket a moment to notice the server has gone: without an
    //! event loop on this thread nothing else will tell us until a write fails.
    bool stillConnected();

    /// Sends encoded audio. False means the connection is gone.
    bool send(const unsigned char* data, size_t size);

    /// "Artist - Title" for the listeners' players. Best effort: a server that
    /// refuses metadata does not stop the stream.
    void sendMetadata(const std::string& nowPlaying);

    const std::string& error() const { return m_error; }

private:
    bool openIcecast(const BroadcastServer& server);
    bool openIcecastLegacy(const BroadcastServer& server);
    bool openShoutcast(const BroadcastServer& server);
    //! Reads one HTTP-style response; empty on timeout.
    std::string readResponse(int msecs);
    //! waitForConnected in short steps, so giving up is quick.
    bool waitConnected(int msecs);
    bool keepGoing() const { return !m_keepGoing || m_keepGoing->load(); }

    QTcpSocket m_socket;
    const std::atomic<bool>* m_keepGoing = nullptr;
    BroadcastServer m_server;
    std::string m_error;
    //! The port the handshake actually went to, which is not always the one in
    //! the settings; the metadata connection needs the player port, not this.
    int m_sourcePort = 0;
};
}
