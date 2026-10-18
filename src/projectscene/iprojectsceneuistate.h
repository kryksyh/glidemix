/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "global/async/notification.h"
#include "modularity/imoduleinterface.h"

#include "types/projectscenetypes.h"

namespace au::projectscene {
class IProjectSceneUiState : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IProjectSceneUiState)

public:
    virtual ~IProjectSceneUiState() = default;

    virtual TimelineRulerMode timelineRulerMode() const = 0;
    virtual void setTimelineRulerMode(TimelineRulerMode mode) = 0;
    virtual muse::async::Notification timelineRulerModeChanged() const = 0;

    //! GlideMix, per workspace: the bar and beat lines behind the tracks
    //! (and their zebra shading). Off is the rekordbox look -- the clips carry
    //! their own red bar markers and white beat ticks, so the lines are only a
    //! second grid drawn over them.
    virtual bool gridLinesVisible() const = 0;
    virtual void setGridLinesVisible(bool visible) = 0;
    virtual muse::async::Notification gridLinesVisibleChanged() const = 0;

    //! GlideMix, per workspace: draw clips as rekordbox's colour
    //! waveform (blue bass, orange mids, white highs on black) instead of
    //! Audacity's single-colour one.
    virtual bool clipWaveformRgb() const = 0;
    virtual void setClipWaveformRgb(bool rgb) = 0;
    virtual muse::async::Notification clipWaveformRgbChanged() const = 0;
    //! GlideMix: the colour waveform as two lanes, left and right, each channel analysed apart.
    virtual bool clipWaveformStereo() const = 0;
    virtual void setClipWaveformStereo(bool stereo) = 0;

    //! GlideMix, per workspace: what the lanes are painted with.
    //! 0 follows the waveform style (black under the colour waveform, the
    //! theme's grey otherwise), 1 is always black, 2 always the theme.
    virtual int laneBackground() const = 0;
    virtual void setLaneBackground(int mode) = 0;
    virtual muse::async::Notification laneBackgroundChanged() const = 0;

    //! What laneBackground() comes out as right now.
    virtual bool blackCanvas() const = 0;
};
}
