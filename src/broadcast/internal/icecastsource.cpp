/*
 * GlideMix
 */
#include "icecastsource.h"

#include <algorithm>
#include <string>

#include <QByteArray>
#include <QString>
#include <QUrl>

#include "log.h"

using namespace au::broadcast;

namespace {
constexpr int kConnectTimeoutMs = 6000;
//! Long enough not to thrash, short enough that stopping feels immediate.
constexpr int kConnectStepMs = 150;
constexpr int kResponseTimeoutMs = 8000;
constexpr int kWriteTimeoutMs = 4000;

QByteArray basicAuth(const std::string& user, const std::string& password)
{
    const QByteArray raw = QByteArray::fromStdString(user + ":" + password);
    return "Basic " + raw.toBase64();
}

//! Shoutcast listens for players on the configured port and for sources on the
//! one above it; Icecast uses a single port for both. Every other broadcaster
//! (BUTT, the Shoutcast DSP) does this same +1, which is why control panels
//! only ever quote the player port. DNAS 2.6 will also take a source on the
//! player port, which is what the fallback below is for.
int sourcePortOf(const BroadcastServer& server)
{
    return server.isShoutcast() ? server.port + 1 : server.port;
}

//! DNAS 2 carries several streams on one server and the v1 handshake has no
//! field for saying which, so the stream id is appended to the password --
//! "<password>:#<sid>", or "<dj>:<password>:#<sid>" when a DJ name is set.
//! That is the form the DNAS 2 documentation asks source clients to send.
std::string shoutcastPassword(const BroadcastServer& server)
{
    if (server.protocol != BroadcastServer::Protocol::Shoutcast2) {
        return server.password;
    }

    std::string password = server.password;
    if (!server.login.empty() && server.login != "source") {
        password = server.login + ":" + password;
    }
    return password + ":#" + std::to_string(std::max(1, server.streamId));
}

QString mountOf(const BroadcastServer& server)
{
    QString mount = QString::fromStdString(server.mount);
    if (mount.isEmpty()) {
        mount = "/stream";
    }
    if (!mount.startsWith('/')) {
        mount.prepend('/');
    }
    return mount;
}
}

IcecastSource::~IcecastSource()
{
    disconnect();
}

bool IcecastSource::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

bool IcecastSource::waitConnected(int msecs)
{
    for (int waited = 0; waited < msecs; waited += kConnectStepMs) {
        if (!keepGoing()) {
            m_error = "stopped";
            return false;
        }
        if (m_socket.waitForConnected(kConnectStepMs)) {
            return true;
        }
        if (m_socket.state() == QAbstractSocket::UnconnectedState) {
            return false;   // refused outright; no point waiting out the timeout
        }
    }
    return false;
}

bool IcecastSource::stillConnected()
{
    if (!isConnected()) {
        return false;
    }
    // Reading is what surfaces a closed peer; a server that has gone away
    // leaves the socket in Unconnected after this.
    m_socket.waitForReadyRead(1);
    if (m_socket.bytesAvailable() > 0) {
        // Icecast says nothing mid-stream, but drain rather than let it sit.
        m_socket.readAll();
    }
    return isConnected();
}

std::string IcecastSource::readResponse(int msecs)
{
    if (!m_socket.waitForReadyRead(msecs)) {
        return {};
    }
    return m_socket.readAll().toStdString();
}

bool IcecastSource::connectToServer(const BroadcastServer& server)
{
    disconnect();
    m_error.clear();
    m_server = server;

    if (!server.valid()) {
        m_error = "the server, port and password have to be filled in";
        return false;
    }

    //! Shoutcast wants the source port, one above the player port. DNAS 2.6
    //! will also take a source on the player port itself, and some hosts only
    //! open that one, so a refusal is worth a second try there before giving up.
    std::vector<int> ports { sourcePortOf(server) };
    if (server.isShoutcast() && ports.front() != server.port) {
        ports.push_back(server.port);
    }

    for (size_t attempt = 0; attempt < ports.size(); ++attempt) {
        const int port = ports[attempt];
        if (attempt > 0) {
            disconnect();
        }
        m_socket.connectToHost(QString::fromStdString(server.host), static_cast<quint16>(port));
        if (!waitConnected(kConnectTimeoutMs)) {
            if (m_error == "stopped") {
                return false;
            }
            m_error = "cannot reach " + server.host + ":" + std::to_string(port)
                      + " (" + m_socket.errorString().toStdString() + ")";
            continue;
        }

        m_sourcePort = port;
        const bool ok = server.isShoutcast() ? openShoutcast(server) : openIcecast(server);
        if (ok) {
            return true;
        }
        m_socket.abort();
        //! A password the server rejected will be rejected on the other port
        //! too; only a connection that went nowhere is worth retrying.
        if (m_error.find("refused the password") != std::string::npos) {
            return false;
        }
    }

    return false;
}

//! Icecast 1 / xaudiocast: SOURCE over ICE/1.0, no HTTP, no Basic auth, the
//! password on its own header line.
bool IcecastSource::openIcecastLegacy(const BroadcastServer& server)
{
    const QString mount = mountOf(server);

    QByteArray request;
    request += "SOURCE " + QByteArray::fromStdString(server.password) + " " + mount.toUtf8() + "\n";
    request += "x-audiocast-mount: " + mount.toUtf8() + "\n";
    request += "x-audiocast-name: " + QByteArray::fromStdString(server.streamName) + "\n";
    request += "x-audiocast-description: " + QByteArray::fromStdString(server.description) + "\n";
    request += "x-audiocast-genre: " + QByteArray::fromStdString(server.genre) + "\n";
    request += "x-audiocast-url: " + QByteArray::fromStdString(server.website) + "\n";
    request += "x-audiocast-bitrate: " + QByteArray::number(server.bitrateKbps) + "\n";
    request += "x-audiocast-public: " + QByteArray(server.publicStream ? "1" : "0") + "\n";
    request += "\n";

    m_socket.write(request);
    if (!m_socket.waitForBytesWritten(kWriteTimeoutMs)) {
        m_error = "the server accepted the connection but not the request";
        return false;
    }

    const std::string response = readResponse(kResponseTimeoutMs);
    if (response.find("OK") != std::string::npos || response.find(" 200") != std::string::npos) {
        return true;
    }
    if (response.empty()) {
        m_error = "no answer from " + server.host + ":" + std::to_string(m_sourcePort);
        return false;
    }
    if (response.find("uthent") != std::string::npos || response.find(" 401") != std::string::npos) {
        m_error = "the server refused the password";
        return false;
    }
    m_error = response.substr(0, response.find_first_of("\r\n"));
    return false;
}

bool IcecastSource::openIcecast(const BroadcastServer& server)
{
    if (server.protocol == BroadcastServer::Protocol::Icecast1) {
        return openIcecastLegacy(server);
    }

    const QString mount = mountOf(server);
    QByteArray request;
    // Icecast 2.4 and later take the stream as an HTTP PUT; the 100-continue
    // is what tells us the mount point and password were accepted before a
    // single frame of audio is sent.
    request += "PUT " + mount.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: " + QByteArray::fromStdString(server.host) + ":" + QByteArray::number(server.port) + "\r\n";
    request += "Authorization: " + basicAuth(server.login.empty() ? "source" : server.login, server.password) + "\r\n";
    request += "User-Agent: MixtapeStudio\r\n";
    request += "Content-Type: audio/mpeg\r\n";
    request += "Ice-Public: " + QByteArray(server.publicStream ? "1" : "0") + "\r\n";
    if (!server.streamName.empty()) {
        request += "Ice-Name: " + QByteArray::fromStdString(server.streamName) + "\r\n";
    }
    if (!server.description.empty()) {
        request += "Ice-Description: " + QByteArray::fromStdString(server.description) + "\r\n";
    }
    if (!server.genre.empty()) {
        request += "Ice-Genre: " + QByteArray::fromStdString(server.genre) + "\r\n";
    }
    if (!server.website.empty()) {
        request += "Ice-URL: " + QByteArray::fromStdString(server.website) + "\r\n";
    }
    request += "Ice-Audio-Info: bitrate=" + QByteArray::number(server.bitrateKbps)
               + ";channels=" + QByteArray::number(server.channels)
               + ";samplerate=" + QByteArray::number(server.sampleRate) + "\r\n";
    request += "Expect: 100-continue\r\n\r\n";

    m_socket.write(request);
    if (!m_socket.waitForBytesWritten(kWriteTimeoutMs)) {
        m_error = "the server accepted the connection but not the request";
        return false;
    }

    const std::string response = readResponse(kResponseTimeoutMs);
    if (response.empty()) {
        m_error = "no answer from " + server.host;
        return false;
    }
    if (response.find(" 100") != std::string::npos || response.find(" 200") != std::string::npos) {
        return true;
    }
    if (response.find(" 401") != std::string::npos) {
        m_error = "the server refused the password";
        return false;
    }
    if (response.find(" 403") != std::string::npos) {
        m_error = "the mount point " + mount.toStdString() + " is already in use";
        return false;
    }

    // Icecast 2.3 and the KH branch only know the older SOURCE verb.
    QByteArray legacy;
    legacy += "SOURCE " + mount.toUtf8() + " ICE/1.0\r\n";
    legacy += "Authorization: " + basicAuth(server.login.empty() ? "source" : server.login, server.password) + "\r\n";
    legacy += "Content-Type: audio/mpeg\r\n";
    legacy += "ice-name: " + QByteArray::fromStdString(server.streamName) + "\r\n";
    legacy += "ice-public: " + QByteArray(server.publicStream ? "1" : "0") + "\r\n";
    legacy += "ice-bitrate: " + QByteArray::number(server.bitrateKbps) + "\r\n\r\n";

    m_socket.abort();
    if (!keepGoing()) {
        m_error = "stopped";
        return false;
    }
    m_socket.connectToHost(QString::fromStdString(server.host), static_cast<quint16>(m_sourcePort));
    if (!waitConnected(kConnectTimeoutMs)) {
        m_error = "cannot reach " + server.host;
        return false;
    }
    m_socket.write(legacy);
    m_socket.waitForBytesWritten(kWriteTimeoutMs);
    const std::string legacyResponse = readResponse(kResponseTimeoutMs);
    if (legacyResponse.find(" 200") != std::string::npos || legacyResponse.find("OK") != std::string::npos) {
        return true;
    }

    const std::string first = response.substr(0, response.find('\r'));
    m_error = first.empty() ? std::string("the server refused the stream") : first;
    return false;
}

bool IcecastSource::openShoutcast(const BroadcastServer& server)
{
    // Shoutcast 1: the password comes first, on its own line, before any
    // headers. The socket is already on the source port (see sourcePortOf).
    QByteArray request;
    request += QByteArray::fromStdString(shoutcastPassword(server)) + "\n";
    request += "icy-name:" + QByteArray::fromStdString(server.streamName) + "\n";
    request += "icy-genre:" + QByteArray::fromStdString(server.genre) + "\n";
    request += "icy-url:" + QByteArray::fromStdString(server.website) + "\n";
    request += "icy-pub:" + QByteArray(server.publicStream ? "1" : "0") + "\n";
    request += "icy-br:" + QByteArray::number(server.bitrateKbps) + "\n";
    request += "content-type:audio/mpeg\n\n";

    m_socket.write(request);
    if (!m_socket.waitForBytesWritten(kWriteTimeoutMs)) {
        m_error = "the server accepted the connection but not the request";
        return false;
    }
    const std::string response = readResponse(kResponseTimeoutMs);
    //! DNAS v1 answers "OK2" and v2 "OK"; both mean the password was taken.
    if (response.rfind("OK", 0) == 0 || response.find("\nOK") != std::string::npos) {
        return true;
    }
    if (response.find("invalid password") != std::string::npos) {
        m_error = "the server refused the password (use the stream password, not the account one)";
        return false;
    }
    if (response.empty()) {
        m_error = "no answer from " + server.host + ":" + std::to_string(m_sourcePort);
        return false;
    }
    m_error = response.substr(0, response.find_first_of("\r\n"));
    return false;
}

void IcecastSource::disconnect()
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.disconnectFromHost();
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_socket.waitForDisconnected(1000);
        }
    }
    m_socket.abort();
}

bool IcecastSource::send(const unsigned char* data, size_t size)
{
    if (size == 0) {
        return isConnected();
    }
    if (!isConnected()) {
        m_error = "the server closed the connection";
        return false;
    }
    const qint64 written = m_socket.write(reinterpret_cast<const char*>(data), static_cast<qint64>(size));
    if (written < 0) {
        m_error = m_socket.errorString().toStdString();
        return false;
    }
    // The socket buffers; this only blocks when the server is slower than the
    // stream, which is the one case worth noticing.
    if (m_socket.bytesToWrite() > 0 && !m_socket.waitForBytesWritten(kWriteTimeoutMs)) {
        m_error = "the server stopped reading the stream";
        return false;
    }
    return true;
}

void IcecastSource::sendMetadata(const std::string& nowPlaying)
{
    if (nowPlaying.empty() || m_server.host.empty()) {
        return;
    }

    // Metadata goes over its own short-lived connection, so a server that
    // refuses it cannot take the stream down with it.
    QTcpSocket admin;
    //! The admin endpoint lives on the player port for both protocols, which is
    //! the port as configured - only the source connection is offset.
    admin.connectToHost(QString::fromStdString(m_server.host), static_cast<quint16>(m_server.port));
    if (!admin.waitForConnected(2000)) {
        return;
    }

    // Non-breaking spaces become plain ones. SHOUTcast 1 reads the title as
    // Latin-1: UTF-8 there showed a non-breaking space as "Â ". SHOUTcast 2
    // and Icecast take UTF-8.
    const QString title = QString::fromStdString(nowPlaying).replace(QChar(0x00A0), QLatin1Char(' '));
    const QByteArray song = (m_server.protocol == BroadcastServer::Protocol::Shoutcast ? title.toLatin1()
                                                                                        : title.toUtf8()).toPercentEncoding();
    QByteArray request;
    if (m_server.isShoutcast()) {
        request += "GET /admin.cgi?pass=" + QByteArray::fromStdString(m_server.password)
                   + "&mode=updinfo&song=" + song;
        //! DNAS 2 needs to be told which of its streams the title belongs to.
        if (m_server.protocol == BroadcastServer::Protocol::Shoutcast2) {
            request += "&sid=" + QByteArray::number(std::max(1, m_server.streamId));
        }
        request += " HTTP/1.0\r\n";
        request += "User-Agent: MixtapeStudio (Mozilla compatible)\r\n\r\n";
    } else {
        request += "GET /admin/metadata?mode=updinfo&mount=" + mountOf(m_server).toUtf8()
                   + "&song=" + song + " HTTP/1.0\r\n";
        request += "Host: " + QByteArray::fromStdString(m_server.host) + "\r\n";
        request += "Authorization: " + basicAuth(m_server.login.empty() ? "source" : m_server.login, m_server.password) + "\r\n";
        request += "User-Agent: MixtapeStudio\r\n\r\n";
    }
    admin.write(request);
    admin.waitForBytesWritten(1000);
    admin.waitForReadyRead(1000);
    // The server's answer, so a refused title (a wrong admin password, an
    // unknown mount) can be told apart from one never sent.
    const QByteArray reply = admin.readLine().trimmed();
    LOGI() << "Broadcast: now playing \"" << nowPlaying << "\" -> " << reply.toStdString();
    admin.abort();
}
