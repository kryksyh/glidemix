/*
 * MixHouse DJ 2
 */
#pragma once

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "global/iglobalconfiguration.h"

#include "mixhouse/imixhouselibraryconfiguration.h"

namespace au::mixhouse {
class MixHouseLibraryConfiguration : public IMixHouseLibraryConfiguration, public muse::async::Asyncable
{
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;

public:
    MixHouseLibraryConfiguration() = default;

    void init();

    int libraryRowHeight() const override;
    void setLibraryRowHeight(int px) override;

    MixHouseTrackDoubleClickAction trackDoubleClickAction() const override;
    void setTrackDoubleClickAction(MixHouseTrackDoubleClickAction action) override;

    bool greyOutPlayedTracks() const override;
    void setGreyOutPlayedTracks(bool grey) override;

    int searchDebounceMs() const override;
    void setSearchDebounceMs(int ms) override;

    double fuzzyBpmRelativeRange() const override;
    void setFuzzyBpmRelativeRange(double range) override;

    QStringList musicDirectories() const override;
    void addMusicDirectory(const QString& path) override;
    void removeMusicDirectory(const QString& path) override;

    bool rescanOnStartup() const override;
    void setRescanOnStartup(bool rescan) override;
    bool watchMusicDirectories() const override;
    void setWatchMusicDirectories(bool watch) override;
    bool analyzeNewTracks() const override;
    void setAnalyzeNewTracks(bool analyze) override;
    bool accurateBeatAnalysis() const override;
    void setAccurateBeatAnalysis(bool accurate) override;

    bool showScanSummaryDialog() const override;
    void setShowScanSummaryDialog(bool show) override;

    bool editMetadataAfterClick() const override;
    void setEditMetadataAfterClick(bool edit) override;

    bool searchCompletionsEnabled() const override;
    void setSearchCompletionsEnabled(bool enabled) override;

    bool searchHistoryShortcutsEnabled() const override;
    void setSearchHistoryShortcutsEnabled(bool enabled) override;

    bool syncMetadataToFileTags() const override;
    void setSyncMetadataToFileTags(bool sync) override;

    MixHouseWaveformOverviewType waveformOverviewType() const override;
    void setWaveformOverviewType(MixHouseWaveformOverviewType type) override;

    bool showWaveformMinuteMarkers() const override;
    QString libraryFontFamily() const override;
    int libraryFontPointSize() const override;
    void setLibraryFont(const QString& family, int pointSize) override;
    int bpmPrecision() const override;
    MixHouseKeyNotation keyNotation() const override;
    void setKeyNotation(MixHouseKeyNotation notation) override;
    MixHouseStemsDevice stemsDevice() const override;
    void setStemsDevice(MixHouseStemsDevice device) override;
    bool stemsHighQuality() const override;
    void setStemsHighQuality(bool high) override;
    int stemsCacheLimitGb() const override;
    void setStemsCacheLimitGb(int gb) override;
    void setBpmPrecision(int decimals) override;
    bool mixxxSettingsImported() const override;
    void setMixxxSettingsImported(bool imported) override;
    bool syncEnabled() const override;
    void setSyncEnabled(bool enabled) override;
    bool phraseRibbonVisible() const override;
    bool useRekordboxAnalysis() const override;
    void setUseRekordboxAnalysis(bool use) override;
    bool useLexiconLibrary() const override;
    void setUseLexiconLibrary(bool use) override;
    bool quantizeEnabled() const override;
    void setQuantizeEnabled(bool enabled) override;
    bool keylockEnabled() const override;
    void setKeylockEnabled(bool enabled) override;
    void setPhraseRibbonVisible(bool visible) override;
    bool tempoLaneVisible() const override;
    void setTempoLaneVisible(bool visible) override;
    bool energyLaneVisible() const override;
    void setEnergyLaneVisible(bool visible) override;
    int tempoLaneHeight() const override;
    void setTempoLaneHeight(int height) override;
    void setShowWaveformMinuteMarkers(bool show) override;

    QStringList searchHistory() const override;
    void addSearchHistoryEntry(const QString& text) override;

    QString settingsDirectoryPath() const override;

    muse::async::Notification settingsChanged() const override;

private:
    muse::async::Notification m_settingsChanged;
};
}
