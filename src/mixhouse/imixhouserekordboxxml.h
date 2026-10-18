/*
 * GlideMix
 */
#pragma once

#include <QString>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

#include "imixhouseexternallibrary.h"

namespace au::mixhouse {
//! A rekordbox collection XML export (rekordbox Preferences > Advanced >
//! rekordbox xml): its playlist tree, tracks, beat grids and cue points,
//! read-only and live -- read again when the file changes.
class IMixHouseRekordboxXml : MODULE_EXPORT_INTERFACE, public IMixHouseExternalLibrary
{
    INTERFACE_ID(IMixHouseRekordboxXml)

public:
    static constexpr int kTrackIdBase = -1000000000;
    static bool isRekordboxTrack(int trackId) { return trackId <= kTrackIdBase; }

    virtual QString path() const = 0;
    virtual void setPath(const QString& path) = 0;
    //! True while the file is being read.
    virtual bool loading() const = 0;
    //! Fires when a (re)read finished or the path changed.
    virtual muse::async::Notification changed() const = 0;
};
}
