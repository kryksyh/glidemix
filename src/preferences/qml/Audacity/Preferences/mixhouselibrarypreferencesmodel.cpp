/*
 * Audacity: A Digital Audio Editor
 */
#include <QDir>
#include <QStandardPaths>
#include "mixhouselibrarypreferencesmodel.h"

#include <cmath>

using namespace au::preferences;
using namespace au::mixhouse;

MixHouseLibraryPreferencesModel::MixHouseLibraryPreferencesModel(QObject* parent)
    : QObject(parent)
{
}

int MixHouseLibraryPreferencesModel::libraryRowHeight() const
{
    return configuration()->libraryRowHeight();
}

void MixHouseLibraryPreferencesModel::setLibraryRowHeight(int px)
{
    if (libraryRowHeight() == px) {
        return;
    }
    configuration()->setLibraryRowHeight(px);
    emit libraryRowHeightChanged();
}

QString MixHouseLibraryPreferencesModel::libraryFontFamily() const
{
    return configuration()->libraryFontFamily();
}

void MixHouseLibraryPreferencesModel::setLibraryFontFamily(const QString& family)
{
    if (libraryFontFamily() == family) {
        return;
    }
    configuration()->setLibraryFont(family, libraryFontPointSize());
    emit libraryFontChanged();
}

int MixHouseLibraryPreferencesModel::libraryFontPointSize() const
{
    return configuration()->libraryFontPointSize();
}

void MixHouseLibraryPreferencesModel::setLibraryFontPointSize(int pointSize)
{
    if (libraryFontPointSize() == pointSize) {
        return;
    }
    configuration()->setLibraryFont(libraryFontFamily(), pointSize);
    emit libraryFontChanged();
}

int MixHouseLibraryPreferencesModel::bpmPrecision() const
{
    return configuration()->bpmPrecision();
}

void MixHouseLibraryPreferencesModel::setBpmPrecision(int decimals)
{
    if (bpmPrecision() == decimals) {
        return;
    }
    configuration()->setBpmPrecision(decimals);
    emit bpmPrecisionChanged();
}

int MixHouseLibraryPreferencesModel::keyNotation() const
{
    return static_cast<int>(configuration()->keyNotation());
}

void MixHouseLibraryPreferencesModel::setKeyNotation(int notation)
{
    if (keyNotation() == notation) {
        return;
    }
    configuration()->setKeyNotation(static_cast<mixhouse::MixHouseKeyNotation>(notation));
    emit keyNotationChanged();
}

int MixHouseLibraryPreferencesModel::trackDoubleClickAction() const
{
    return static_cast<int>(configuration()->trackDoubleClickAction());
}

void MixHouseLibraryPreferencesModel::setTrackDoubleClickAction(int action)
{
    if (trackDoubleClickAction() == action) {
        return;
    }
    configuration()->setTrackDoubleClickAction(static_cast<MixHouseTrackDoubleClickAction>(action));
    emit trackDoubleClickActionChanged();
}

bool MixHouseLibraryPreferencesModel::greyOutPlayedTracks() const
{
    return configuration()->greyOutPlayedTracks();
}

void MixHouseLibraryPreferencesModel::setGreyOutPlayedTracks(bool grey)
{
    if (greyOutPlayedTracks() == grey) {
        return;
    }
    configuration()->setGreyOutPlayedTracks(grey);
    emit greyOutPlayedTracksChanged();
}

int MixHouseLibraryPreferencesModel::searchDebounceMs() const
{
    return configuration()->searchDebounceMs();
}

void MixHouseLibraryPreferencesModel::setSearchDebounceMs(int ms)
{
    if (searchDebounceMs() == ms) {
        return;
    }
    configuration()->setSearchDebounceMs(ms);
    emit searchDebounceMsChanged();
}

int MixHouseLibraryPreferencesModel::fuzzyBpmRangePercent() const
{
    // Reverse of setFuzzyBpmRangePercent's own real-mixxx formula -- see
    // this class's own header comment.
    const double range = configuration()->fuzzyBpmRelativeRange();
    return static_cast<int>(std::lround(range * 10000.0 / 8.0));
}

void MixHouseLibraryPreferencesModel::setFuzzyBpmRangePercent(int percent)
{
    if (fuzzyBpmRangePercent() == percent) {
        return;
    }
    // Real mixxx: DlgPrefLibrary::slotBpmRangeSelected -- bpmRange *
    // rateRangePercent / 10000.0, rateRangePercent assumed at its own
    // real default (8) since this port has no deck to read a live value
    // from (see IMixHouseLibraryConfiguration::fuzzyBpmRelativeRange).
    const double range = percent * 8.0 / 10000.0;
    configuration()->setFuzzyBpmRelativeRange(range);
    // Real mixxx applies this immediately, no restart -- see
    // au::mixhouse::setFuzzyBpmRelativeRange's own comment.
    au::mixhouse::setFuzzyBpmRelativeRange(range);
    emit fuzzyBpmRangePercentChanged();
}

QStringList MixHouseLibraryPreferencesModel::musicDirectories() const
{
    return configuration()->musicDirectories();
}

void MixHouseLibraryPreferencesModel::addMusicDirectory(const QString& path)
{
    configuration()->addMusicDirectory(path);
    emit musicDirectoriesChanged();
}

void MixHouseLibraryPreferencesModel::removeMusicDirectory(const QString& path)
{
    configuration()->removeMusicDirectory(path);
    emit musicDirectoriesChanged();
}

bool MixHouseLibraryPreferencesModel::accurateBeatAnalysis() const
{
    return configuration()->accurateBeatAnalysis();
}

void MixHouseLibraryPreferencesModel::setAccurateBeatAnalysis(bool accurate)
{
    if (accurateBeatAnalysis() == accurate) {
        return;
    }
    configuration()->setAccurateBeatAnalysis(accurate);
    emit accurateBeatAnalysisChanged();
}

bool MixHouseLibraryPreferencesModel::rescanOnStartup() const
{
    return configuration()->rescanOnStartup();
}

void MixHouseLibraryPreferencesModel::setRescanOnStartup(bool rescan)
{
    if (rescanOnStartup() == rescan) {
        return;
    }
    configuration()->setRescanOnStartup(rescan);
    emit rescanOnStartupChanged();
}

bool MixHouseLibraryPreferencesModel::watchMusicDirectories() const
{
    return configuration()->watchMusicDirectories();
}

void MixHouseLibraryPreferencesModel::setWatchMusicDirectories(bool watch)
{
    if (watchMusicDirectories() == watch) {
        return;
    }
    configuration()->setWatchMusicDirectories(watch);
    emit watchMusicDirectoriesChanged();
}

bool MixHouseLibraryPreferencesModel::analyzeNewTracks() const
{
    return configuration()->analyzeNewTracks();
}

void MixHouseLibraryPreferencesModel::setAnalyzeNewTracks(bool analyze)
{
    if (analyzeNewTracks() == analyze) {
        return;
    }
    configuration()->setAnalyzeNewTracks(analyze);
    emit analyzeNewTracksChanged();
}

bool MixHouseLibraryPreferencesModel::showScanSummaryDialog() const
{
    return configuration()->showScanSummaryDialog();
}

void MixHouseLibraryPreferencesModel::setShowScanSummaryDialog(bool show)
{
    if (showScanSummaryDialog() == show) {
        return;
    }
    configuration()->setShowScanSummaryDialog(show);
    emit showScanSummaryDialogChanged();
}

bool MixHouseLibraryPreferencesModel::editMetadataAfterClick() const
{
    return configuration()->editMetadataAfterClick();
}

void MixHouseLibraryPreferencesModel::setEditMetadataAfterClick(bool edit)
{
    if (editMetadataAfterClick() == edit) {
        return;
    }
    configuration()->setEditMetadataAfterClick(edit);
    emit editMetadataAfterClickChanged();
}

bool MixHouseLibraryPreferencesModel::searchCompletionsEnabled() const
{
    return configuration()->searchCompletionsEnabled();
}

void MixHouseLibraryPreferencesModel::setSearchCompletionsEnabled(bool enabled)
{
    if (searchCompletionsEnabled() == enabled) {
        return;
    }
    configuration()->setSearchCompletionsEnabled(enabled);
    emit searchCompletionsEnabledChanged();
}

bool MixHouseLibraryPreferencesModel::searchHistoryShortcutsEnabled() const
{
    return configuration()->searchHistoryShortcutsEnabled();
}

void MixHouseLibraryPreferencesModel::setSearchHistoryShortcutsEnabled(bool enabled)
{
    if (searchHistoryShortcutsEnabled() == enabled) {
        return;
    }
    configuration()->setSearchHistoryShortcutsEnabled(enabled);
    emit searchHistoryShortcutsEnabledChanged();
}

bool MixHouseLibraryPreferencesModel::syncMetadataToFileTags() const
{
    return configuration()->syncMetadataToFileTags();
}

void MixHouseLibraryPreferencesModel::setSyncMetadataToFileTags(bool sync)
{
    if (syncMetadataToFileTags() == sync) {
        return;
    }
    configuration()->setSyncMetadataToFileTags(sync);
    emit syncMetadataToFileTagsChanged();
}

bool MixHouseLibraryPreferencesModel::useRekordboxAnalysis() const
{
    return configuration()->useRekordboxAnalysis();
}

void MixHouseLibraryPreferencesModel::setUseRekordboxAnalysis(bool use)
{
    if (useRekordboxAnalysis() == use) {
        return;
    }
    configuration()->setUseRekordboxAnalysis(use);
    emit useRekordboxAnalysisChanged();
}

bool MixHouseLibraryPreferencesModel::useLexiconLibrary() const
{
    return configuration()->useLexiconLibrary();
}

void MixHouseLibraryPreferencesModel::setUseLexiconLibrary(bool use)
{
    if (useLexiconLibrary() == use) {
        return;
    }
    configuration()->setUseLexiconLibrary(use);
    emit useLexiconLibraryChanged();
}

int MixHouseLibraryPreferencesModel::waveformOverviewType() const
{
    return static_cast<int>(configuration()->waveformOverviewType());
}

void MixHouseLibraryPreferencesModel::setWaveformOverviewType(int type)
{
    if (waveformOverviewType() == type) {
        return;
    }
    configuration()->setWaveformOverviewType(static_cast<MixHouseWaveformOverviewType>(type));
    emit waveformOverviewTypeChanged();
}

bool MixHouseLibraryPreferencesModel::showWaveformMinuteMarkers() const
{
    return configuration()->showWaveformMinuteMarkers();
}

void MixHouseLibraryPreferencesModel::setShowWaveformMinuteMarkers(bool show)
{
    if (showWaveformMinuteMarkers() == show) {
        return;
    }
    configuration()->setShowWaveformMinuteMarkers(show);
    emit showWaveformMinuteMarkersChanged();
}

QString MixHouseLibraryPreferencesModel::settingsDirectoryPath() const
{
    return configuration()->settingsDirectoryPath();
}

QString MixHouseLibraryPreferencesModel::musicFolder() const
{
    // Without a folder the Windows chooser opened in System32.
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return QDir(music).exists() ? music : QDir::homePath();
}
