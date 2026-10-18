/*
 * GlideMix
 */
#include "broadcastconfiguration.h"

#include <algorithm>

#include "global/settings.h"

using namespace au::broadcast;

static const std::string module_name("broadcast");

static const muse::Settings::Key PROTOCOL(module_name, "broadcast/server/protocol");
static const muse::Settings::Key HOST(module_name, "broadcast/server/host");
static const muse::Settings::Key PORT(module_name, "broadcast/server/port");
static const muse::Settings::Key MOUNT(module_name, "broadcast/server/mount");
static const muse::Settings::Key LOGIN(module_name, "broadcast/server/login");
static const muse::Settings::Key PASSWORD(module_name, "broadcast/server/password");
static const muse::Settings::Key STREAM_NAME(module_name, "broadcast/stream/name");
static const muse::Settings::Key DESCRIPTION(module_name, "broadcast/stream/description");
static const muse::Settings::Key GENRE(module_name, "broadcast/stream/genre");
static const muse::Settings::Key WEBSITE(module_name, "broadcast/stream/website");
static const muse::Settings::Key PUBLIC(module_name, "broadcast/stream/public");
static const muse::Settings::Key BITRATE(module_name, "broadcast/stream/bitrate");
static const muse::Settings::Key SOURCE(module_name, "broadcast/source");
static const muse::Settings::Key SEND_METADATA(module_name, "broadcast/sendMetadata");
static const muse::Settings::Key TITLE_FIRST(module_name, "broadcast/titleFirst");
static const muse::Settings::Key GAIN_DB(module_name, "broadcast/stream/gainDb");
static const muse::Settings::Key RECONNECT(module_name, "broadcast/reconnect/enabled");
static const muse::Settings::Key RECONNECT_DELAY(module_name, "broadcast/reconnect/delaySec");
static const muse::Settings::Key STREAM_ID(module_name, "broadcast/server/streamId");

void BroadcastConfiguration::init()
{
    muse::settings()->setDefaultValue(PROTOCOL, muse::Val(0));   // Icecast 2
    muse::settings()->setDefaultValue(HOST, muse::Val(std::string()));
    muse::settings()->setDefaultValue(PORT, muse::Val(8000));
    muse::settings()->setDefaultValue(MOUNT, muse::Val(std::string("/stream")));
    muse::settings()->setDefaultValue(LOGIN, muse::Val(std::string("source")));
    muse::settings()->setDefaultValue(PASSWORD, muse::Val(std::string()));
    muse::settings()->setDefaultValue(STREAM_NAME, muse::Val(std::string()));
    muse::settings()->setDefaultValue(DESCRIPTION, muse::Val(std::string()));
    muse::settings()->setDefaultValue(GENRE, muse::Val(std::string()));
    muse::settings()->setDefaultValue(WEBSITE, muse::Val(std::string()));
    muse::settings()->setDefaultValue(PUBLIC, muse::Val(false));
    muse::settings()->setDefaultValue(BITRATE, muse::Val(128));
    //! The audience's output, as on a DJ mixer: the headphones are for cueing.
    muse::settings()->setDefaultValue(SOURCE, muse::Val(0));
    muse::settings()->setDefaultValue(SEND_METADATA, muse::Val(true));
    muse::settings()->setDefaultValue(TITLE_FIRST, muse::Val(false));
    muse::settings()->setDefaultValue(GAIN_DB, muse::Val(0.0));
    muse::settings()->setDefaultValue(RECONNECT, muse::Val(true));
    muse::settings()->setDefaultValue(RECONNECT_DELAY, muse::Val(5));
    //! Shoutcast 2 numbers its streams from 1, and a server with one stream
    //! still calls it stream 1.
    muse::settings()->setDefaultValue(STREAM_ID, muse::Val(1));

    for (const muse::Settings::Key& key : { PROTOCOL, HOST, PORT, MOUNT, LOGIN, PASSWORD, STREAM_NAME,
                                            DESCRIPTION, GENRE, WEBSITE, PUBLIC, BITRATE, SOURCE, SEND_METADATA, TITLE_FIRST,
                                            GAIN_DB, RECONNECT, RECONNECT_DELAY, STREAM_ID }) {
        muse::settings()->valueChanged(key).onReceive(nullptr, [this](const muse::Val&) {
            m_changed.notify();
        });
    }
}

BroadcastServer BroadcastConfiguration::server() const
{
    BroadcastServer s;
    const int protocol = muse::settings()->value(PROTOCOL).toInt();
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
    s.host = muse::settings()->value(HOST).toString();
    s.port = muse::settings()->value(PORT).toInt();
    s.mount = muse::settings()->value(MOUNT).toString();
    s.login = muse::settings()->value(LOGIN).toString();
    s.password = muse::settings()->value(PASSWORD).toString();
    s.streamName = muse::settings()->value(STREAM_NAME).toString();
    s.description = muse::settings()->value(DESCRIPTION).toString();
    s.genre = muse::settings()->value(GENRE).toString();
    s.website = muse::settings()->value(WEBSITE).toString();
    s.publicStream = muse::settings()->value(PUBLIC).toBool();
    s.bitrateKbps = muse::settings()->value(BITRATE).toInt();
    s.streamId = std::max(1, muse::settings()->value(STREAM_ID).toInt());
    return s;
}

void BroadcastConfiguration::setServer(const BroadcastServer& s)
{
    muse::settings()->setSharedValue(PROTOCOL, muse::Val(static_cast<int>(s.protocol)));
    muse::settings()->setSharedValue(HOST, muse::Val(s.host));
    muse::settings()->setSharedValue(PORT, muse::Val(s.port));
    muse::settings()->setSharedValue(MOUNT, muse::Val(s.mount));
    muse::settings()->setSharedValue(LOGIN, muse::Val(s.login));
    muse::settings()->setSharedValue(PASSWORD, muse::Val(s.password));
    muse::settings()->setSharedValue(STREAM_NAME, muse::Val(s.streamName));
    muse::settings()->setSharedValue(DESCRIPTION, muse::Val(s.description));
    muse::settings()->setSharedValue(GENRE, muse::Val(s.genre));
    muse::settings()->setSharedValue(WEBSITE, muse::Val(s.website));
    muse::settings()->setSharedValue(PUBLIC, muse::Val(s.publicStream));
    muse::settings()->setSharedValue(BITRATE, muse::Val(s.bitrateKbps));
    muse::settings()->setSharedValue(STREAM_ID, muse::Val(s.streamId));
}

au::broadcast::IBroadcaster::Source BroadcastConfiguration::source() const
{
    return muse::settings()->value(SOURCE).toInt() == 1 ? IBroadcaster::Source::Cue : IBroadcaster::Source::Perform;
}

void BroadcastConfiguration::setSource(IBroadcaster::Source source)
{
    muse::settings()->setSharedValue(SOURCE, muse::Val(source == IBroadcaster::Source::Cue ? 1 : 0));
}

bool BroadcastConfiguration::sendMetadata() const
{
    return muse::settings()->value(SEND_METADATA).toBool();
}

void BroadcastConfiguration::setSendMetadata(bool send)
{
    muse::settings()->setSharedValue(SEND_METADATA, muse::Val(send));
}

bool BroadcastConfiguration::titleFirst() const
{
    return muse::settings()->value(TITLE_FIRST).toBool();
}

void BroadcastConfiguration::setTitleFirst(bool titleFirst)
{
    muse::settings()->setSharedValue(TITLE_FIRST, muse::Val(titleFirst));
}

muse::async::Notification BroadcastConfiguration::changed() const
{
    return m_changed;
}

float BroadcastConfiguration::gainDb() const
{
    return static_cast<float>(muse::settings()->value(GAIN_DB).toDouble());
}

void BroadcastConfiguration::setGainDb(float db)
{
    muse::settings()->setSharedValue(GAIN_DB, muse::Val(static_cast<double>(db)));
}

bool BroadcastConfiguration::reconnect() const
{
    return muse::settings()->value(RECONNECT).toBool();
}

void BroadcastConfiguration::setReconnect(bool reconnect)
{
    muse::settings()->setSharedValue(RECONNECT, muse::Val(reconnect));
}

int BroadcastConfiguration::reconnectDelaySec() const
{
    return std::clamp(muse::settings()->value(RECONNECT_DELAY).toInt(), 1, 120);
}

void BroadcastConfiguration::setReconnectDelaySec(int seconds)
{
    muse::settings()->setSharedValue(RECONNECT_DELAY, muse::Val(std::clamp(seconds, 1, 120)));
}
