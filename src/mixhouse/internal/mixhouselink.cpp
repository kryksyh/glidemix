/*
 * GlideMix: Ableton Link -- see the header.
 */
#include "mixhouselink.h"

#include <atomic>
#include <cmath>

#include <QSettings>

#include <ableton/Link.hpp>

#include "log.h"

namespace au::mixhouse {
namespace {
MixHouseLink* g_instance = nullptr;
constexpr double kQuantum = 4.0;   // a bar of four beats
const QString kEnabledKey = QStringLiteral("GlideMix/abletonLink");
}

struct MixHouseLink::Impl {
    ableton::Link link { 120.0 };
    std::atomic<int> peers { 0 };
    std::atomic<bool> peersChanged { false };   // an app joined: tell it we're playing
};

MixHouseLink::MixHouseLink(const muse::modularity::ContextPtr& ctx)
    : muse::Injectable(ctx), m_impl(std::make_unique<Impl>())
{
    g_instance = this;
}

MixHouseLink::~MixHouseLink()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
    m_impl->link.enable(false);
}

MixHouseLink* MixHouseLink::instance()
{
    return g_instance;
}

void MixHouseLink::init()
{
    m_impl->link.enableStartStopSync(true);
    m_impl->link.setNumPeersCallback([this](std::size_t peers) {
        m_impl->peers = static_cast<int>(peers);
        m_impl->peersChanged = true;
        // From Link's own thread: tell the interface on the main thread.
        QMetaObject::invokeMethod(this, [this]() { m_changed.notify(); }, Qt::QueuedConnection);
    });
    m_timer.setInterval(20);
    QObject::connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
    if (QSettings().value(kEnabledKey, false).toBool()) {
        setEnabled(true);
    }
}

bool MixHouseLink::enabled() const
{
    return m_impl->link.isEnabled();
}

void MixHouseLink::setEnabled(bool enabled)
{
    if (enabled == this->enabled()) {
        return;
    }
    m_impl->link.enable(enabled);
    QSettings().setValue(kEnabledKey, enabled);
    if (enabled) {
        m_timer.start();
    } else {
        m_timer.stop();
        m_impl->peers = 0;
        m_wasPlaying = false;
    }
    LOGI() << "GlideMix: Ableton Link " << (enabled ? "enabled" : "disabled");
    m_changed.notify();
}

int MixHouseLink::peers() const
{
    return m_impl->peers;
}

void MixHouseLink::tick()
{
    const auto state = globalContext() ? globalContext()->playbackState() : nullptr;
    if (!state || !tempoMap()) {
        return;
    }
    const bool playing = state->playbackStatus() == playback::PlaybackStatus::Running;
    auto session = m_impl->link.captureAppSessionState();
    const auto now = m_impl->link.clock().micros();
    if (playing) {
        // The mix leads: its tempo, and its beat in the bar right now.
        const double position = state->playbackPosition().to_double();
        const double bpm = std::clamp(tempoMap()->bpmAt(position), 20.0, 999.0);
        const double beat = tempoMap()->secToBeat(position);
        bool commit = false;
        if (std::abs(session.tempo() - bpm) > 0.001) {
            session.setTempo(bpm, now);
            commit = true;
        }
        // Only when Link has drifted off our beat (a seek, a tempo glide):
        // forcing it every tick would make other apps' clocks jitter.
        if (commit || std::abs(session.beatAtTime(now, kQuantum) - beat) > 0.01) {
            session.forceBeatAtTime(beat, now, kQuantum);
            commit = true;
        }
        if (!session.isPlaying() || m_impl->peersChanged.exchange(false)) {
            session.setIsPlaying(true, now);
            commit = true;
        }
        if (commit) {
            m_impl->link.commitAppSessionState(session);
        }
    } else if (m_wasPlaying && session.isPlaying()) {
        session.setIsPlaying(false, now);
        m_impl->link.commitAppSessionState(session);
    }
    m_wasPlaying = playing;
}
}
