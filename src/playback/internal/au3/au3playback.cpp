#include "au3playback.h"

#include "au3player.h"
#include "mixtapeplayer.h"
#include "au3audiooutput.h"

#include "log.h"

using namespace au::playback;

au::playback::IPlayerPtr Au3Playback::player(TrackSequenceId) const
{
    if (!m_player) {
        if (MixtapePlayer::enabled()) {
            m_player = std::make_shared<MixtapePlayer>(iocContext());
        } else {
            m_player = std::make_shared<Au3Player>(iocContext());
        }
    }
    return m_player;
}

std::shared_ptr<au::playback::IAudioOutput> Au3Playback::audioOutput() const
{
    if (!m_audioOutput) {
        m_audioOutput = std::make_shared<Au3AudioOutput>(iocContext());
    }
    return m_audioOutput;
}

MixtapePlayer* Au3Playback::mixtape() const
{
    return dynamic_cast<MixtapePlayer*>(player().get());
}

bool Au3Playback::performanceAvailable() const { return mixtape() != nullptr; }
bool Au3Playback::performanceActive() const { return mixtape() && mixtape()->performanceActive(); }
bool Au3Playback::performing() const { return mixtape() && mixtape()->performing(); }
muse::secs_t Au3Playback::performancePosition() const { return mixtape() ? mixtape()->performancePosition() : muse::secs_t(0.0); }

void Au3Playback::togglePerform()
{
    if (auto* m = mixtape()) {
        m->togglePerform();
        return;
    }
    // Perform lives in GlideMix's engine only, which is off only when asked.
    LOGW() << "Perform needs GlideMix's playback engine, which is off (MIXTAPE_ENGINE=0 or use-old-engine)";
}

void Au3Playback::resetPerformance()
{
    if (auto* m = mixtape()) {
        m->resetPerformance();
    }
}

void Au3Playback::setPerformancePosition(muse::secs_t position)
{
    if (auto* m = mixtape()) {
        m->setPerformancePosition(position.to_double());
    }
}

std::vector<std::string> Au3Playback::outputDevices() const
{
    return mixtape() ? mixtape()->outputDevices() : std::vector<std::string> {};
}

std::string Au3Playback::headphonesDevice() const
{
    return mixtape() ? mixtape()->headphonesDevice() : std::string();
}

void Au3Playback::setHeadphonesDevice(const std::string& device)
{
    if (auto* m = mixtape()) {
        m->setHeadphonesDevice(device);
    }
}

muse::async::Channel<muse::secs_t> Au3Playback::performancePositionChanged() const
{
    static muse::async::Channel<muse::secs_t> none;
    return mixtape() ? mixtape()->performancePositionChanged() : none;
}

muse::async::Notification Au3Playback::performanceStateChanged() const
{
    static muse::async::Notification none;
    return mixtape() ? mixtape()->performanceStateChanged() : none;
}
