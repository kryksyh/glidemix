/*
 * GlideMix
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "broadcast/ibroadcaster.h"
#include "broadcast/ibroadcastconfiguration.h"

namespace au::broadcast {
/// Takes finished blocks from the playback engine, encodes them and pushes
/// them at an Icecast or Shoutcast server.
///
/// The engine hands over blocks on the audio thread, which must never wait on
/// a socket: blocks are copied into a ring buffer there and everything else --
/// encoding, connecting, sending -- happens on this object's own thread.
class Broadcaster : public IBroadcaster, public muse::async::Asyncable
{
    muse::GlobalInject<IBroadcastConfiguration> configuration;

public:
    Broadcaster() = default;
    ~Broadcaster() override;

    void init();

    State state() const override;
    std::string statusText() const override;
    int secondsOnAir() const override;

    Source source() const override;
    void setSource(Source source) override;

    void start() override;
    void stop() override;
    void toggle() override;

    float gainDb() const override;
    void setGainDb(float db) override;
    float peakDbfs(int channel) const override;

    muse::async::Notification stateChanged() const override;
    void setNowPlaying(const std::string& title) override;

private:
    void run();
    //! Joins the worker if one has finished or is finishing. Always before a
    //! new one is started: assigning over a joinable std::thread terminates
    //! the process, which is what a failed connection followed by a second
    //! press used to do.
    void joinWorker();
    void setState(State state, const std::string& status);
    //! Audio thread: copy a block of the output we are streaming.
    void takeBlock(bool isCue, const float* const* out, size_t channels, size_t frames, double sampleRate);
    //! What the listeners' players show, from the clip under the playhead.
    std::string nowPlaying() const;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::thread m_thread;
    std::atomic<bool> m_running { false };

    //! Interleaved stereo, written by the audio thread, read by ours.
    std::vector<float> m_ring;
    size_t m_ringWrite = 0;   // audio thread
    size_t m_ringRead = 0;    // worker thread
    std::atomic<size_t> m_available { 0 };
    std::atomic<double> m_sampleRate { 0.0 };
    //! Read by the audio thread instead of the settings, which it must not touch.
    std::atomic<bool> m_streamCue { false };
    std::atomic<size_t> m_dropped { 0 };
    //! Total frames the tap has handed over, so the worker can tell whether
    //! the chosen output is running at all.
    std::atomic<size_t> m_framesSeen { 0 };

    //! Linear, applied to the block on its way to the encoder.
    std::atomic<float> m_gain { 1.0f };
    //! What the last encoded block peaked at, dBFS, falling back like a meter.
    std::atomic<float> m_peak[2] { -100.0f, -100.0f };

    std::atomic<State> m_state { State::Off };
    std::string m_status;
    std::atomic<int64_t> m_liveSince { 0 };
    muse::async::Notification m_stateChanged;
    mutable std::mutex m_nowPlayingMutex;
    std::string m_nowPlaying;
};
}
