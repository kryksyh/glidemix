/*
* Audacity: A Digital Audio Editor
*/
#include "playbackuistate.h"

using namespace au::playback;

static const QString PLAYBACK_METER_POSITION_KEY("playbackToolbar/playbackMeterPosition");

//! GlideMix: there is one meter, in the side panel, and this says what
//! it shows -- SideBar = Room (the Perform output, what the audience hears),
//! TopBar = Cue (the headphones). The names are from when the two outputs had
//! a meter each and this swapped their places; the saved value is kept so the
//! choice carries over.
//!
//! While a performance is on, the meter shows Room whatever was picked: that
//! is the output that must not be lost sight of on air. Cue can still be
//! picked during the set; the next performance starts on Room again, and when
//! it ends the meter goes back to the saved pick.
void PlaybackUiState::init()
{
    uiState()->uiItemStateChanged(PLAYBACK_METER_POSITION_KEY).onNotify(this, [this]() {
        m_playbackMeterPositionChanged.notify();
    });

    if (performance()) {
        m_performanceWasActive = performance()->performanceActive();
        performance()->performanceStateChanged().onNotify(this, [this]() {
            const bool active = performance()->performanceActive();
            if (active == m_performanceWasActive) {
                return;
            }
            m_performanceWasActive = active;
            m_cueDuringPerformance = false;
            m_playbackMeterPositionChanged.notify();
        });
    }
}

bool PlaybackUiState::performanceActive() const
{
    return performance() && performance()->performanceActive();
}

PlaybackMeterPosition::MeterPosition PlaybackUiState::playbackMeterPosition() const
{
    if (performanceActive()) {
        return m_cueDuringPerformance ? PlaybackMeterPosition::MeterPosition::TopBar
               : PlaybackMeterPosition::MeterPosition::SideBar;
    }
    return uiState()->uiItemState(PLAYBACK_METER_POSITION_KEY) == "1"
           ? PlaybackMeterPosition::MeterPosition::SideBar
           : PlaybackMeterPosition::MeterPosition::TopBar;
}

void PlaybackUiState::setPlaybackMeterPosition(PlaybackMeterPosition::MeterPosition position)
{
    if (performanceActive()) {
        const bool cue = position == PlaybackMeterPosition::MeterPosition::TopBar;
        if (cue != m_cueDuringPerformance) {
            m_cueDuringPerformance = cue;
            m_playbackMeterPositionChanged.notify();
        }
        return;
    }
    const QString value = position == PlaybackMeterPosition::MeterPosition::SideBar ? "1" : "0";
    if (uiState()->uiItemState(PLAYBACK_METER_POSITION_KEY) == value) {
        return;
    }
    uiState()->setUiItemState(PLAYBACK_METER_POSITION_KEY, value);
}

muse::async::Notification PlaybackUiState::playbackMeterPositionChanged() const
{
    return m_playbackMeterPositionChanged;
}
