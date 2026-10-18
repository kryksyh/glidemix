/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include "modularity/ioc.h"

#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/mixhousesearchquery.h"

namespace au::preferences {
/// Backs LibraryPreferencesPage.qml -- see IMixHouseLibraryConfiguration's
/// own per-setting comments for exactly which real mixxx preferences/
/// dialog/dlgpreflibrary.cpp control each mirrors. Session History
/// (Track duplicate distance / auto-pruned short History playlists) and
/// External Libraries (Rhythmbox/Banshee/Traktor/Serato/iTunes show/hide)
/// and the Cover Art Fetcher resolution combo stay the original unwired
/// skeleton: those need real features this port doesn't have at all (a
/// live playback session log to dedupe/prune -- there's no deck here to
/// generate one; a browsable external-library tree distinct from the
/// one-shot iTunes/Rekordbox import this app already has; a MusicBrainz/
/// coverartarchive.org network fetcher) -- faking those specific few would
/// be the invented behavior the reference standing instruction rules out.
/// Everything else on the page is real now.
class MixHouseLibraryPreferencesModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    muse::GlobalInject<au::mixhouse::IMixHouseLibraryConfiguration> configuration;

    Q_PROPERTY(int libraryRowHeight READ libraryRowHeight WRITE setLibraryRowHeight NOTIFY libraryRowHeightChanged)
    // GlideMix: Mixxx's Library Font (empty family / size 0 = theme) and BPM precision.
    Q_PROPERTY(QString libraryFontFamily READ libraryFontFamily WRITE setLibraryFontFamily NOTIFY libraryFontChanged)
    Q_PROPERTY(int libraryFontPointSize READ libraryFontPointSize WRITE setLibraryFontPointSize NOTIFY libraryFontChanged)
    Q_PROPERTY(int bpmPrecision READ bpmPrecision WRITE setBpmPrecision NOTIFY bpmPrecisionChanged)
    Q_PROPERTY(int keyNotation READ keyNotation WRITE setKeyNotation NOTIFY keyNotationChanged)
    Q_PROPERTY(
        int trackDoubleClickAction READ trackDoubleClickAction WRITE setTrackDoubleClickAction NOTIFY trackDoubleClickActionChanged)
    Q_PROPERTY(bool greyOutPlayedTracks READ greyOutPlayedTracks WRITE setGreyOutPlayedTracks NOTIFY greyOutPlayedTracksChanged)
    Q_PROPERTY(int searchDebounceMs READ searchDebounceMs WRITE setSearchDebounceMs NOTIFY searchDebounceMsChanged)
    Q_PROPERTY(
        int fuzzyBpmRangePercent READ fuzzyBpmRangePercent WRITE setFuzzyBpmRangePercent NOTIFY fuzzyBpmRangePercentChanged)
    Q_PROPERTY(QStringList musicDirectories READ musicDirectories NOTIFY musicDirectoriesChanged)
    Q_PROPERTY(bool rescanOnStartup READ rescanOnStartup WRITE setRescanOnStartup NOTIFY rescanOnStartupChanged)
    Q_PROPERTY(bool accurateBeatAnalysis READ accurateBeatAnalysis WRITE setAccurateBeatAnalysis NOTIFY accurateBeatAnalysisChanged)
    Q_PROPERTY(
        bool showScanSummaryDialog READ showScanSummaryDialog WRITE setShowScanSummaryDialog NOTIFY showScanSummaryDialogChanged)
    Q_PROPERTY(
        bool editMetadataAfterClick READ editMetadataAfterClick WRITE setEditMetadataAfterClick NOTIFY editMetadataAfterClickChanged)
    Q_PROPERTY(
        bool searchCompletionsEnabled READ searchCompletionsEnabled WRITE setSearchCompletionsEnabled NOTIFY searchCompletionsEnabledChanged)
    Q_PROPERTY(bool searchHistoryShortcutsEnabled READ searchHistoryShortcutsEnabled WRITE setSearchHistoryShortcutsEnabled
                   NOTIFY searchHistoryShortcutsEnabledChanged)
    Q_PROPERTY(
        bool syncMetadataToFileTags READ syncMetadataToFileTags WRITE setSyncMetadataToFileTags NOTIFY syncMetadataToFileTagsChanged)
    Q_PROPERTY(
        bool useRekordboxAnalysis READ useRekordboxAnalysis WRITE setUseRekordboxAnalysis NOTIFY useRekordboxAnalysisChanged)
    Q_PROPERTY(
        bool useLexiconLibrary READ useLexiconLibrary WRITE setUseLexiconLibrary NOTIFY useLexiconLibraryChanged)
    Q_PROPERTY(
        int waveformOverviewType READ waveformOverviewType WRITE setWaveformOverviewType NOTIFY waveformOverviewTypeChanged)
    Q_PROPERTY(bool showWaveformMinuteMarkers READ showWaveformMinuteMarkers WRITE setShowWaveformMinuteMarkers
                   NOTIFY showWaveformMinuteMarkersChanged)
    Q_PROPERTY(QString settingsDirectoryPath READ settingsDirectoryPath CONSTANT)

public:
    explicit MixHouseLibraryPreferencesModel(QObject* parent = nullptr);

    int libraryRowHeight() const;
    QString libraryFontFamily() const;
    void setLibraryFontFamily(const QString& family);
    int libraryFontPointSize() const;
    void setLibraryFontPointSize(int pointSize);
    int bpmPrecision() const;
    void setBpmPrecision(int decimals);
    int keyNotation() const;
    void setKeyNotation(int notation);
    void setLibraryRowHeight(int px);

    int trackDoubleClickAction() const;
    void setTrackDoubleClickAction(int action);

    bool greyOutPlayedTracks() const;
    void setGreyOutPlayedTracks(bool grey);

    int searchDebounceMs() const;
    void setSearchDebounceMs(int ms);

    /// The QML combo shows real mixxx's own four choices (25/50/75/100 "%
    /// of the deck pitch range") -- see IMixHouseLibraryConfiguration::
    /// fuzzyBpmRelativeRange's own comment for the resolved-value storage
    /// and why this port can't apply the real second factor (a live deck's
    /// own configured rate range). Converted at this model boundary so the
    /// underlying config (and mixhousesearchquery.cpp, which reads it)
    /// only ever deals in the final relative range, matching real mixxx's
    /// own BpmFilterNode::setBpmRelativeRange(), not the UI's "% of pitch
    /// range" framing.
    int fuzzyBpmRangePercent() const;
    void setFuzzyBpmRangePercent(int percent);

    QStringList musicDirectories() const;
    Q_INVOKABLE void addMusicDirectory(const QString& path);
    Q_INVOKABLE void removeMusicDirectory(const QString& path);

    bool rescanOnStartup() const;
    void setRescanOnStartup(bool rescan);
    bool accurateBeatAnalysis() const;
    void setAccurateBeatAnalysis(bool accurate);

    bool showScanSummaryDialog() const;
    void setShowScanSummaryDialog(bool show);

    bool editMetadataAfterClick() const;
    void setEditMetadataAfterClick(bool edit);

    bool searchCompletionsEnabled() const;
    void setSearchCompletionsEnabled(bool enabled);

    bool searchHistoryShortcutsEnabled() const;
    void setSearchHistoryShortcutsEnabled(bool enabled);

    bool syncMetadataToFileTags() const;
    bool useRekordboxAnalysis() const;
    void setUseRekordboxAnalysis(bool use);
    bool useLexiconLibrary() const;
    void setUseLexiconLibrary(bool use);
    void setSyncMetadataToFileTags(bool sync);

    /// DlgPrefWaveform's "Overview Waveforms" type combo -- see
    /// IMixHouseLibraryConfiguration::waveformOverviewType's own comment.
    int waveformOverviewType() const;
    void setWaveformOverviewType(int type);

    bool showWaveformMinuteMarkers() const;
    void setShowWaveformMinuteMarkers(bool show);

    QString settingsDirectoryPath() const;

signals:
    void libraryRowHeightChanged();
    void libraryFontChanged();
    void bpmPrecisionChanged();
    void keyNotationChanged();
    void trackDoubleClickActionChanged();
    void greyOutPlayedTracksChanged();
    void searchDebounceMsChanged();
    void fuzzyBpmRangePercentChanged();
    void musicDirectoriesChanged();
    void rescanOnStartupChanged();
    void accurateBeatAnalysisChanged();
    void showScanSummaryDialogChanged();
    void editMetadataAfterClickChanged();
    void searchCompletionsEnabledChanged();
    void searchHistoryShortcutsEnabledChanged();
    void syncMetadataToFileTagsChanged();
    void useRekordboxAnalysisChanged();
    void useLexiconLibraryChanged();
    void waveformOverviewTypeChanged();
    void showWaveformMinuteMarkersChanged();
};
}
