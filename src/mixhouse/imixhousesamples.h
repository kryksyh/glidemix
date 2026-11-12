/*
 * GlideMix Samples (plan B1): the user's own sample and sound-effect folders.
 * Only their own files (no bundled packs, no store). Each added folder is
 * scanned in the background into samples.sqlite next to the library: path,
 * size, date, length (from the file header), and what the names say (tempo,
 * key, loop or one-shot, category and pack folders). A folder on a disk that
 * is not plugged in keeps its samples, shown as missing.
 */
#pragma once

#include <QString>
#include <QStringList>
#include <vector>

#include "async/channel.h"
#include "async/notification.h"
#include "modularity/imoduleinterface.h"

namespace au::mixhouse {
struct MixHouseSample {
    int id = 0;
    QString path;         ///< full path
    QString folder;       ///< the added folder it was found under
    QString location;     ///< its own folder, below `folder` ("Loops/Pack/Sub")
    QString category;     ///< first folder level ("Loops")
    QString pack;         ///< second folder level
    QString name;         ///< file name without extension
    QString format;       ///< "wav", "aif", "mp3", "flac", ...
    double durationSec = 0.0;
    double bpm = 0.0;
    int keyId = 0;        ///< Mixxx key id, 0 = none
    bool loop = false;    ///< else a one-shot
    int bars = 0;         ///< loops: whole bars at bpm, 0 = unknown
    qint64 dateAdded = 0; ///< seconds since epoch
    bool missing = false; ///< its folder's disk is not connected
};

class IMixHouseSamples : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IMixHouseSamples)

public:
    virtual ~IMixHouseSamples() = default;

    /// Reads the saved samples once and checks the folders for changes in
    /// the background; the Samples tab calls it when it first shows.
    virtual void load() = 0;

    /// How many samples there are, without reading them all (the status
    /// line's "… and 42,073 samples").
    virtual int sampleCount() const = 0;

    virtual QStringList folders() const = 0;
    /// Adds the folder and scans it in the background.
    virtual void addFolder(const QString& path) = 0;
    /// Forgets the folder and its samples (never deletes files).
    virtual void removeFolder(const QString& path) = 0;
    /// Scans every folder again: new files added, changed ones re-read,
    /// deleted ones dropped (folders on an unplugged disk are left alone).
    virtual void rescan() = 0;

    virtual bool scanning() const = 0;
    /// (files done, files found so far) while a scan runs.
    virtual muse::async::Channel<int, int> scanProgress() const = 0;

    /// Every sample, in memory (42,000 rows is a few MB).
    virtual const std::vector<MixHouseSample>& samples() const = 0;
    /// The list or the folders changed (scan finished, folder added/removed).
    virtual muse::async::Notification samplesChanged() const = 0;

    /// GlideMix: a clip's right-click asks the Samples tab to show a file:
    /// its pack (`similar` false) or the sounds most like it. Kept until the
    /// tab takes it, so a tab opened just now still gets it.
    virtual void showInSamplesTab(const QString& path, bool similar) = 0;
    virtual bool takeShowRequest(QString& path, bool& similar) = 0;
    virtual muse::async::Notification showRequested() const = 0;
};
}
