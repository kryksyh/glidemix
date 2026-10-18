/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "../iprojectsceneuistate.h"

#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "framework/ui/iuistate.h"

namespace au::projectscene {
class ProjectSceneUiState : public IProjectSceneUiState, public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<muse::ui::IUiState> uiState { this };

public:
    ProjectSceneUiState(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    void init();

    TimelineRulerMode timelineRulerMode() const override;
    void setTimelineRulerMode(TimelineRulerMode mode) override;
    muse::async::Notification timelineRulerModeChanged() const override;

    bool gridLinesVisible() const override;
    void setGridLinesVisible(bool visible) override;
    muse::async::Notification gridLinesVisibleChanged() const override;

    bool clipWaveformRgb() const override;
    void setClipWaveformRgb(bool rgb) override;
    muse::async::Notification clipWaveformRgbChanged() const override;
    bool clipWaveformStereo() const override;
    void setClipWaveformStereo(bool stereo) override;

    int laneBackground() const override;
    void setLaneBackground(int mode) override;
    muse::async::Notification laneBackgroundChanged() const override;
    bool blackCanvas() const override;

private:
    muse::async::Notification m_timelineRulerModeChanged;
    muse::async::Notification m_gridLinesVisibleChanged;
    muse::async::Notification m_clipWaveformRgbChanged;
    muse::async::Notification m_laneBackgroundChanged;
};
}
