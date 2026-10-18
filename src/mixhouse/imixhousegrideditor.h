/*
 * GlideMix
 */
#pragma once

#include "async/channel.h"
#include "modularity/imoduleinterface.h"

namespace au::mixhouse {
/// The Beatgrid editor: one library track at a time, its beat grid and
/// phrases edited on a deck of its own. Opening a track switches to the
/// Beatgrid workspace and shows the editor panel.
class IMixHouseGridEditor : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IMixHouseGridEditor)

public:
    virtual ~IMixHouseGridEditor() = default;

    virtual void openTrack(int trackId) = 0;
    /// -1 until a track was opened.
    virtual int currentTrackId() const = 0;
    virtual muse::async::Channel<int> trackOpened() const = 0;
};
}
