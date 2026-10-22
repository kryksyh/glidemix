/*
 * MixHouse DJ 2
 *
 * The DJ-side track record. Deliberately a plain struct with no Mixxx headers:
 * this module reads Mixxx's library DATABASE, not its code, so nothing here
 * drags in Mixxx's engine, its ControlObject system, or QtWidgets.
 */
#pragma once

#include <QString>
#include <QByteArray>
#include <vector>

#include "mixhousebeats.h"

namespace au::mixhouse {
struct MixHouseTrack {
    int id = -1;
    QString title;
    QString artist;
    QString album;
    QString genre;
    QString year;
    QString filePath;       // track_locations.location
    double durationSec = 0.0;   // -1 = NULL in the DB (header not parsed yet); see tracks()
    double bpm = 0.0;
    double energy = 0.0;    ///< 1-10 (GlideMix's analysis, or Lexicon's); 0 = not rated
    QString key;            // library.key, e.g. "8A" in the user's notation
    int keyId = 0;          // library.key_id, Mixxx's ChromaticKey enum value
    int rating = 0;
    int color = -1;         // library.color, 0xRRGGBB or -1

    // The rest of Mixxx's real library columns (library/columncache.cpp) --
    // every field its own column picker offers, already sitting in the same
    // mixxxdb.sqlite row: no analysis, no engine, just more of the same SELECT.
    QString albumArtist;    // library.album_artist
    QString composer;       // library.composer
    QString grouping;       // library.grouping
    QString trackNumber;    // library.tracknumber (Mixxx stores it as text, e.g. "3/12")
    QString fileType;       // library.filetype
    QString comment;        // library.comment
    int bitrate = 0;        // library.bitrate, kbps
    double replaygain = 0.0;// library.replaygain
    QString dateAdded;      // library.datetime_added, ISO-ish text from SQLite
    int timesPlayed = 0;    // library.timesplayed
    QString lastPlayedAt;   // library.last_played_at, NULL until first played
    bool bpmLocked = false; // library.bpm_lock -- WTrackMenu's "Lock BPM"

    // Cover art (Phase 8) -- CoverInfoRelative, mirrored from library.coverart_*.
    // type: CoverInfoRelative::Type -- 0=NONE, 1=METADATA (embedded in the audio
    // file's own tags), 2=FILE (a standalone sidecar image next to the track).
    int coverArtType = 0;
    int coverArtSource = 0;      // CoverInfoRelative::Source -- 0=UNKNOWN, 1=GUESSED, 2=USER_SELECTED
    QString coverArtLocation;    // library.coverart_location -- relative path, only meaningful for type==FILE
    quint16 coverArtHash = 0;    // library.coverart_hash -- legacy qChecksum, nonzero when real art was hashed

    // library.beats is a SERIALISED PROTOBUF beatgrid -- the real analysis
    // output. Carried through as-is for now; decoding it needs Mixxx's
    // beats.proto schema, which is the next piece of brain to port.
    QByteArray beatsBlob;
    QString beatsVersion;
    int sampleRate = 0;

    /// Decoded from beatsBlob at load time -- see mixhousebeats.h. This is the
    /// real analysis output: where every downbeat sits, not just the BPM.
    MixHouseBeatGrid beats;

    bool hasBeatgrid() const { return beats.valid; }

    /// PlaylistTracks.position -- only meaningful when this row came from a
    /// TrackQuery with playlistId set (-1 otherwise). Real mixxx playlists
    /// are ORDERED lists (PlaylistDAO::moveTrack) and default-sort by this
    /// exact column; it's also the only correct way to remove ONE occurrence
    /// of a track that appears more than once in the same playlist -- a
    /// track_id alone can't tell two occurrences apart.
    int playlistPosition = -1;
    /// Has a real Waveform Overview analysis row (track_analysis type 2) --
    /// what the library's rekordbox-style progress column shows as
    /// "Analyzed" once no batch is working on this track.
    bool analyzed = false;
    /// Detected downbeat (beat 1 of a bar), seconds into the file, from the
    /// MixHouse-only `mixhouse_downbeats` table (Beat This!, see
    /// MixHouseAnalysisResult::downbeatSec). -1 = not analyzed for downbeats.
    double downbeatSec = -1.0;
    double downbeatConfidence = 0.0;
    /// Who set the downbeat/grid: the analyzer's detector name, "manual"
    /// (grid tools) or "rekordbox". Empty when there's no row.
    QString downbeatDetector;
};

using MixHouseTrackList = std::vector<MixHouseTrack>;

/// PlaylistDAO::AutoDJSendLoc, mirrored here for addTrackToAutoDj: where a
/// track lands in the real Auto DJ queue playlist.
enum class MixHouseAutoDjLoc { Top, Bottom, Replace };

/// The "Clear" submenu of mixxx's real WTrackMenu. Each value clears one
/// piece of performance metadata -- matched against the real
/// ResetXxxTrackPointerOperation classes in widget/wtrackmenu.cpp. `All`
/// mirrors ClearAllPerformanceMetadataTrackPointerOperation exactly,
/// which deliberately does NOT include Comment (that's library metadata,
/// not performance data, in mixxx's own split).
enum class MixHouseClearField {
    Beats, PlayCount, Rating, Comment, MainCue, HotCues, IntroCue, OutroCue,
    Loops, Key, ReplayGain, Waveform, All
};

/// A playlist or crate: id, name, and how many tracks are in it.
struct MixHouseNamedItem {
    int id = -1;
    QString name;
    int count = 0;
    /// Playlist folders/nesting -- real mixxx has NO folder concept at all
    /// for its own Playlists table (confirmed directly from its real
    /// schema.xml: id/name/position/hidden/date_created/date_modified,
    /// nothing else), so this is original work against real rekordbox's
    /// own documented folder behavior (rekordbox manual pp.52, 248: a
    /// folder can hold both playlists and further folders, arbitrarily
    /// deep, and deleting one takes everything inside it with it) rather
    /// than a port. parentId -1 means top-level (real mixxx's own
    /// Playlists.id/crates.id never legitimately reach -1, so this stays
    /// a safe, unambiguous sentinel). isFolder is never true for crates --
    /// nesting is a Playlists-only feature there too, matching rekordbox's
    /// own Playlists-side-only folder tree.
    int parentId = -1;
    bool isFolder = false;
    /// Playlists.locked / crates.locked -- real mixxx disables Rename/
    /// Delete/Import and blocks add/remove on a locked collection
    /// (widget/wtrackmenu.cpp:1558-1559,1653,1197-1208;
    /// library/trackset/baseplaylistfeature.cpp's isPlaylistLocked() guards).
    bool locked = false;
};
using MixHouseNamedList = std::vector<MixHouseNamedItem>;

/// Result of importing an external library's XML export (iTunes/Apple Music,
/// or Rekordbox). Deliberately separate from ScanResult (addTracksFromFolder/
/// addTracksFromFiles): those only ever add tracks, this also creates real
/// playlists, so it needs its own count.
struct MixHouseImportResult {
    int tracksFound = 0;      ///< tracks referenced by an imported playlist
    int tracksAdded = 0;      ///< of those, newly inserted into `library`
    int tracksMatched = 0;    ///< of those, already present (matched by file path)
    int playlistsCreated = 0;
    /// Rekordbox import only: how many tracks ALREADY in this library (by
    /// real file path, anywhere in the rekordbox XML's <COLLECTION> -- not
    /// only ones referenced by a playlist) got real bpm/beatgrid/key data
    /// written in from rekordbox's own <TEMPO>/Tonality analysis. See
    /// IMixHouseLibrary::importFromRekordboxXml's own comment.
    int analysisImported = 0;
    QString error;
};

/// Roadmap Phase 7: result of running the real analysis engine
/// (mixhouseanalyzer.h) on one track and writing its output back to the
/// library row.
struct MixHouseAnalyzeResult {
    bool valid = false;
    double bpm = 0.0;
    QString key;     ///< Traditional notation text (e.g. "Bb", "F#m"), matching library.key's own real convention
    int keyId = 0;   ///< library.key_id -- Mixxx's ChromaticKey enum value
    QString error;
    /// True if the track's bpm/beatgrid were left untouched because
    /// bpm_lock was set -- real mixxx's AnalyzerBeats::shouldAnalyze()/
    /// isAnalyzerFeatureEnabled() refuse to run at all on a locked track
    /// (analyzer/analyzerbeats.cpp). Key detection is a separate real
    /// analyzer with no such lock, so key is still updated even when this
    /// is true.
    bool bpmSkippedLocked = false;
};
}
