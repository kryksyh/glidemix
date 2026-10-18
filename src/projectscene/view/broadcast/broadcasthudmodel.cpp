/*
 * GlideMix
 */
#include "broadcasthudmodel.h"

#include <algorithm>

#include <QTimer>

#include "translation.h"

using namespace au::projectscene;
using namespace au::broadcast;

BroadcastHudModel::BroadcastHudModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void BroadcastHudModel::init()
{
    if (broadcaster()) {
        broadcaster()->stateChanged().onNotify(this, [this]() {
            emit changed();
        });
    }
    // The time on air ticks in this window even when nothing else changes.
    if (!m_tick) {
        m_tick = new QTimer(this);
        // Fast enough for meters while on air; the window is small and idle
        // the rest of the time.
        m_tick->setInterval(80);
        connect(m_tick, &QTimer::timeout, this, [this]() {
            if (state() != 0) {
                emit changed();
            }
        });
        m_tick->start();
    }
    emit changed();
}

void BroadcastHudModel::toggle()
{
    if (broadcaster()) {
        broadcaster()->toggle();
    }
}

void BroadcastHudModel::reconnect()
{
    if (!broadcaster()) {
        return;
    }
    broadcaster()->stop();
    broadcaster()->start();
}

bool BroadcastHudModel::confirmClose()
{
    if (!broadcaster() || broadcaster()->state() == IBroadcaster::State::Off || !interactive()) {
        return true;
    }

    const muse::IInteractive::Result result = interactive()->warningSync(
        muse::trc("projectscene", "Go off air?"),
        muse::trc("projectscene", "Closing this window stops the stream. Your listeners will be disconnected."),
        { muse::IInteractive::Button::Cancel, muse::IInteractive::Button::Ok });

    if (result.standardButton() != muse::IInteractive::Button::Ok) {
        return false;
    }
    broadcaster()->stop();
    return true;
}

void BroadcastHudModel::openSettings()
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

int BroadcastHudModel::state() const
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

QString BroadcastHudModel::statusText() const
{
    return broadcaster() ? QString::fromStdString(broadcaster()->statusText()) : QString();
}

int BroadcastHudModel::secondsOnAir() const
{
    return broadcaster() ? broadcaster()->secondsOnAir() : 0;
}

int BroadcastHudModel::source() const
{
    return broadcaster() && broadcaster()->source() == IBroadcaster::Source::Cue ? 1 : 0;
}

void BroadcastHudModel::setSource(int source)
{
    if (broadcaster()) {
        broadcaster()->setSource(source == 1 ? IBroadcaster::Source::Cue : IBroadcaster::Source::Perform);
    }
    emit changed();
}

QString BroadcastHudModel::serverSummary() const
{
    if (!configuration()) {
        return {};
    }
    const BroadcastServer server = configuration()->server();
    if (server.host.empty()) {
        return {};
    }
    QString summary = QString::fromStdString(server.host) + ":" + QString::number(server.port);
    if (server.isIcecast()) {
        summary += QString::fromStdString(server.mount);
    } else if (server.protocol == BroadcastServer::Protocol::Shoutcast2) {
        summary += "  ·  " + muse::qtrc("projectscene", "stream %1").arg(std::max(1, server.streamId));
    }
    summary += "  ·  " + QString::number(server.bitrateKbps) + " kbps";
    return summary;
}

double BroadcastHudModel::peakLeft() const
{
    return broadcaster() ? broadcaster()->peakDbfs(0) : -100.0;
}

double BroadcastHudModel::peakRight() const
{
    return broadcaster() ? broadcaster()->peakDbfs(1) : -100.0;
}

double BroadcastHudModel::gainDb() const
{
    return broadcaster() ? broadcaster()->gainDb() : 0.0;
}

void BroadcastHudModel::setGainDb(double db)
{
    if (broadcaster()) {
        broadcaster()->setGainDb(static_cast<float>(db));
    }
    emit changed();
}

bool BroadcastHudModel::autoReconnect() const
{
    return configuration() && configuration()->reconnect();
}

void BroadcastHudModel::setAutoReconnect(bool on)
{
    if (configuration()) {
        configuration()->setReconnect(on);
    }
    emit changed();
}
