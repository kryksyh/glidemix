/*
 * GlideMix
 */
#include "broadcaster.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>

#include <QDateTime>

#include "au3-mixtape-engine/MixtapeRenderer.h"

#include "icecastsource.h"
#include "mp3encoder.h"

#include "async/async.h"
#include "log.h"

using namespace au::broadcast;

namespace {
//! Four seconds of stereo at 48 kHz: enough that a slow server or a stalled
//! connection attempt cannot cost the listeners a gap, small enough that the
//! stream stays close to live.
constexpr size_t kRingFrames = 48000 * 4;
constexpr size_t kChannels = 2;
//! Encode in blocks of about a tenth of a second.
constexpr size_t kEncodeFrames = 4096;

int64_t nowSeconds()
{
    return QDateTime::currentSecsSinceEpoch();
}
}

Broadcaster::~Broadcaster()
{
    stop();
}

void Broadcaster::init()
{
    m_ring.assign(kRingFrames * kChannels, 0.0f);
}

IBroadcaster::State Broadcaster::state() const
{
    return m_state.load();
}

std::string Broadcaster::statusText() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

int Broadcaster::secondsOnAir() const
{
    const int64_t since = m_liveSince.load();
    return since > 0 ? static_cast<int>(nowSeconds() - since) : 0;
}

IBroadcaster::Source Broadcaster::source() const
{
    return configuration()->source();
}

void Broadcaster::setSource(Source source)
{
    if (source == configuration()->source()) {
        return;
    }
    configuration()->setSource(source);
    // Mid-stream the listeners would hear the other output cut in, so the
    // stream restarts on the one just chosen.
    if (m_running.load()) {
        stop();
        start();
    } else {
        m_stateChanged.notify();
    }
}

void Broadcaster::setState(State state, const std::string& status)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = status;
    }
    m_state.store(state);
    m_liveSince.store(state == State::Live ? nowSeconds() : 0);
    // The engine's thread and ours both get here; the UI listens on the GUI one.
    muse::async::Async::call(this, [this]() {
        m_stateChanged.notify();
    });
}

void Broadcaster::takeBlock(bool isCue, const float* const* out, size_t channels, size_t frames, double sampleRate)
{
    // Audio thread: no settings, no locks, no allocation.
    const bool wanted = m_streamCue.load() == isCue;
    if (!wanted || frames == 0 || channels == 0) {
        return;
    }
    m_sampleRate.store(sampleRate);

    const size_t capacity = kRingFrames;
    const size_t used = m_available.load();
    if (used + frames > capacity) {
        // The worker is behind (a stalled server): drop rather than block the
        // audio thread. The listeners hear a gap; the music does not stutter.
        m_dropped.fetch_add(frames);
        return;
    }

    for (size_t i = 0; i < frames; ++i) {
        const size_t at = ((m_ringWrite + i) % capacity) * kChannels;
        m_ring[at] = out[0][i];
        m_ring[at + 1] = channels > 1 ? out[1][i] : out[0][i];
    }
    m_ringWrite = (m_ringWrite + frames) % capacity;
    m_available.fetch_add(frames);
    m_framesSeen.fetch_add(frames);
    m_wake.notify_one();
}

void Broadcaster::joinWorker()
{
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void Broadcaster::start()
{
    if (m_running.load()) {
        return;
    }
    // A worker that gave up on its own (the server refused it) is still
    // joinable; it has to be collected before another one starts.
    joinWorker();
    const BroadcastServer server = configuration()->server();
    if (!server.valid()) {
        setState(State::Failed, "Fill in the server, port and password in Preferences > Live broadcasting first");
        return;
    }

    m_available.store(0);
    m_dropped.store(0);
    m_framesSeen.store(0);
    m_streamCue.store(configuration()->source() == Source::Cue);
    m_gain.store(static_cast<float>(std::pow(10.0, configuration()->gainDb() / 20.0)));
    m_ringRead = 0;
    m_ringWrite = 0;
    m_running.store(true);
    setState(State::Connecting, "Connecting to " + server.host);

    mixtape::Renderer::SetOutputTap([this](bool isCue, const float* const* out, size_t channels, size_t frames,
                                           double sampleRate) {
        takeBlock(isCue, out, channels, frames, sampleRate);
    });

    m_thread = std::thread([this]() { run(); });
}

void Broadcaster::stop()
{
    const bool wasRunning = m_running.exchange(false);
    mixtape::Renderer::SetOutputTap(nullptr);
    m_wake.notify_all();
    // Even when it had already given up, its thread is waiting to be joined.
    joinWorker();
    if (wasRunning && m_state.load() != State::Failed) {
        setState(State::Off, {});
    }
}

float Broadcaster::gainDb() const
{
    return configuration() ? configuration()->gainDb() : 0.0f;
}

void Broadcaster::setGainDb(float db)
{
    if (configuration()) {
        configuration()->setGainDb(db);
    }
    m_gain.store(static_cast<float>(std::pow(10.0, db / 20.0)));
    m_stateChanged.notify();
}

float Broadcaster::peakDbfs(int channel) const
{
    if (channel < 0 || channel >= static_cast<int>(kChannels) || m_state.load() != State::Live) {
        return -100.0f;
    }
    return m_peak[channel].load();
}

muse::async::Notification Broadcaster::stateChanged() const
{
    return m_stateChanged;
}

void Broadcaster::toggle()
{
    if (m_running.load()) {
        stop();
    } else {
        // A failed attempt left a thread behind; stop() collects it first.
        stop();
        start();
    }
}

void Broadcaster::run()
{
    const BroadcastServer server = configuration()->server();
    IcecastSource connection;
    connection.setKeepGoing(&m_running);
    Mp3Encoder encoder;
    std::vector<float> block(kEncodeFrames * kChannels);
    std::vector<unsigned char> encoded;
    std::string lastMetadata;
    int64_t lastMetadataAt = 0;
    size_t lastFramesSeen = 0;
    bool wasSilent = false;

    const std::string where = server.host + ":" + std::to_string(server.port)
                              + (server.isIcecast() ? server.mount : std::string());

    //! Keeps trying while the operator wants to be on air: a server that goes
    //! away during a show comes back without anyone touching the app. A
    //! password or mount the server refuses is not retried -- that will not
    //! fix itself, and hammering it is rude.
    const auto refusedForGood = [](const std::string& error) {
        return error.find("password") != std::string::npos
               || error.find("already in use") != std::string::npos;
    };

    const auto openConnection = [&]() {
        int attempt = 0;
        while (m_running.load()) {
            setState(State::Connecting, attempt == 0 ? "Connecting to " + server.host
                     : "Reconnecting to " + server.host + " (try " + std::to_string(attempt + 1) + ")");
            if (connection.connectToServer(server)) {
                setState(State::Live, "On air to " + where);
                LOGI() << "Broadcasting to " << where;
                return true;
            }
            const std::string error = connection.error();
            if (error == "stopped" || !m_running.load()) {
                return false;
            }
            if (!configuration()->reconnect() || refusedForGood(error)) {
                setState(State::Failed, error);
                return false;
            }
            const int delay = configuration()->reconnectDelaySec();
            setState(State::Connecting, error + " -- trying again in " + std::to_string(delay) + " s");
            LOGW() << "Broadcast: " << error << "; retrying in " << delay << " s";
            for (int waited = 0; waited < delay * 10 && m_running.load(); ++waited) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            ++attempt;
        }
        return false;
    };

    if (!openConnection()) {
        if (m_state.load() != State::Failed) {
            setState(State::Off, {});
        }
        m_running.store(false);
        mixtape::Renderer::SetOutputTap(nullptr);
        // The thread ends here; start() joins it before making another.
        return;
    }

    while (m_running.load()) {
        // Wait for a block, but wake often enough to notice the stream being
        // stopped and to keep the metadata fresh.
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return !m_running.load() || m_available.load() >= kEncodeFrames;
            });
        }
        if (!m_running.load()) {
            break;
        }

        const double rate = m_sampleRate.load();
        if (rate > 0.0 && !encoder.isOpen() && !encoder.open(rate, static_cast<int>(kChannels), server.bitrateKbps)) {
            setState(State::Failed, "the MP3 encoder would not start");
            break;
        }

        while (m_available.load() >= kEncodeFrames && m_running.load()) {
            const float gain = m_gain.load();
            float peak[kChannels] = { 0.0f, 0.0f };
            for (size_t i = 0; i < kEncodeFrames; ++i) {
                const size_t at = ((m_ringRead + i) % kRingFrames) * kChannels;
                for (size_t c = 0; c < kChannels; ++c) {
                    const float sample = m_ring[at + c] * gain;
                    block[i * kChannels + c] = sample;
                    peak[c] = std::max(peak[c], std::abs(sample));
                }
            }
            // What the listeners are getting, for the window's meters. Falls
            // back a little each block so a glance at it reads like a meter.
            for (size_t c = 0; c < kChannels; ++c) {
                const float db = peak[c] > 1e-5f ? 20.0f * std::log10(peak[c]) : -100.0f;
                const float previous = m_peak[c].load();
                m_peak[c].store(db > previous ? db : std::max(db, previous - 3.0f));
            }
            m_ringRead = (m_ringRead + kEncodeFrames) % kRingFrames;
            m_available.fetch_sub(kEncodeFrames);

            encoded.clear();
            if (!encoder.encode(block.data(), kEncodeFrames, encoded)) {
                setState(State::Failed, "the MP3 encoder stopped");
                m_running.store(false);
                break;
            }
            if (!encoded.empty() && !connection.send(encoded.data(), encoded.size())) {
                LOGW() << "Broadcast: " << connection.error();
                connection.disconnect();
                // What is in the ring is stale by the time we are back.
                m_available.store(0);
                m_ringRead = m_ringWrite;
                encoder.close();
                if (!openConnection()) {
                    m_running.store(false);
                }
                break;
            }
        }

        // A server that goes away while nothing is playing would otherwise go
        // unnoticed until the next block: the socket says so straight away.
        if (m_running.load() && !connection.stillConnected()) {
            LOGW() << "Broadcast: the server closed the connection";
            connection.disconnect();
            m_available.store(0);
            m_ringRead = m_ringWrite;
            encoder.close();
            if (!openConnection()) {
                m_running.store(false);
                break;
            }
        }

        // The chosen output may not be running -- Perform while only the
        // preview is playing, say. The stream stays up (the server keeps the
        // mount) but the button should say why nothing is going out.
        {
            const size_t seen = m_framesSeen.load();
            const bool silent = seen == lastFramesSeen;
            lastFramesSeen = seen;
            if (silent != wasSilent && m_state.load() == State::Live) {
                wasSilent = silent;
                const std::string which = configuration()->source() == Source::Cue
                                          ? "the headphone cue" : "the Perform output";
                setState(State::Live, silent ? "On air to " + where + " -- waiting for " + which
                         : "On air to " + where);
            }
        }

        if (configuration()->sendMetadata()) {
            const int64_t now = nowSeconds();
            if (now - lastMetadataAt >= 2) {
                lastMetadataAt = now;
                const std::string playing = nowPlaying();
                if (!playing.empty() && playing != lastMetadata) {
                    lastMetadata = playing;
                    connection.sendMetadata(playing);
                }
            }
        }
    }

    m_peak[0].store(-100.0f);
    m_peak[1].store(-100.0f);

    encoded.clear();
    encoder.flush(encoded);
    if (!encoded.empty()) {
        connection.send(encoded.data(), encoded.size());
    }
    connection.disconnect();
    mixtape::Renderer::SetOutputTap(nullptr);

    const size_t dropped = m_dropped.load();
    if (dropped > 0) {
        LOGW() << "Broadcast dropped " << dropped << " frames: the server could not keep up";
    }
    LOGI() << "Broadcast stopped";
}

std::string Broadcaster::nowPlaying() const
{
    // The clip under the playhead of the output being streamed, set by
    // projectscene's BroadcastNowPlaying; empty until it knows one, when the
    // listeners see the stream's own name.
    std::lock_guard<std::mutex> lock(m_nowPlayingMutex);
    return m_nowPlaying;
}

void Broadcaster::setNowPlaying(const std::string& title)
{
    std::lock_guard<std::mutex> lock(m_nowPlayingMutex);
    m_nowPlaying = title;
}
