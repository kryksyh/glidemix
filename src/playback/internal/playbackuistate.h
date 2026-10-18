/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "../iplaybackuistate.h"
#include "../iperformance.h"

#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "framework/ui/iuistate.h"

namespace au::playback {
class PlaybackUiState : public IPlaybackUiState, public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<muse::ui::IUiState> uiState { this };
    muse::ContextInject<IPerformance> performance { this };

public:
    PlaybackUiState(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    void init();

    PlaybackMeterPosition::MeterPosition playbackMeterPosition() const override;
    void setPlaybackMeterPosition(PlaybackMeterPosition::MeterPosition position) override;
    muse::async::Notification playbackMeterPositionChanged() const override;

private:
    bool performanceActive() const;

    muse::async::Notification m_playbackMeterPositionChanged;
    bool m_performanceWasActive = false;
    //! The Cue button pressed during a performance; forgotten when the next
    //! one starts, so every set starts on Room.
    bool m_cueDuringPerformance = false;
};
}
