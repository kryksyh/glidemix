/*
 * GlideMix
 */
#pragma once

#include <QString>

#include "async/channel.h"
#include "async/notification.h"

#include "modularity/imoduleinterface.h"
#include "trackedit/trackedittypes.h"

namespace au::mixhouse {
//! Stem separation: a track's audio split by Demucs (htdemucs) into drums,
//! bass, vocals and other, each on its own track right under the original.
//! Demucs runs as a separate helper process, found like the key analyzer
//! (bundled, installed add-on, or a developer checkout).
class IMixHouseStems : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IMixHouseStems)

public:
    virtual ~IMixHouseStems() = default;

    //! Whether the Demucs helper was found.
    virtual bool available() const = 0;

    //! Separates every clip on the track (with a progress dialog), adds four
    //! stem tracks under it and mutes it. One undo step.
    virtual void separateTrack(const trackedit::TrackId& track) = 0;

    //! Bytes of separated stems kept for reuse.
    virtual qint64 cacheSize() const = 0;
    virtual void clearCache() = 0;

    //! The stems engine installed from inside the app (Preferences > Stems):
    //! uv, its own Python, Demucs and PyTorch in Application Support/Glidemix/
    //! Helpers/glidemix-stems, about 1 GB, from GitHub and the Python package
    //! index. Runs in the background; installProgress() sends each step's text,
    //! installFinished() true when it worked.
    virtual bool installing() const = 0;
    virtual void installEngine() = 0;
    virtual void cancelInstall() = 0;
    //! Removes the engine installed in Helpers (not a developer checkout).
    virtual bool engineInHelpers() const = 0;
    virtual void uninstallEngine() = 0;
    virtual muse::async::Channel<QString> installProgress() const = 0;
    virtual muse::async::Channel<bool> installFinished() const = 0;
};
}
