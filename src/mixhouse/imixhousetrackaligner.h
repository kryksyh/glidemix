/*
 * MixHouse DJ 2
 */
#pragma once

#include <vector>

#include "modularity/imoduleinterface.h"
#include "async/channel.h"
#include "io/path.h"
#include "trackedit/trackedittypes.h"

namespace au::mixhouse {
/// Lets Audacity's own timeline drop handler (projectscene DropController)
/// hand library tracks dropped on the timeline to MixHouse's beat-to-bar
/// alignment, the same alignment "load to project" does from the library.
class IMixHouseTrackAligner : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IMixHouseTrackAligner)

public:
    virtual ~IMixHouseTrackAligner() = default;

    /// `newClipsPerFile[i]` = the clips the import of `filePaths[i]` created.
    /// Files that aren't analyzed library tracks are left untouched.
    /// `projectWasEmpty` = no audio content before the drop: the first
    /// aligned file then sets the project tempo. `pxPerSec`: the timeline's
    /// zoom, so the anchors' pull reaches as far on screen at any zoom.
    virtual void alignDroppedFiles(const std::vector<muse::io::path_t>& filePaths,
                                   const std::vector<trackedit::ClipKeyList>& newClipsPerFile,
                                   bool projectWasEmpty, double pxPerSec = 0.0) = 0;

    /// Whether `filePath` is a library track whose tempo is already known --
    /// the same lookup alignDroppedFiles uses. Such a file skips the import's
    /// own tempo detection, whose guess this aligner would overwrite anyway.
    virtual bool knowsTempo(const muse::io::path_t& filePath) const = 0;

    /// GlideMix: a song placed without a drop (double-click in the library)
    /// asks the timeline to show it the way a drop does: `start`..`end` in
    /// view, and its lane.
    virtual void requestViewFocus(double start, double end, trackedit::TrackId track) = 0;
    virtual muse::async::Channel<double, double, trackedit::TrackId> viewFocusRequested() const = 0;
};
}
