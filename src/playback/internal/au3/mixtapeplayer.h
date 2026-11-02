/*
 * GlideMix: the player on the new playback engine (stage 1).
 *
 * The default player (MIXTAPE_ENGINE=0 or the file use-old-engine in
 * ~/Library/Application Support/Glidemix picks Audacity's instead). Play, stop,
 * pause, seek and the playhead run on au3-mixtape-engine; loop and play
 * region bookkeeping stay with Au3Player. While playing, the arrangement is
 * re-copied every tick, so edits are heard without stopping the music.
 */
#pragma once

#include <functional>
#include <map>
#include <memory>

#include <QTimer>

#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "modularity/ioc.h"

#include "../../iplayer.h"
#include "../../imetronome.h"

class IMeterSender;

namespace mixtape {
struct Arrangement;
class EffectsHost;
class Transport;
}

namespace au::playback {
class Au3Player;

class MixtapePlayer : public IPlayer, public muse::async::Asyncable, public muse::Contextable
{
    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::GlobalInject<IMetronome> metronome;

public:
    explicit MixtapePlayer(const muse::modularity::ContextPtr& ctx);
    ~MixtapePlayer() override;

    //! True when the new engine should be used (MIXTAPE_ENGINE=1).
    static bool enabled();
    //! True while the new engine is playing (edits need no playback restart).
    static bool engineIsPlaying();

    bool isBusy() const override { return false; }

    void play(std::optional<muse::secs_t> startTime = std::nullopt) override;
    void playRange(const PlaybackRegion& range) override;
    void seek(const muse::secs_t newPosition, bool applyIfPlaying = false) override;
    void rewind() override;
    void stop() override;
    void pause() override;
    void resume() override;

    bool isRunning() const override;
    PlaybackStatus playbackStatus() const override;
    muse::async::Channel<PlaybackStatus> playbackStatusChanged() const override;
    muse::ValNt<bool> reachedEnd() const override;

    PlaybackRegion playbackRegion() const override;
    void setPlaybackRegion(const PlaybackRegion& region) override;

    muse::async::Notification loopRegionChanged() const override;
    PlaybackRegion loopRegion() const override;
    void loopEditingBegin() override;
    void loopEditingEnd() override;
    void setLoopRegion(const PlaybackRegion& region) override;
    void setLoopRegionStart(const muse::secs_t time) override;
    void setLoopRegionEnd(const muse::secs_t time) override;
    void clearLoopRegion() override;
    bool isLoopRegionClear() const override;
    void setLoopRegionActive(const bool active) override;
    bool isLoopRegionActive() const override;

    void updatePlaybackPosition() override;
    muse::secs_t playbackPosition() const override;
    muse::async::Channel<muse::secs_t> playbackPositionChanged() const override;

    muse::Ret playTracks(TrackList& trackList, double startTime, double endTime, const PlayTracksOptions& options = {}) override;

    // The performance (MixMeister Perform): see IPerformance.
    bool performanceActive() const { return m_performActive; }
    bool performing() const { return m_performActive && !m_performPaused; }
    muse::secs_t performancePosition() const { return m_performPosition.val; }
    void togglePerform();
    void resetPerformance();
    void setPerformancePosition(double position);
    std::vector<std::string> outputDevices() const;
    std::string headphonesDevice() const;
    void setHeadphonesDevice(const std::string& device);
    muse::async::Channel<muse::secs_t> performancePositionChanged() const { return m_performPosition.ch; }
    muse::async::Notification performanceStateChanged() const { return m_performState; }

private:
    bool startPerformance();
    void stopPerformance();
    void updatePerformance();
    //! The metronome click mixed over a transport's output.
    std::function<void(float* const*, size_t, double, double)> metronomeOverlay(double sampleRate, size_t channels);
    bool startAt(double startTime, double endTime);
    void publishArrangement();
    //! Main thread: give new tracks effect processors, rebuild changed automation.
    void prepareEffects(mixtape::EffectsHost* host, std::map<int64_t, uint64_t>& signatures,
                        const std::shared_ptr<const mixtape::Arrangement>& arrangement);
    void stopTransport();
    void setStatus(PlaybackStatus status);

    std::shared_ptr<Au3Player> m_regions;   // loop / play region bookkeeping
    std::unique_ptr<mixtape::Transport> m_transport;
    std::unique_ptr<mixtape::EffectsHost> m_effects;
    std::map<int64_t, uint64_t> m_automationSignatures;   // track id -> last configured
    muse::ValCh<PlaybackStatus> m_status;
    muse::ValNt<bool> m_reachedEnd;
    muse::ValCh<muse::secs_t> m_position;
    std::shared_ptr<IMeterSender> m_meter;   // the one playing, so stopping needs no services
    //! Plain play runs to the end of the tracks as they are now, not as they were at play.
    bool m_followTracksEnd = false;
    uint64_t m_generation = 0;
    QTimer m_timer;

    std::unique_ptr<mixtape::Transport> m_perform;
    std::unique_ptr<mixtape::EffectsHost> m_performEffects;
    std::map<int64_t, uint64_t> m_performSignatures;
    int m_performStatTicks = 0;
    uint64_t m_performUnderrunsLogged = 0;
    std::shared_ptr<IMeterSender> m_performMeter;
    bool m_performActive = false;
    bool m_performPaused = false;
    muse::ValCh<muse::secs_t> m_performPosition;
    muse::async::Notification m_performState;
    QTimer m_performTimer;
};
}
