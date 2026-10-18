/*
 * GlideMix
 */
#pragma once

#include "async/asyncable.h"

#include "playback/view/toolbars/playbacktoolbarbpmitem.h"
#include "mixhouse/imixhousetempomap.h"

namespace au::projectscene {
//! The BPM clock: with a ramp on the tempo line it shows the tempo under the
//! playhead, following it while playing; with a flat line, the project tempo.
class TempoBpmToolBarItem : public playback::PlaybackToolBarBPMItem
{
    Q_OBJECT

    muse::ContextInject<mixhouse::IMixHouseTempoMap> tempoMap { this };

public:
    explicit TempoBpmToolBarItem(const muse::ui::UiAction& action, muse::uicomponents::ToolBarItemType::Type type,
                                 QObject* parent = nullptr);

protected:
    void updateValues() override;
};
}
