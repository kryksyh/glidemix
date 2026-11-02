/*
 * MixHouse DJ 2
 */
#include "au3-mixtape-engine/MixtapeClipVoice.h"
#include "mixhouselibraryconfiguration.h"

#include <algorithm>

#include <QDir>

#include "global/settings.h"

using namespace au::mixhouse;

static const std::string module_name("mixhouse");

// Real mixxx defaults, ported from preferences/dialog/dlgpreflibrary.cpp's
// own .ui/constructor defaults -- see IMixHouseLibraryConfiguration's own
// per-setting comments for exactly which real control each mirrors.
static const muse::Settings::Key LIBRARY_ROW_HEIGHT(module_name, "mixhouse/library/rowHeight");
static const muse::Settings::Key TRACK_DOUBLE_CLICK_ACTION(module_name, "mixhouse/library/trackDoubleClickAction");
static const muse::Settings::Key GREY_OUT_PLAYED_TRACKS(module_name, "mixhouse/library/greyOutPlayedTracks");
static const muse::Settings::Key SEARCH_DEBOUNCE_MS(module_name, "mixhouse/library/searchDebounceMs");
static const muse::Settings::Key FUZZY_BPM_RELATIVE_RANGE(module_name, "mixhouse/library/fuzzyBpmRelativeRange");
static const muse::Settings::Key MUSIC_DIRECTORIES(module_name, "mixhouse/library/musicDirectories");
static const muse::Settings::Key RESCAN_ON_STARTUP(module_name, "mixhouse/library/rescanOnStartup");
static const muse::Settings::Key WATCH_MUSIC_DIRECTORIES(module_name, "mixhouse/library/watchMusicDirectories");
static const muse::Settings::Key ANALYZE_NEW_TRACKS(module_name, "mixhouse/library/analyzeNewTracks");
static const muse::Settings::Key ACCURATE_BEAT_ANALYSIS(module_name, "mixhouse/library/accurateBeatAnalysis");
static const muse::Settings::Key ANALYSIS_MODE(module_name, "mixhouse/library/analysisMode");
static const muse::Settings::Key SHOW_SCAN_SUMMARY_DIALOG(module_name, "mixhouse/library/showScanSummaryDialog");
static const muse::Settings::Key EDIT_METADATA_AFTER_CLICK(module_name, "mixhouse/library/editMetadataAfterClick");
static const muse::Settings::Key SEARCH_COMPLETIONS_ENABLED(module_name, "mixhouse/library/searchCompletionsEnabled");
static const muse::Settings::Key SEARCH_HISTORY_SHORTCUTS_ENABLED(module_name, "mixhouse/library/searchHistoryShortcutsEnabled");
static const muse::Settings::Key SYNC_METADATA_TO_FILE_TAGS(module_name, "mixhouse/library/syncMetadataToFileTags");
static const muse::Settings::Key SEARCH_HISTORY(module_name, "mixhouse/library/searchHistory");
static const muse::Settings::Key WAVEFORM_OVERVIEW_TYPE(module_name, "mixhouse/library/waveformOverviewType");
static const muse::Settings::Key SHOW_WAVEFORM_MINUTE_MARKERS(module_name, "mixhouse/library/showWaveformMinuteMarkers");
static const muse::Settings::Key LIBRARY_FONT_FAMILY(module_name, "mixhouse/library/fontFamily");
static const muse::Settings::Key LIBRARY_FONT_POINT_SIZE(module_name, "mixhouse/library/fontPointSize");
static const muse::Settings::Key BPM_PRECISION(module_name, "mixhouse/library/bpmPrecision");
static const muse::Settings::Key KEY_NOTATION(module_name, "mixhouse/library/keyNotation");
static const muse::Settings::Key KEY_BADGES(module_name, "mixhouse/library/keyBadges");
static const muse::Settings::Key STEMS_DEVICE(module_name, "mixhouse/stems/device");
static const muse::Settings::Key STEMS_HIGH_QUALITY(module_name, "mixhouse/stems/highQuality");
static const muse::Settings::Key STEMS_CACHE_LIMIT_GB(module_name, "mixhouse/stems/cacheLimitGb");
static const muse::Settings::Key MIXXX_SETTINGS_IMPORTED(module_name, "mixhouse/library/mixxxSettingsImported");
static const muse::Settings::Key SYNC_ENABLED(module_name, "mixhouse/arrangement/syncEnabled");
static const muse::Settings::Key PHRASE_RIBBON_VISIBLE(module_name, "mixhouse/arrangement/phraseRibbonVisible");
static const muse::Settings::Key USE_REKORDBOX_ANALYSIS(module_name, "mixhouse/library/useRekordboxAnalysis");
static const muse::Settings::Key USE_LEXICON_LIBRARY(module_name, "mixhouse/library/useLexiconLibrary");
static const muse::Settings::Key QUANTIZE_ENABLED(module_name, "mixhouse/deck/quantize");
static const muse::Settings::Key KEYLOCK_ENABLED(module_name, "mixhouse/deck/keylock");
static const muse::Settings::Key TEMPO_LANE_VISIBLE(module_name, "mixhouse/arrangement/tempoLaneVisible");
static const muse::Settings::Key ENERGY_LANE_VISIBLE(module_name, "mixhouse/arrangement/energyLaneVisible");
static const muse::Settings::Key TEMPO_LANE_HEIGHT(module_name, "mixhouse/arrangement/tempoLaneHeight");
static constexpr int kMaxSearchHistoryEntries = 50;

// The settings this app ships as its defaults: rows tall enough
// for the cover art and the waveform overview, and a readable library font.
static constexpr int kDefaultLibraryRowHeight = 30;
static constexpr int kDefaultLibraryFontPointSize = 16;
// Real mixxx: WSearchLineEdit::kDefaultDebouncingTimeoutMillis (widget/
// wsearchlineedit.h) -- 300ms, clamped in real mixxx to [kMin=100,
// kMax=9999] by verifyDebouncingTimeoutMillis(). This port's own UI
// (LibraryPreferencesPage.qml) enforces that same [100, 9999] range
// directly rather than accepting then silently clamping a stored 0-99.
static constexpr int kDefaultSearchDebounceMs = 300;
// 75% * mixxx's own default 8% deck pitch range / 100 -- see
// IMixHouseLibraryConfiguration::fuzzyBpmRelativeRange's own comment for why
// this port can't read a real deck's own configured range.
static constexpr double kDefaultFuzzyBpmRelativeRange = 0.06;

void MixHouseLibraryConfiguration::init()
{
    muse::settings()->setDefaultValue(PHRASE_RIBBON_VISIBLE, muse::Val(true));
    muse::settings()->setDefaultValue(USE_REKORDBOX_ANALYSIS, muse::Val(true));
    muse::settings()->setDefaultValue(USE_LEXICON_LIBRARY, muse::Val(true));
    muse::settings()->setDefaultValue(QUANTIZE_ENABLED, muse::Val(true));
    muse::settings()->setDefaultValue(KEYLOCK_ENABLED, muse::Val(true));
    mixtape::SetKeylock(muse::settings()->value(KEYLOCK_ENABLED).toBool());
    muse::settings()->setDefaultValue(LIBRARY_ROW_HEIGHT, muse::Val(kDefaultLibraryRowHeight));
    muse::settings()->setDefaultValue(TRACK_DOUBLE_CLICK_ACTION,
                                       muse::Val(static_cast<int>(MixHouseTrackDoubleClickAction::LoadToProject)));
    muse::settings()->setDefaultValue(GREY_OUT_PLAYED_TRACKS, muse::Val(false));
    muse::settings()->setDefaultValue(LIBRARY_FONT_FAMILY, muse::Val(std::string()));
    muse::settings()->setDefaultValue(LIBRARY_FONT_POINT_SIZE, muse::Val(kDefaultLibraryFontPointSize));
    muse::settings()->setDefaultValue(BPM_PRECISION, muse::Val(1));   // Mixxx's default
    muse::settings()->setDefaultValue(KEY_NOTATION, muse::Val(static_cast<int>(MixHouseKeyNotation::Lancelot)));
    muse::settings()->setDefaultValue(KEY_BADGES, muse::Val(true));
    muse::settings()->setDefaultValue(STEMS_DEVICE, muse::Val(static_cast<int>(MixHouseStemsDevice::Automatic)));
    muse::settings()->setDefaultValue(STEMS_HIGH_QUALITY, muse::Val(false));
    muse::settings()->setDefaultValue(STEMS_CACHE_LIMIT_GB, muse::Val(10));
    muse::settings()->setDefaultValue(MIXXX_SETTINGS_IMPORTED, muse::Val(false));
    muse::settings()->setDefaultValue(SYNC_ENABLED, muse::Val(true));
    muse::settings()->setDefaultValue(TEMPO_LANE_VISIBLE, muse::Val(false));
    muse::settings()->setDefaultValue(TEMPO_LANE_HEIGHT, muse::Val(96));
    muse::settings()->setDefaultValue(ENERGY_LANE_VISIBLE, muse::Val(false));
    muse::settings()->setDefaultValue(SEARCH_DEBOUNCE_MS, muse::Val(kDefaultSearchDebounceMs));
    muse::settings()->setDefaultValue(FUZZY_BPM_RELATIVE_RANGE, muse::Val(kDefaultFuzzyBpmRelativeRange));
    muse::settings()->setDefaultValue(MUSIC_DIRECTORIES, muse::Val(muse::ValList {}));
    // Real mixxx's own .ui defaults for these three checkboxes.
    muse::settings()->setDefaultValue(RESCAN_ON_STARTUP, muse::Val(true));
    muse::settings()->setDefaultValue(WATCH_MUSIC_DIRECTORIES, muse::Val(true));
    muse::settings()->setDefaultValue(ANALYZE_NEW_TRACKS, muse::Val(true));
    muse::settings()->setDefaultValue(ACCURATE_BEAT_ANALYSIS, muse::Val(false));
    muse::settings()->setDefaultValue(ANALYSIS_MODE, muse::Val(1));
    muse::settings()->setDefaultValue(SHOW_SCAN_SUMMARY_DIALOG, muse::Val(true));
    muse::settings()->setDefaultValue(EDIT_METADATA_AFTER_CLICK, muse::Val(false));
    muse::settings()->setDefaultValue(SEARCH_COMPLETIONS_ENABLED, muse::Val(false));
    muse::settings()->setDefaultValue(SEARCH_HISTORY_SHORTCUTS_ENABLED, muse::Val(false));
    muse::settings()->setDefaultValue(SYNC_METADATA_TO_FILE_TAGS, muse::Val(false));
    muse::settings()->setDefaultValue(SEARCH_HISTORY, muse::Val(muse::ValList {}));
    // RGB, as in real mixxx's dlgprefwaveform.cpp and as rekordbox draws it.
    // Minute markers on, as in mixxx.
    muse::settings()->setDefaultValue(WAVEFORM_OVERVIEW_TYPE,
                                       muse::Val(static_cast<int>(MixHouseWaveformOverviewType::RGB)));
    muse::settings()->setDefaultValue(SHOW_WAVEFORM_MINUTE_MARKERS, muse::Val(true));

    // Preferences > Cancel / Discard rolls values back without going through
    // the setters: the settings channels still report it, so every listener
    // (library panel, toolbar) refreshes to the restored values.
    for (const muse::Settings::Key& key : { LIBRARY_ROW_HEIGHT, TRACK_DOUBLE_CLICK_ACTION, GREY_OUT_PLAYED_TRACKS,
                                            LIBRARY_FONT_FAMILY, LIBRARY_FONT_POINT_SIZE, BPM_PRECISION, KEY_NOTATION,
                                            STEMS_DEVICE, STEMS_HIGH_QUALITY, STEMS_CACHE_LIMIT_GB,
                                            SYNC_ENABLED, TEMPO_LANE_VISIBLE, TEMPO_LANE_HEIGHT, ENERGY_LANE_VISIBLE,
                                            QUANTIZE_ENABLED, KEYLOCK_ENABLED,
                                            SEARCH_DEBOUNCE_MS, FUZZY_BPM_RELATIVE_RANGE, MUSIC_DIRECTORIES,
                                            RESCAN_ON_STARTUP, WATCH_MUSIC_DIRECTORIES, ANALYZE_NEW_TRACKS, ACCURATE_BEAT_ANALYSIS, ANALYSIS_MODE, SHOW_SCAN_SUMMARY_DIALOG, EDIT_METADATA_AFTER_CLICK,
                                            SEARCH_COMPLETIONS_ENABLED, SEARCH_HISTORY_SHORTCUTS_ENABLED,
                                            SYNC_METADATA_TO_FILE_TAGS, WAVEFORM_OVERVIEW_TYPE,
                                            SHOW_WAVEFORM_MINUTE_MARKERS }) {
        muse::settings()->valueChanged(key).onReceive(this, [this](const muse::Val&) {
            m_settingsChanged.notify();
        });
    }
    // The engine follows the stored keylock even when it changes without the setter.
    muse::settings()->valueChanged(KEYLOCK_ENABLED).onReceive(this, [](const muse::Val& value) {
        mixtape::SetKeylock(value.toBool());
    });
}

int MixHouseLibraryConfiguration::libraryRowHeight() const
{
    return muse::settings()->value(LIBRARY_ROW_HEIGHT).toInt();
}

void MixHouseLibraryConfiguration::setLibraryRowHeight(int px)
{
    muse::settings()->setSharedValue(LIBRARY_ROW_HEIGHT, muse::Val(px));
    m_settingsChanged.notify();
}

MixHouseTrackDoubleClickAction MixHouseLibraryConfiguration::trackDoubleClickAction() const
{
    return static_cast<MixHouseTrackDoubleClickAction>(muse::settings()->value(TRACK_DOUBLE_CLICK_ACTION).toInt());
}

void MixHouseLibraryConfiguration::setTrackDoubleClickAction(MixHouseTrackDoubleClickAction action)
{
    muse::settings()->setSharedValue(TRACK_DOUBLE_CLICK_ACTION, muse::Val(static_cast<int>(action)));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::greyOutPlayedTracks() const
{
    return muse::settings()->value(GREY_OUT_PLAYED_TRACKS).toBool();
}

void MixHouseLibraryConfiguration::setGreyOutPlayedTracks(bool grey)
{
    muse::settings()->setSharedValue(GREY_OUT_PLAYED_TRACKS, muse::Val(grey));
    m_settingsChanged.notify();
}

int MixHouseLibraryConfiguration::searchDebounceMs() const
{
    return muse::settings()->value(SEARCH_DEBOUNCE_MS).toInt();
}

void MixHouseLibraryConfiguration::setSearchDebounceMs(int ms)
{
    muse::settings()->setSharedValue(SEARCH_DEBOUNCE_MS, muse::Val(ms));
    m_settingsChanged.notify();
}

double MixHouseLibraryConfiguration::fuzzyBpmRelativeRange() const
{
    return muse::settings()->value(FUZZY_BPM_RELATIVE_RANGE).toDouble();
}

void MixHouseLibraryConfiguration::setFuzzyBpmRelativeRange(double range)
{
    muse::settings()->setSharedValue(FUZZY_BPM_RELATIVE_RANGE, muse::Val(range));
    m_settingsChanged.notify();
}

muse::async::Notification MixHouseLibraryConfiguration::settingsChanged() const
{
    return m_settingsChanged;
}

QStringList MixHouseLibraryConfiguration::musicDirectories() const
{
    QStringList result;
    for (const muse::Val& v : muse::settings()->value(MUSIC_DIRECTORIES).toList()) {
        result << v.toQString();
    }
    return result;
}

static void saveMusicDirectories(const QStringList& dirs)
{
    muse::ValList list;
    list.reserve(dirs.size());
    for (const QString& d : dirs) {
        list.push_back(muse::Val(d));
    }
    muse::settings()->setSharedValue(MUSIC_DIRECTORIES, muse::Val(list));
}

void MixHouseLibraryConfiguration::addMusicDirectory(const QString& path)
{
    QStringList dirs = musicDirectories();
    if (dirs.contains(path)) {
        return;
    }
    dirs << path;
    saveMusicDirectories(dirs);
    m_settingsChanged.notify();
}

void MixHouseLibraryConfiguration::removeMusicDirectory(const QString& path)
{
    QStringList dirs = musicDirectories();
    if (!dirs.removeOne(path)) {
        return;
    }
    saveMusicDirectories(dirs);
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::accurateBeatAnalysis() const
{
    return muse::settings()->value(ACCURATE_BEAT_ANALYSIS).toBool();
}

void MixHouseLibraryConfiguration::setAccurateBeatAnalysis(bool accurate)
{
    muse::settings()->setSharedValue(ACCURATE_BEAT_ANALYSIS, muse::Val(accurate));
}

int MixHouseLibraryConfiguration::analysisMode() const
{
    return std::clamp(muse::settings()->value(ANALYSIS_MODE).toInt(), 0, 2);
}

void MixHouseLibraryConfiguration::setAnalysisMode(int mode)
{
    muse::settings()->setSharedValue(ANALYSIS_MODE, muse::Val(std::clamp(mode, 0, 2)));
}

bool MixHouseLibraryConfiguration::rescanOnStartup() const
{
    return muse::settings()->value(RESCAN_ON_STARTUP).toBool();
}

void MixHouseLibraryConfiguration::setRescanOnStartup(bool rescan)
{
    muse::settings()->setSharedValue(RESCAN_ON_STARTUP, muse::Val(rescan));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::watchMusicDirectories() const
{
    return muse::settings()->value(WATCH_MUSIC_DIRECTORIES).toBool();
}

void MixHouseLibraryConfiguration::setWatchMusicDirectories(bool watch)
{
    muse::settings()->setSharedValue(WATCH_MUSIC_DIRECTORIES, muse::Val(watch));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::analyzeNewTracks() const
{
    return muse::settings()->value(ANALYZE_NEW_TRACKS).toBool();
}

void MixHouseLibraryConfiguration::setAnalyzeNewTracks(bool analyze)
{
    muse::settings()->setSharedValue(ANALYZE_NEW_TRACKS, muse::Val(analyze));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::showScanSummaryDialog() const
{
    return muse::settings()->value(SHOW_SCAN_SUMMARY_DIALOG).toBool();
}

void MixHouseLibraryConfiguration::setShowScanSummaryDialog(bool show)
{
    muse::settings()->setSharedValue(SHOW_SCAN_SUMMARY_DIALOG, muse::Val(show));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::editMetadataAfterClick() const
{
    return muse::settings()->value(EDIT_METADATA_AFTER_CLICK).toBool();
}

void MixHouseLibraryConfiguration::setEditMetadataAfterClick(bool edit)
{
    muse::settings()->setSharedValue(EDIT_METADATA_AFTER_CLICK, muse::Val(edit));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::searchCompletionsEnabled() const
{
    return muse::settings()->value(SEARCH_COMPLETIONS_ENABLED).toBool();
}

void MixHouseLibraryConfiguration::setSearchCompletionsEnabled(bool enabled)
{
    muse::settings()->setSharedValue(SEARCH_COMPLETIONS_ENABLED, muse::Val(enabled));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::searchHistoryShortcutsEnabled() const
{
    return muse::settings()->value(SEARCH_HISTORY_SHORTCUTS_ENABLED).toBool();
}

void MixHouseLibraryConfiguration::setSearchHistoryShortcutsEnabled(bool enabled)
{
    muse::settings()->setSharedValue(SEARCH_HISTORY_SHORTCUTS_ENABLED, muse::Val(enabled));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::syncMetadataToFileTags() const
{
    return muse::settings()->value(SYNC_METADATA_TO_FILE_TAGS).toBool();
}

void MixHouseLibraryConfiguration::setSyncMetadataToFileTags(bool sync)
{
    muse::settings()->setSharedValue(SYNC_METADATA_TO_FILE_TAGS, muse::Val(sync));
    m_settingsChanged.notify();
}

MixHouseWaveformOverviewType MixHouseLibraryConfiguration::waveformOverviewType() const
{
    return static_cast<MixHouseWaveformOverviewType>(muse::settings()->value(WAVEFORM_OVERVIEW_TYPE).toInt());
}

void MixHouseLibraryConfiguration::setWaveformOverviewType(MixHouseWaveformOverviewType type)
{
    muse::settings()->setSharedValue(WAVEFORM_OVERVIEW_TYPE, muse::Val(static_cast<int>(type)));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::showWaveformMinuteMarkers() const
{
    return muse::settings()->value(SHOW_WAVEFORM_MINUTE_MARKERS).toBool();
}

void MixHouseLibraryConfiguration::setShowWaveformMinuteMarkers(bool show)
{
    muse::settings()->setSharedValue(SHOW_WAVEFORM_MINUTE_MARKERS, muse::Val(show));
    m_settingsChanged.notify();
}

QStringList MixHouseLibraryConfiguration::searchHistory() const
{
    QStringList result;
    for (const muse::Val& v : muse::settings()->value(SEARCH_HISTORY).toList()) {
        result << v.toQString();
    }
    return result;
}

void MixHouseLibraryConfiguration::addSearchHistoryEntry(const QString& text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }
    QStringList history = searchHistory();
    history.removeAll(text);   // move to front instead of duplicating
    history.prepend(text);
    while (history.size() > kMaxSearchHistoryEntries) {
        history.removeLast();
    }
    muse::ValList list;
    list.reserve(history.size());
    for (const QString& s : history) {
        list.push_back(muse::Val(s));
    }
    muse::settings()->setSharedValue(SEARCH_HISTORY, muse::Val(list));
    // No m_settingsChanged.notify() here: the search box's own history/
    // completions read this on demand (Up/Down press, text changed), not
    // through a live-bound property -- same reasoning as
    // fuzzyBpmRelativeRange not needing one either.
}

QString MixHouseLibraryConfiguration::settingsDirectoryPath() const
{
    // Same real path MixHouseLibrary::defaultDatabasePath() computes,
    // directory component only -- see this method's own header comment
    // for why it's duplicated here instead of injected.
    return QDir::homePath() + QStringLiteral("/Library/Application Support/Glidemix/Library");
}

QString MixHouseLibraryConfiguration::libraryFontFamily() const
{
    return muse::settings()->value(LIBRARY_FONT_FAMILY).toQString();
}

int MixHouseLibraryConfiguration::libraryFontPointSize() const
{
    return std::clamp(muse::settings()->value(LIBRARY_FONT_POINT_SIZE).toInt(), 0, 72);
}

void MixHouseLibraryConfiguration::setLibraryFont(const QString& family, int pointSize)
{
    muse::settings()->setSharedValue(LIBRARY_FONT_FAMILY, muse::Val(family));
    muse::settings()->setSharedValue(LIBRARY_FONT_POINT_SIZE, muse::Val(std::clamp(pointSize, 0, 72)));
    m_settingsChanged.notify();
}

int MixHouseLibraryConfiguration::bpmPrecision() const
{
    return std::clamp(muse::settings()->value(BPM_PRECISION).toInt(), 0, 4);
}

void MixHouseLibraryConfiguration::setBpmPrecision(int decimals)
{
    muse::settings()->setSharedValue(BPM_PRECISION, muse::Val(std::clamp(decimals, 0, 4)));
    m_settingsChanged.notify();
}

MixHouseKeyNotation MixHouseLibraryConfiguration::keyNotation() const
{
    const int value = muse::settings()->value(KEY_NOTATION).toInt();
    return value >= 0 && value <= static_cast<int>(MixHouseKeyNotation::Traditional)
           ? static_cast<MixHouseKeyNotation>(value) : MixHouseKeyNotation::Lancelot;
}

void MixHouseLibraryConfiguration::setKeyNotation(MixHouseKeyNotation notation)
{
    muse::settings()->setSharedValue(KEY_NOTATION, muse::Val(static_cast<int>(notation)));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::keyBadges() const
{
    return muse::settings()->value(KEY_BADGES).toBool();
}

void MixHouseLibraryConfiguration::setKeyBadges(bool badges)
{
    muse::settings()->setSharedValue(KEY_BADGES, muse::Val(badges));
    m_settingsChanged.notify();
}

MixHouseStemsDevice MixHouseLibraryConfiguration::stemsDevice() const
{
    const int value = muse::settings()->value(STEMS_DEVICE).toInt();
    return value >= 0 && value <= static_cast<int>(MixHouseStemsDevice::Cpu)
           ? static_cast<MixHouseStemsDevice>(value) : MixHouseStemsDevice::Automatic;
}

void MixHouseLibraryConfiguration::setStemsDevice(MixHouseStemsDevice device)
{
    muse::settings()->setSharedValue(STEMS_DEVICE, muse::Val(static_cast<int>(device)));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::stemsHighQuality() const
{
    return muse::settings()->value(STEMS_HIGH_QUALITY).toBool();
}

void MixHouseLibraryConfiguration::setStemsHighQuality(bool high)
{
    muse::settings()->setSharedValue(STEMS_HIGH_QUALITY, muse::Val(high));
    m_settingsChanged.notify();
}

int MixHouseLibraryConfiguration::stemsCacheLimitGb() const
{
    return std::clamp(muse::settings()->value(STEMS_CACHE_LIMIT_GB).toInt(), 1, 200);
}

void MixHouseLibraryConfiguration::setStemsCacheLimitGb(int gb)
{
    muse::settings()->setSharedValue(STEMS_CACHE_LIMIT_GB, muse::Val(std::clamp(gb, 1, 200)));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::mixxxSettingsImported() const
{
    return muse::settings()->value(MIXXX_SETTINGS_IMPORTED).toBool();
}

void MixHouseLibraryConfiguration::setMixxxSettingsImported(bool imported)
{
    muse::settings()->setSharedValue(MIXXX_SETTINGS_IMPORTED, muse::Val(imported));
}

bool MixHouseLibraryConfiguration::syncEnabled() const
{
    return muse::settings()->value(SYNC_ENABLED).toBool();
}

void MixHouseLibraryConfiguration::setSyncEnabled(bool enabled)
{
    muse::settings()->setSharedValue(SYNC_ENABLED, muse::Val(enabled));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::phraseRibbonVisible() const
{
    return muse::settings()->value(PHRASE_RIBBON_VISIBLE).toBool();
}

void MixHouseLibraryConfiguration::setPhraseRibbonVisible(bool visible)
{
    muse::settings()->setSharedValue(PHRASE_RIBBON_VISIBLE, muse::Val(visible));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::useRekordboxAnalysis() const
{
    return muse::settings()->value(USE_REKORDBOX_ANALYSIS).toBool();
}

void MixHouseLibraryConfiguration::setUseRekordboxAnalysis(bool use)
{
    muse::settings()->setSharedValue(USE_REKORDBOX_ANALYSIS, muse::Val(use));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::useLexiconLibrary() const
{
    return muse::settings()->value(USE_LEXICON_LIBRARY).toBool();
}

void MixHouseLibraryConfiguration::setUseLexiconLibrary(bool use)
{
    muse::settings()->setSharedValue(USE_LEXICON_LIBRARY, muse::Val(use));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::quantizeEnabled() const
{
    return muse::settings()->value(QUANTIZE_ENABLED).toBool();
}

void MixHouseLibraryConfiguration::setQuantizeEnabled(bool enabled)
{
    muse::settings()->setSharedValue(QUANTIZE_ENABLED, muse::Val(enabled));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::keylockEnabled() const
{
    return muse::settings()->value(KEYLOCK_ENABLED).toBool();
}

void MixHouseLibraryConfiguration::setKeylockEnabled(bool enabled)
{
    muse::settings()->setSharedValue(KEYLOCK_ENABLED, muse::Val(enabled));
    mixtape::SetKeylock(enabled);
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::energyLaneVisible() const
{
    return muse::settings()->value(ENERGY_LANE_VISIBLE).toBool();
}

void MixHouseLibraryConfiguration::setEnergyLaneVisible(bool visible)
{
    muse::settings()->setSharedValue(ENERGY_LANE_VISIBLE, muse::Val(visible));
    m_settingsChanged.notify();
}

bool MixHouseLibraryConfiguration::tempoLaneVisible() const
{
    return muse::settings()->value(TEMPO_LANE_VISIBLE).toBool();
}

void MixHouseLibraryConfiguration::setTempoLaneVisible(bool visible)
{
    muse::settings()->setSharedValue(TEMPO_LANE_VISIBLE, muse::Val(visible));
    m_settingsChanged.notify();
}

int MixHouseLibraryConfiguration::tempoLaneHeight() const
{
    return std::clamp(muse::settings()->value(TEMPO_LANE_HEIGHT).toInt(), 56, 260);
}

void MixHouseLibraryConfiguration::setTempoLaneHeight(int height)
{
    muse::settings()->setSharedValue(TEMPO_LANE_HEIGHT, muse::Val(std::clamp(height, 56, 260)));
    m_settingsChanged.notify();
}
