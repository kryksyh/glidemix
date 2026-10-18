/*
 * MixHouse DJ 2
 *
 * Persisted UI/behavior settings for the library panel -- deliberately
 * separate from IMixHouseLibrary (which reads Mixxx's library DATABASE):
 * these are app preferences (QSettings-backed, via muse::IGlobalConfiguration),
 * not track data. Backs the "Music library" Preferences page
 * (LibraryPreferencesPage.qml), ported from real mixxx's own
 * preferences/dialog/dlgpreflibrary.cpp -- see each setting's own comment
 * for its real source.
 */
#pragma once

#include <QString>
#include <QStringList>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

namespace au::mixhouse {
/// Real mixxx: WLibrary::slotSetTrackTableFont / DlgPrefLibrary's
/// comboBox_track_double_click_action (dlgpreflibrary.cpp) --
/// WTrackTableView::doubleClicked dispatches on this to decide what a
/// double-clicked track does. This port has no deck/Auto-DJ-queue-from-
/// library UI split beyond what already exists (loadTrackToProject,
/// addTrackToAutoDj), so this enum just picks between those already-real
/// actions instead of a live deck.
enum class MixHouseTrackDoubleClickAction {
    LoadToProject = 0,       ///< real mixxx: "Load track to next available deck"
    AddToAutoDjBottom = 1,
    AddToAutoDjTop = 2,
    Ignore = 3
};

/// Real mixxx: waveform/overviewtype.h's own OverviewType enum, same order
/// (Filtered/HSV/RGB) and same default (RGB) -- DlgPrefWaveform's "Overview
/// Waveforms" group's own type combo, the ONE real Preferences control on
/// that page that actually reaches a whole-track overview bar rather than
/// only the live deck's scrolling waveform (see MixHouseLibraryPanel.qml's
/// overview Canvas for why the frame-rate/visual-gain controls on that same
/// real page are NOT ported here -- confirmed, not guessed, that real mixxx
/// itself never reaches an overview widget with those).
enum class MixHouseWaveformOverviewType {
    Filtered = 0,
    HSV = 1,
    RGB = 2,
    /// MixHouse's own, not a mixxx type: plain #FFFFFF -- a deliberate choice after
    /// comparing our colored library previews against rekordbox's crisp
    /// overview. RGB is the default now that it draws rekordbox's own preview.
    White = 3
};

/// How a musical key is written everywhere the app shows one (library Key
/// column, clip menu, grid editor). Mixxx's KeyUtils::KeyNotation, without its
/// "Custom" (24 hand-typed names). The numbers are what gets saved.
//! Stem separation: where Demucs runs.
enum class MixHouseStemsDevice {
    Automatic = 0,   ///< the Mac's GPU (Metal) when there is one, else CPU
    Gpu = 1,
    Cpu = 2
};

enum class MixHouseKeyNotation {
    Lancelot = 0,             ///< "8A" -- the Camelot wheel; the default
    LancelotTraditional = 1,  ///< "8A/Am"
    OpenKey = 2,              ///< "1m"
    OpenKeyTraditional = 3,   ///< "1m/Am"
    Traditional = 4           ///< "Am"
};

class IMixHouseLibraryConfiguration : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IMixHouseLibraryConfiguration)

public:
    virtual ~IMixHouseLibraryConfiguration() = default;

    /// DlgPrefLibrary's spinBox_row_height (dlgpreflibrary.cpp) -- the
    /// track table's own row height, px. Real mixxx also derives a minimum
    /// from the selected library font's metrics; this port's font is fixed
    /// (Audacity's own theme font, not user-selectable -- no Library Font
    /// control here, matching [[audacity-ui-untouchable]]), so only the
    /// height itself is configurable.
    virtual int libraryRowHeight() const = 0;
    virtual void setLibraryRowHeight(int px) = 0;

    /// Mixxx's "Library Font": the track table's text font. An empty family
    /// (the default) means the app theme's font; a point size of 0, the
    /// theme's size.
    virtual QString libraryFontFamily() const = 0;
    virtual int libraryFontPointSize() const = 0;
    virtual void setLibraryFont(const QString& family, int pointSize) = 0;

    /// Mixxx's "BPM display precision": decimals in the BPM column (0..4).
    virtual int bpmPrecision() const = 0;
    virtual void setBpmPrecision(int decimals) = 0;

    virtual MixHouseKeyNotation keyNotation() const = 0;
    virtual void setKeyNotation(MixHouseKeyNotation notation) = 0;

    //! Stem separation (Preferences > Stems).
    virtual MixHouseStemsDevice stemsDevice() const = 0;
    virtual void setStemsDevice(MixHouseStemsDevice device) = 0;
    //! High: htdemucs_ft (cleaner, about 4x slower); else htdemucs.
    virtual bool stemsHighQuality() const = 0;
    virtual void setStemsHighQuality(bool high) = 0;
    //! Oldest separated songs are removed past this size.
    virtual int stemsCacheLimitGb() const = 0;
    virtual void setStemsCacheLimitGb(int gb) = 0;

    /// Whether Mixxx's own preference values (mixxx.cfg) were imported once.
    virtual bool mixxxSettingsImported() const = 0;
    virtual void setMixxxSettingsImported(bool imported) = 0;

    virtual MixHouseTrackDoubleClickAction trackDoubleClickAction() const = 0;
    virtual void setTrackDoubleClickAction(MixHouseTrackDoubleClickAction action) = 0;

    /// DlgPrefLibrary's checkBox_show_recent_tracks... no -- this is
    /// checkBox_grey_out_played (real mixxx greys out a track's Title/
    /// Artist columns once its own timesplayed counter is nonzero).
    virtual bool greyOutPlayedTracks() const = 0;
    virtual void setGreyOutPlayedTracks(bool grey) = 0;

    /// DlgPrefLibrary::slotSearchDebouncingTimeoutMillisChanged ->
    /// WSearchLineEdit::setDebouncingTimeoutMillis. Real mixxx default is
    /// 0ms (debounce OFF) per its own spin box default in the .ui.
    virtual int searchDebounceMs() const = 0;
    virtual void setSearchDebounceMs(int ms) = 0;

    /// DlgPrefLibrary::slotBpmRangeSelected: real formula is
    /// `comboBoxValuePercent * RateRangePercent / 10000.0` -- the SECOND
    /// factor is the deck pitch-fader's own configured range, a setting
    /// this app has no deck UI to source (no [[audacity-ui-untouchable]]
    /// deck exists here). Assumes real mixxx's own RateRangePercent
    /// default (8%) since there's no live deck value to read instead, so
    /// the same four real UI choices (25/50/75/100%) resolve to 2/4/6/8% --
    /// 75% (6%) is both real mixxx's own default AND this port's existing
    /// hardcoded kFuzzyRelativeRange, so the default behavior is unchanged.
    /// Stored and returned as the FINAL relative range (e.g. 0.06), already
    /// resolved -- mixhousesearchquery.cpp reads this value directly, it
    /// doesn't know about the "percent of pitch range" UI framing at all.
    virtual double fuzzyBpmRelativeRange() const = 0;
    virtual void setFuzzyBpmRelativeRange(double range) = 0;

    /// DlgPrefLibrary's "Music Directories" list -- real mixxx's own
    /// watched-folders list (each one gets rescanned for new/changed files
    /// on demand or at startup). This port has no live filesystem watcher
    /// (no scanning happens except when explicitly asked), so "watched"
    /// here means "remembered, and (re)scanned when you press Add, or at
    /// startup if rescanOnStartup is on" -- addTracksFromFolder() itself
    /// (IMixHouseLibrary) is the same INSERT-only real scan the File menu's
    /// own "Add Folder to Library" already uses.
    virtual QStringList musicDirectories() const = 0;
    /// Adds AND runs a real scan of the folder (real mixxx: clicking "Add"
    /// immediately scans the new directory, it doesn't just remember it).
    /// No-op if already present.
    virtual void addMusicDirectory(const QString& path) = 0;
    /// Removes it from the remembered list only -- matches real mixxx:
    /// this never deletes the tracks already in the library from that
    /// folder, it just stops rescanning it.
    virtual void removeMusicDirectory(const QString& path) = 0;

    virtual bool rescanOnStartup() const = 0;
    virtual void setRescanOnStartup(bool rescan) = 0;

    /// GlideMix: the full Beat This! model instead of the small one -- about
    /// 1.7x slower, beat grids about 2% more exact. Default off (Fast).
    virtual bool accurateBeatAnalysis() const = 0;
    virtual void setAccurateBeatAnalysis(bool accurate) = 0;

    /// Whether addMusicDirectory()'s scan (and a startup rescan) reports
    /// its ScanResult via a real dialog -- reuses the exact IInteractive::
    /// info() dialog the File menu's "Add Folder to Library" already shows.
    virtual bool showScanSummaryDialog() const = 0;
    virtual void setShowScanSummaryDialog(bool show) = 0;

    /// DlgPrefLibrary's checkBox_edit_metadata_selected_click: real mixxx
    /// starts editing a cell on a SECOND click if the row was ALREADY
    /// selected (skipping the need for a double-click), only when this is
    /// on. See MixHouseTrackListModel::handleTrackClicked's own comment.
    virtual bool editMetadataAfterClick() const = 0;
    virtual void setEditMetadataAfterClick(bool edit) = 0;

    virtual bool searchCompletionsEnabled() const = 0;
    virtual void setSearchCompletionsEnabled(bool enabled) = 0;

    virtual bool searchHistoryShortcutsEnabled() const = 0;
    virtual void setSearchHistoryShortcutsEnabled(bool enabled) = 0;

    /// Real mixxx: WSearchLineEdit's own remembered searches (most-recent-
    /// first, deduplicated), the shared data both searchCompletionsEnabled
    /// (typed-prefix matches shown in a dropdown) and
    /// searchHistoryShortcutsEnabled (Up/Down cycles through these) read
    /// from.
    virtual QStringList searchHistory() const = 0;
    virtual void addSearchHistoryEntry(const QString& text) = 0;

    /// "Settings Directory" section's "Open Settings Folder": GlideMix's
    /// own library folder (library.sqlite + analysis/), the same folder
    /// MixHouseLibrary::defaultDatabasePath() uses. Pure path math, so this
    /// global interface doesn't need the per-context library.
    virtual QString settingsDirectoryPath() const = 0;

    /// DlgPrefLibrary's checkBox_SyncTrackMetadataExport
    /// (kSyncTrackMetadataExport real config key): when on, this port's own
    /// already-real Comment/Grouping/Composer/Album Artist/Track # edits
    /// (IMixHouseLibrary::setTrackComment et al, MixHouseTrackListModel's
    /// own double-click-to-edit) ALSO write straight to the actual audio
    /// file's tags via TagLib -- same library this app already links for
    /// cover art extraction, now used for writing instead of just reading.
    virtual bool syncMetadataToFileTags() const = 0;
    virtual void setSyncMetadataToFileTags(bool sync) = 0;

    /// DlgPrefWaveform's "Overview Waveforms" group -- see
    /// MixHouseWaveformOverviewType's own comment. Default RGB, matching
    /// real mixxx's own default (dlgprefwaveform.cpp's
    /// kOverviewTypeCfgKey default).
    virtual MixHouseWaveformOverviewType waveformOverviewType() const = 0;
    virtual void setWaveformOverviewType(MixHouseWaveformOverviewType type) = 0;

    /// DlgPrefWaveform's "Show minute markers on waveform overview"
    /// checkbox -- default checked, matching real mixxx's own
    /// slotResetToDefaults(). A real, small tick mark every 60 seconds of
    /// track duration along the Overview column, not an invented control:
    /// this port already has each row's real durationSec to place them
    /// correctly.
    virtual bool showWaveformMinuteMarkers() const = 0;
    virtual void setShowWaveformMinuteMarkers(bool show) = 0;

    /// Fires on every setter above. Mirrors IMixHouseLibrary::
    /// libraryChanged()'s exact pattern -- the ONLY way a change made in
    /// the (separate-module) Preferences page reaches the already-running
    /// library panel without a restart. Testing caught this in the field:
    /// the first version of this class had these settings persist and
    /// read back correctly in Preferences, but the library panel had
    /// already read them once at construction with no way to know they'd
    /// changed, so a running session never saw the new value at all.
    virtual muse::async::Notification settingsChanged() const = 0;

    /// SYNC, the parked MixHouse DJ app's toolbar toggle: when on, a track
    /// loaded or dropped into the arrangement has its downbeat locked to the
    /// bar grid. Tempo follows the project either way. Default on.
    virtual bool syncEnabled() const = 0;
    virtual void setSyncEnabled(bool enabled) = 0;


    /// TEMPO, the parked app's toolbar toggle: the master tempo lane under the
    /// ruler (off by default), and its height (56..260 px, default 96).
    //! GlideMix: the phrase ribbon (rekordbox's phrase analysis) on clips.
    virtual bool phraseRibbonVisible() const = 0;
    virtual void setPhraseRibbonVisible(bool visible) = 0;

    //! GlideMix: rekordbox's analysis (phrases, beatgrids, waveforms) for the
    //! tracks it has analysed, in addition to GlideMix's own. Default on.
    virtual bool useRekordboxAnalysis() const = 0;
    virtual void setUseRekordboxAnalysis(bool use) = 0;
    //! GlideMix: the Lexicon DJ library (tracks, playlists, artwork, cues)
    //! in the library's sidebar when Lexicon is installed. Default on.
    virtual bool useLexiconLibrary() const = 0;
    virtual void setUseLexiconLibrary(bool use) = 0;

    /// Quantize (rekordbox Q, Lexicon QUANTIZE, Mixxx's quantize button):
    /// cues, loops, seeks and phrase cuts snap to the nearest beat. Default on.
    virtual bool quantizeEnabled() const = 0;
    virtual void setQuantizeEnabled(bool enabled) = 0;

    /// Keylock (Mixxx, rekordbox MT): tempo changes keep the pitch. Off plays
    /// like a turntable, pitch following the speed. Default on.
    virtual bool keylockEnabled() const = 0;
    virtual void setKeylockEnabled(bool enabled) = 0;

    /// Automation Arm's Latch (Pro Tools): after a control is let go, its
    /// last value keeps being written until playback stops. Off is Touch:
    /// the line goes back to what it was. Default off.
    virtual bool automationLatch() const = 0;
    virtual void setAutomationLatch(bool latch) = 0;

    virtual bool tempoLaneVisible() const = 0;
    virtual void setTempoLaneVisible(bool visible) = 0;
    virtual int tempoLaneHeight() const = 0;
    virtual void setTempoLaneHeight(int height) = 0;
};
}
