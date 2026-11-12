/*
 * GlideMix
 */
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

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

    //! Samples B7: stems of project seconds [t0, t1] of one clip, as 24-bit
    //! WAVs -- the whole clip is separated (or taken from the cache), then the
    //! range cut out. `stems`: 0 drums, 1 bass, 2 vocals, 3 other. File names
    //! are `namePattern` with %1 = the stem ("drums"); `keepExisting` adds
    //! " 2", " 3"... instead of replacing a file. Returns the files
    //! written; empty when it failed (already said) or was cancelled.
    //! Takes stem `stem` (0 drums .. 3 other) out of project seconds [t0, t1]
    //! of one clip, in place, with short fades at the edges. One undo step.
    virtual bool removeStemFromRange(const trackedit::ClipKey& clip, double t0, double t1, int stem) = 0;
    //! Moves project seconds [t0, t1] of one clip onto four stem lanes under
    //! its lane (that part of the song goes silent). One undo step.
    virtual bool splitRangeIntoStems(const trackedit::ClipKey& clip, double t0, double t1) = 0;

    virtual QStringList saveClipStems(const trackedit::ClipKey& clip, double t0, double t1, const QList<int>& stems,
                                      const QString& dir, const QString& namePattern, bool keepExisting) = 0;

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
