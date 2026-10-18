/*
 * GlideMix
 */
#pragma once

#include <QTimer>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/iprojecthistory.h"

#include "mixhouse/imixhouseclipcolors.h"
#include "mixhouse/imixhousekeymatch.h"

class WaveTrack;

namespace au::mixhouse {
//! Colour intensity and Color by key live on the track itself (an au3 track
//! attachment), so they are saved with the project, copied with the track and
//! restored by undo along with everything else.
class MixHouseClipColors : public IMixHouseClipColors, public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<IMixHouseKeyMatch> keyMatch { this };
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory { this };

public:
    explicit MixHouseClipColors(const muse::modularity::ContextPtr& iocCtx);

    int intensity(const trackedit::TrackId& track) const override;
    void setIntensity(const trackedit::TrackId& track, int intensity) override;
    bool colorByKey(const trackedit::TrackId& track) const override;
    void setColorByKey(const trackedit::TrackId& track, bool on) override;
    QColor keyColor(const trackedit::ClipKey& clip) const override;
    muse::async::Channel<trackedit::TrackId> trackColorsChanged() const override;

private:
    WaveTrack* waveTrack(const trackedit::TrackId& track) const;
    //! Folds the change into the current undo state (no step of its own) and
    //! marks the project unsaved; a slider drag is coalesced into one.
    void recordChange();
    void subscribeToHistory();

    muse::async::Channel<trackedit::TrackId> m_changed;
    QTimer m_recordTimer;
    bool m_subscribed = false;
};
}
