/*
 * GlideMix
 */
#include "broadcastpreferencesmodel.h"

#include <algorithm>

#include <QGuiApplication>
#include <QClipboard>

using namespace au::preferences;
using namespace au::broadcast;

BroadcastPreferencesModel::BroadcastPreferencesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void BroadcastPreferencesModel::init()
{
    if (broadcaster()) {
        broadcaster()->stateChanged().onNotify(this, [this]() {
            emit changed();
        });
    }
    emit changed();
}

void BroadcastPreferencesModel::reconnect()
{
    if (!broadcaster()) {
        return;
    }
    broadcaster()->stop();
    broadcaster()->start();
}

bool BroadcastPreferencesModel::confirmClose()
{
    if (!broadcaster() || broadcaster()->state() == broadcast::IBroadcaster::State::Off) {
        return true;
    }
    if (!interactive()) {
        return true;
    }

    const muse::IInteractive::Result result = interactive()->warningSync(
        muse::trc("projectscene", "Go off air?"),
        muse::trc("projectscene", "Closing this window takes the stream down. Your listeners will be disconnected."),
        { muse::IInteractive::Button::Cancel, muse::IInteractive::Button::Ok });

    if (result.standardButton() != muse::IInteractive::Button::Ok) {
        return false;
    }
    broadcaster()->stop();
    return true;
}

void BroadcastPreferencesModel::openSettings()
{
    if (!interactive()) {
        return;
    }
    //! Straight to the Live broadcasting page: the operator pressed a button
    //! in the broadcast window, not "Preferences".
    muse::UriQuery preferences("audacity://preferences");
    preferences.addParam("currentPageId", muse::Val("broadcasting"));
    interactive()->open(preferences);
}

void BroadcastPreferencesModel::toggle()
{
    if (broadcaster()) {
        broadcaster()->toggle();
    }
}

BroadcastServer BroadcastPreferencesModel::server() const
{
    return configuration() ? configuration()->server() : BroadcastServer();
}

void BroadcastPreferencesModel::write(const BroadcastServer& server)
{
    if (configuration()) {
        configuration()->setServer(server);
    }
    emit changed();
}

int BroadcastPreferencesModel::protocol() const
{
    //! The same numbers the settings file holds, so the page can compare
    //! against them directly.
    return static_cast<int>(server().protocol);
}

void BroadcastPreferencesModel::setProtocol(int protocol)
{
    BroadcastServer s = server();
    switch (protocol) {
    case 1: s.protocol = BroadcastServer::Protocol::Shoutcast;
        break;
    case 2: s.protocol = BroadcastServer::Protocol::Icecast1;
        break;
    case 3: s.protocol = BroadcastServer::Protocol::Shoutcast2;
        break;
    default: s.protocol = BroadcastServer::Protocol::Icecast2;
        break;
    }
    write(s);
}

QString BroadcastPreferencesModel::host() const
{
    return QString::fromStdString(server().host);
}

void BroadcastPreferencesModel::setHost(const QString& host)
{
    BroadcastServer s = server();
    s.host = host.trimmed().toStdString();
    write(s);
}

int BroadcastPreferencesModel::port() const
{
    return server().port;
}

void BroadcastPreferencesModel::setPort(int port)
{
    BroadcastServer s = server();
    s.port = port;
    write(s);
}

QString BroadcastPreferencesModel::mount() const
{
    return QString::fromStdString(server().mount);
}

void BroadcastPreferencesModel::setMount(const QString& mount)
{
    BroadcastServer s = server();
    s.mount = mount.trimmed().toStdString();
    write(s);
}

QString BroadcastPreferencesModel::login() const
{
    return QString::fromStdString(server().login);
}

void BroadcastPreferencesModel::setLogin(const QString& login)
{
    BroadcastServer s = server();
    s.login = login.trimmed().toStdString();
    write(s);
}

QString BroadcastPreferencesModel::password() const
{
    return QString::fromStdString(server().password);
}

void BroadcastPreferencesModel::setPassword(const QString& password)
{
    BroadcastServer s = server();
    s.password = password.toStdString();
    write(s);
}

int BroadcastPreferencesModel::streamId() const
{
    return server().streamId;
}

void BroadcastPreferencesModel::setStreamId(int streamId)
{
    BroadcastServer s = server();
    s.streamId = std::max(1, streamId);
    write(s);
}

QString BroadcastPreferencesModel::streamName() const
{
    return QString::fromStdString(server().streamName);
}

void BroadcastPreferencesModel::setStreamName(const QString& name)
{
    BroadcastServer s = server();
    s.streamName = name.toStdString();
    write(s);
}

QString BroadcastPreferencesModel::description() const
{
    return QString::fromStdString(server().description);
}

void BroadcastPreferencesModel::setDescription(const QString& description)
{
    BroadcastServer s = server();
    s.description = description.toStdString();
    write(s);
}

QString BroadcastPreferencesModel::genre() const
{
    return QString::fromStdString(server().genre);
}

void BroadcastPreferencesModel::setGenre(const QString& genre)
{
    BroadcastServer s = server();
    s.genre = genre.toStdString();
    write(s);
}

QString BroadcastPreferencesModel::website() const
{
    return QString::fromStdString(server().website);
}

void BroadcastPreferencesModel::setWebsite(const QString& website)
{
    BroadcastServer s = server();
    s.website = website.trimmed().toStdString();
    write(s);
}

bool BroadcastPreferencesModel::publicStream() const
{
    return server().publicStream;
}

void BroadcastPreferencesModel::setPublicStream(bool isPublic)
{
    BroadcastServer s = server();
    s.publicStream = isPublic;
    write(s);
}

int BroadcastPreferencesModel::bitrate() const
{
    return server().bitrateKbps;
}

void BroadcastPreferencesModel::setBitrate(int bitrate)
{
    BroadcastServer s = server();
    s.bitrateKbps = bitrate;
    write(s);
}

int BroadcastPreferencesModel::source() const
{
    return configuration() && configuration()->source() == IBroadcaster::Source::Cue ? 1 : 0;
}

void BroadcastPreferencesModel::setSource(int source)
{
    if (broadcaster()) {
        // Through the broadcaster, so a stream already on air moves with it.
        broadcaster()->setSource(source == 1 ? IBroadcaster::Source::Cue : IBroadcaster::Source::Perform);
    }
    emit changed();
}

bool BroadcastPreferencesModel::sendMetadata() const
{
    return configuration() && configuration()->sendMetadata();
}

void BroadcastPreferencesModel::setSendMetadata(bool send)
{
    if (configuration()) {
        configuration()->setSendMetadata(send);
    }
    emit changed();
}

bool BroadcastPreferencesModel::titleFirst() const
{
    return configuration() && configuration()->titleFirst();
}

void BroadcastPreferencesModel::setTitleFirst(bool titleFirst)
{
    if (configuration()) {
        configuration()->setTitleFirst(titleFirst);
    }
    emit changed();
}

int BroadcastPreferencesModel::state() const
{
    if (!broadcaster()) {
        return 0;
    }
    switch (broadcaster()->state()) {
    case IBroadcaster::State::Off: return 0;
    case IBroadcaster::State::Connecting: return 1;
    case IBroadcaster::State::Live: return 2;
    case IBroadcaster::State::Failed: return 3;
    }
    return 0;
}

QString BroadcastPreferencesModel::statusText() const
{
    return broadcaster() ? QString::fromStdString(broadcaster()->statusText()) : QString();
}

QString BroadcastPreferencesModel::listenUrl() const
{
    const QString address = host().trimmed();
    if (address.isEmpty() || port() <= 0) {
        return QString();
    }

    //! Where a player finds the audio, which differs per server: Icecast
    //! serves each stream at its mount, DNAS 1 serves its one stream at the
    //! root, and DNAS 2 serves stream <sid> at /stream/<sid>/. Always the
    //! player port, never the source port the app connects on.
    QString path;
    switch (protocol()) {
    case 1:   // Shoutcast 1
        break;
    case 3:   // Shoutcast 2
        path = QString("/stream/%1/").arg(qMax(1, streamId()));
        break;
    default:  // Icecast 1 and 2
        path = mount().trimmed();
        if (path.isEmpty()) {
            path = "/stream";
        }
        if (!path.startsWith('/')) {
            path.prepend('/');
        }
        break;
    }

    return QString("http://%1:%2%3").arg(address).arg(port()).arg(path);
}

void BroadcastPreferencesModel::copyListenUrl()
{
    const QString url = listenUrl();
    if (url.isEmpty()) {
        return;
    }
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(url);
    }
}
