/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "modularity/ioc.h"

#include "../../iplayback.h"
#include "../../iperformance.h"

namespace au::playback {
class Au3Player;
class Au3AudioOutput;
class MixtapePlayer;
class Au3Playback : public IPlayback, public IPerformance, public muse::Contextable
{
public:
    Au3Playback(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    std::shared_ptr<playback::IPlayer> player(TrackSequenceId id = -1) const override;

    std::shared_ptr<playback::IAudioOutput> audioOutput() const override;

    bool performanceAvailable() const override;
    bool performanceActive() const override;
    bool performing() const override;
    muse::secs_t performancePosition() const override;
    void togglePerform() override;
    void resetPerformance() override;
    void setPerformancePosition(muse::secs_t position) override;
    std::vector<std::string> outputDevices() const override;
    std::string headphonesDevice() const override;
    void setHeadphonesDevice(const std::string& device) override;
    muse::async::Channel<muse::secs_t> performancePositionChanged() const override;
    muse::async::Notification performanceStateChanged() const override;

private:
    MixtapePlayer* mixtape() const;

    mutable std::shared_ptr<IPlayer> m_player;
    mutable std::shared_ptr<Au3AudioOutput> m_audioOutput;
};
}
