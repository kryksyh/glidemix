/*
 * GlideMix: what other DJ apps on this computer already know -- Engine DJ,
 * Traktor, VirtualDJ, Serato and djay. Read only: their files are opened for
 * reading (SQLite read-only) and never written.
 *
 * Two uses:
 *  - analysis takes an app's beat grid and key instead of working them out
 *    again (lookupDjApps);
 *  - each app shows in the library sidebar with its songs and playlists, like
 *    Rekordbox XML and Lexicon DJ (djAppLibrary).
 *
 * Loaded once, on a background thread (a large collection takes a few
 * seconds); until then nothing is known and songs are analysed as usual.
 * Serato also keeps its grid inside the music file: see readFileTags().
 */
#pragma once

#include <QString>
#include <QStringList>

#include "mixhouse/imixhouseexternallibrary.h"

namespace au::mixhouse {
struct MixHouseDjAppInfo {
    double bpm = 0.0;
    double firstBeatSec = 0.0;
    bool hasGrid = false;   ///< firstBeatSec is a real beat (djay gives BPM only)
    int keyId = 0;          ///< Mixxx ChromaticKey: 1..12 C..B major, 13..24 C..B minor; 0 = none
    QString source;         ///< "Engine DJ", "Traktor", ...
};

//! Starts loading the apps' libraries in the background (once).
void startLoadingDjApps();

//! True once the background load has finished (the sidebar refreshes then).
bool djAppsLoaded();
//! Bumps whenever a library arrives (the first load, Traktor after its click).
int djAppsGeneration();
//! macOS: Traktor is installed but its collection (in Documents, which macOS
//! guards) hasn't been read yet -- the sidebar offers it, a click reads it.
bool djAppWaitsForClick(const QString& source);
void readDjAppNow(const QString& source);

//! What the first app that knows `filePath` has, grid first. Empty until loaded.
MixHouseDjAppInfo lookupDjApps(const QString& filePath);

//! The sidebar's sources, in order: "engine", "traktor", "serato", "vdj", "djay".
QStringList djAppSources();
QString djAppName(const QString& source);
//! The app's library, or nullptr when it isn't on this computer (or not loaded yet).
const IMixHouseExternalLibrary* djAppLibrary(const QString& source);

//! "Gm", "G#m", "Abm", "C", "6A", "12B", "11m", "8d" -> Mixxx key id; 0 if not a key.
int djKeyIdFromText(const QString& text);
}
