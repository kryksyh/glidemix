/*
 * MixHouse DJ 2
 */
#include "mixhouselink.h"
#include "mixhousetracklist.h"
#include "mixhouse/mixhousecoverart.h"
#include "mixhouseactionscontroller.h"

#include <QDir>
#include <QProcess>
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>
#include <optional>
#include <QSettings>
#include "mixhouseprojectaligner.h"
#include "trackedit/dom/track.h"
#include "mixhouse/imixhousekeymatch.h"
#include "mixhouse/mixhousesamplename.h"
#include "mixhouse/mixhousebeat.h"
#include "mixhouse/mixhousevariations.h"
#include <QRandomGenerator>
#include <QStandardPaths>

#include "translation.h"

using namespace au::mixhouse;

MixHouseActionsController::MixHouseActionsController(const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx)
{
}

void MixHouseActionsController::init()
{
    // Ableton's Automation Arm.
    dispatcher()->reg(this, "mixhouse-automation-arm", [this]() {
        const auto project = globalContext()->currentProject();
        if (!project) {
            return;
        }
        const bool armed = !project->viewState()->automationArmed();
        project->viewState()->setAutomationArmed(armed);
        if (automationArmChanged) {
            automationArmChanged(armed);
        }
    });
    // Ableton's Control Bar Re-Enable Automation.
    dispatcher()->reg(this, "mixhouse-reenable-automation", [this]() {
        if (clipGain()->reenableAllPluginAutomation()) {
            projectHistory()->pushHistoryState("Re-enabled automation", "Re-enable automation");
        }
    });
    const auto refreshOverride = [this]() {
        if (automationOverrideChanged) {
            automationOverrideChanged(globalContext()->currentProject() && clipGain()->hasOverriddenPluginAutomation());
        }
    };
    globalContext()->currentProjectChanged().onNotify(this, [this, refreshOverride]() {
        refreshOverride();
        projectHistory()->historyChanged().onReceive(this, [refreshOverride](auto) { refreshOverride(); },
            muse::async::Asyncable::Mode::SetReplace);
    });
    dispatcher()->reg(this, "mixhouse-add-files", this, &MixHouseActionsController::addFilesToLibrary);
    dispatcher()->reg(this, "mixhouse-add-folder", this, &MixHouseActionsController::addFolderToLibrary);
    dispatcher()->reg(this, "mixhouse-import-rekordbox", this, &MixHouseActionsController::importFromRekordboxXml);
    dispatcher()->reg(this, "mixhouse-import-itunes", this, &MixHouseActionsController::importFromItunesXml);
    using Adjust = IMixHouseBeatGrids::Adjust;
    dispatcher()->reg(this, "mixhouse-grid-earlier", [this]() { beatGrids()->adjustSelectedClips(Adjust::Earlier); });
    dispatcher()->reg(this, "mixhouse-grid-later", [this]() { beatGrids()->adjustSelectedClips(Adjust::Later); });
    dispatcher()->reg(this, "mixhouse-bar-earlier", [this]() { beatGrids()->adjustSelectedClips(Adjust::BarEarlier); });
    dispatcher()->reg(this, "mixhouse-bar-later", [this]() { beatGrids()->adjustSelectedClips(Adjust::BarLater); });
    dispatcher()->reg(this, "mixhouse-grid-faster", [this]() { beatGrids()->adjustSelectedClips(Adjust::Faster); });
    dispatcher()->reg(this, "mixhouse-grid-slower", [this]() { beatGrids()->adjustSelectedClips(Adjust::Slower); });
    dispatcher()->reg(this, "mixhouse-grid-undo", [this]() { beatGrids()->adjustSelectedClips(Adjust::Undo); });
    // Split by hand only: a song added to the mix is never split by itself.
    dispatcher()->reg(this, "mixhouse-save-as-sample", this, &MixHouseActionsController::saveSelectionAsSample);
    // A clip's own right-click menu (songs and samples on the timeline).
    dispatcher()->reg(this, "mixhouse-clip-show-in-finder", [this](const muse::actions::ActionData& args) {
        if (args.count() == 1) {
            clipShowInFinder(args.arg<trackedit::ClipKey>(0));
        }
    });
    dispatcher()->reg(this, "mixhouse-clip-export", [this](const muse::actions::ActionData& args) {
        if (args.count() == 1) {
            clipExport(args.arg<trackedit::ClipKey>(0));
        }
    });
    dispatcher()->reg(this, "mixhouse-clip-similar", [this](const muse::actions::ActionData& args) {
        if (args.count() == 1) {
            const trackedit::ClipKey key = args.arg<trackedit::ClipKey>(0);
            clipShowInSamplesTab(key, true);
        }
    });
    dispatcher()->reg(this, "mixhouse-clip-view-pack", [this](const muse::actions::ActionData& args) {
        if (args.count() == 1) {
            const trackedit::ClipKey key = args.arg<trackedit::ClipKey>(0);
            clipShowInSamplesTab(key, false);
        }
    });
    dispatcher()->reg(this, "mixhouse-clip-resample", [this](const muse::actions::ActionData& args) {
        if (args.count() == 1) {
            const trackedit::ClipKey key = args.arg<trackedit::ClipKey>(0);
            clipResample(key);
        }
    });
    dispatcher()->reg(this, "mixhouse-clip-variations", [this](const muse::actions::ActionData& args) {
        if (args.count() == 1) {
            const trackedit::ClipKey key = args.arg<trackedit::ClipKey>(0);
            clipVariations(key);
        }
    });
    dispatcher()->reg(this, "mixhouse-clip-send-to-beat", [this](const muse::actions::ActionData& args) {
        if (args.count() == 1) {
            const trackedit::ClipKey key = args.arg<trackedit::ClipKey>(0);
            clipSendToBeat(key);
        }
    });
    dispatcher()->reg(this, "mixhouse-clip-add-compatible", [this](const muse::actions::ActionData& args) {
        if (args.count() == 1) {
            const trackedit::ClipKey key = args.arg<trackedit::ClipKey>(0);
            clipAddCompatible(key);
        }
    });
    dispatcher()->reg(this, "mixhouse-clip-disable", [this](const muse::actions::ActionData& args) {
        if (args.count() != 1 || !clipGain()) {
            return;
        }
        const trackedit::ClipKey key = args.arg<trackedit::ClipKey>(0);
        // The selected clips go with the one clicked (Ableton's Deactivate on a selection).
        std::vector<trackedit::ClipKey> keys { key };
        if (selectionController()) {
            const trackedit::ClipKeyList selected = selectionController()->selectedClips();
            const auto same = [&](const trackedit::ClipKey& a, const trackedit::ClipKey& b) {
                return a.trackId == b.trackId && a.itemId == b.itemId;
            };
            if (std::any_of(selected.begin(), selected.end(), [&](const trackedit::ClipKey& k) { return same(k, key); })) {
                for (const trackedit::ClipKey& k : selected) {
                    if (!same(k, key)) {
                        keys.push_back(k);
                    }
                }
            }
        }
        const bool disable = !clipGain()->clipDisabled(key);
        const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
        for (const trackedit::ClipKey& k : keys) {
            if (clipGain()->setClipDisabled(k, disable) && project) {
                project->notifyAboutClipChanged(project->clip(k));
            }
        }
        if (projectHistory()) {
            projectHistory()->pushHistoryState(disable ? "Disabled clip" : "Enabled clip",
                                               disable ? "Disable" : "Enable");
        }
    });
    dispatcher()->reg(this, "mixhouse-clip-stems", [this](const muse::actions::ActionData& args) {
        if (args.count() == 1 && stemsReady()) {
            const trackedit::ClipKey key = args.arg<trackedit::ClipKey>(0);
            const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
            if (project) {
                const trackedit::Clip clip = project->clip(key);
                stems()->splitRangeIntoStems(key, clip.startTime, clip.endTime);
            }
        }
    });
    dispatcher()->reg(this, "mixhouse-save-stem-drums", [this]() { saveSelectionStems({ 0 }); });
    dispatcher()->reg(this, "mixhouse-save-stem-bass", [this]() { saveSelectionStems({ 1 }); });
    dispatcher()->reg(this, "mixhouse-save-stem-vocals", [this]() { saveSelectionStems({ 2 }); });
    dispatcher()->reg(this, "mixhouse-save-stem-other", [this]() { saveSelectionStems({ 3 }); });
    dispatcher()->reg(this, "mixhouse-save-stems-all", [this]() { saveSelectionStems({ 0, 1, 2, 3 }); });
    // The selected part itself: one stem out, or all four onto stem lanes.
    const char* removeCodes[] = { "mixhouse-remove-drums", "mixhouse-remove-bass", "mixhouse-remove-vocals", "mixhouse-remove-other" };
    for (int stem = 0; stem < 4; ++stem) {
        dispatcher()->reg(this, removeCodes[stem], [this, stem]() {
            if (!stemsReady()) {
                return;
            }
            if (const auto target = selectedSampleTarget()) {
                stems()->removeStemFromRange(target->clip, target->t0, target->t1, stem);
            }
        });
    }
    dispatcher()->reg(this, "mixhouse-split-selection-stems", [this]() {
        if (!stemsReady()) {
            return;
        }
        if (const auto target = selectedSampleTarget()) {
            stems()->splitRangeIntoStems(target->clip, target->t0, target->t1);
        }
    });
    dispatcher()->reg(this, "mixhouse-split-grid-jumps", [this]() {
        const int cuts = beatGrids()->splitSelectedAtGridJumps();
        if (cuts > 0) {
            projectHistory()->pushHistoryState(muse::trc("mixhouse", "Split at grid jumps"),
                                               muse::trc("mixhouse", "Split at grid jumps"));
        } else if (interactive()) {
            interactive()->info(muse::trc("mixhouse", "No grid jumps"),
                                muse::trc("mixhouse", "The beats of this song stay on its grid: nothing to split."));
        }
    });
    // Marc Mont: the clip menu's "Set IN/OUT anchor here", from the keyboard, at the playhead.
    for (const int which : { 0, 1 }) {
        dispatcher()->reg(this, which == 0 ? "mixhouse-anchor-in-playhead" : "mixhouse-anchor-out-playhead", [this, which]() {
            const auto playback = globalContext() ? globalContext()->playbackState() : nullptr;
            if (!playback) {
                return;
            }
            if (!beatGrids()->setSelectedClipAnchorAt(which, playback->playbackPosition().to_double()) && interactive()) {
                interactive()->info(muse::trc("mixhouse", "No song under the playhead"),
                                    muse::trc("mixhouse", "Select a song in the timeline and put the playhead over it."));
            }
        });
    }
    dispatcher()->reg(this, "mixhouse-edit-beatgrid", [this]() {
        const int trackId = beatGrids()->selectedLibraryTrackId();
        if (trackId >= 0 && gridEditor()) {
            gridEditor()->openTrack(trackId);
        }
    });
    dispatcher()->reg(this, "mixhouse-broadcast", [this]() {
        // The tower opens the operator's window; going on air is a button in
        // there, so a stray click on the header cannot cut the listeners off.
        if (!interactive()) {
            return;
        }
        // There is one HUD, not one per click: a second press brings the window
        // that is already up back to the front.
        const muse::Uri hud("audacity://broadcast/hud");
        if (interactive()->isOpened(hud).val) {
            interactive()->raise(muse::UriQuery(hud.toString()));
            return;
        }
        interactive()->open(hud);
    });
    dispatcher()->reg(this, "mixhouse-perform", [this]() {
        if (performance()) {
            performance()->togglePerform();
        }
    });
    dispatcher()->reg(this, "mixhouse-reset-performance", [this]() {
        if (performance()) {
            performance()->resetPerformance();
        }
    });
    dispatcher()->reg(this, "mixhouse-perform-from-playhead", [this]() {
        const auto playback = globalContext() ? globalContext()->playbackState() : nullptr;
        if (performance() && playback) {
            performance()->setPerformancePosition(playback->playbackPosition());
        }
    });
    dispatcher()->reg(this, "mixhouse-mix-now", [this]() {
        const auto playback = globalContext() ? globalContext()->playbackState() : nullptr;
        if (!transitions() || !playback) {
            return;
        }
        // While the audience is listening Mix Now works on the performance
        // (MixMeister's Mix Performance Now); otherwise on the preview.
        const double at = performance() && performance()->performing()
                          ? performance()->performancePosition().to_double()
                          : playback->playbackPosition().to_double();
        const std::string what = transitions()->mixNow(at);
        if (!what.empty()) {
            LOGI() << what;
        }
    });
    // GlideMix: after an export, the tracklist beside the file (sent by the exporter).
    dispatcher()->reg(this, "mixhouse-write-tracklist", [this](const muse::actions::ActionData& args) {
        if (args.count() < 3) {
            return;
        }
        const MixHouseTracklist tracklist(iocContext());
        tracklist.write(args.arg<std::string>(0), args.arg<double>(1), args.arg<double>(2));
    });
    dispatcher()->reg(this, "mixhouse-embed-artwork", [](const muse::actions::ActionData& args) {
        if (args.count() < 2) {
            return;
        }
        embedCoverArt(QString::fromStdString(args.arg<std::string>(0)), QString::fromStdString(args.arg<std::string>(1)));
    });
    dispatcher()->reg(this, "mixhouse-keylock", [this]() {
        libraryConfiguration()->setKeylockEnabled(!libraryConfiguration()->keylockEnabled());
    });
    dispatcher()->reg(this, "mixhouse-link", []() {
        if (MixHouseLink* link = MixHouseLink::instance()) {
            link->setEnabled(!link->enabled());
        }
    });
    dispatcher()->reg(this, "mixhouse-sync", [this]() {
        libraryConfiguration()->setSyncEnabled(!libraryConfiguration()->syncEnabled());
    });
    dispatcher()->reg(this, "mixhouse-tempo-lane", [this]() {
        libraryConfiguration()->setTempoLaneVisible(!libraryConfiguration()->tempoLaneVisible());
    });
    dispatcher()->reg(this, "mixhouse-energy-lane", [this]() {
        libraryConfiguration()->setEnergyLaneVisible(!libraryConfiguration()->energyLaneVisible());
    });
}

void MixHouseActionsController::addFilesToLibrary()
{
    static const std::vector<std::string> filter = {
        "Audio Files (*.mp3 *.m4a *.aac *.flac *.ogg *.opus *.wav *.aiff *.aif *.wv)"
    };
    const muse::io::paths_t files = interactive()->selectOpeningFilesSync(
        muse::trc("mixhouse", "Choose audio files to add to the library"), muse::io::path_t(), filter);
    if (files.empty()) {
        return;   // picker cancelled
    }
    QStringList paths;
    for (const muse::io::path_t& f : files) {
        paths << f.toQString();
    }
    const IMixHouseLibrary::ScanResult result = library()->addTracksFromFiles(paths);
    if (!result.error.isEmpty()) {
        interactive()->error(muse::trc("mixhouse", "Couldn't add files to library"), result.error.toStdString());
        return;
    }
    std::string text = muse::qtrc("mixhouse",
        "Scanned %1 file(s): %2 added to the library, %3 already known.")
        .arg(result.scanned).arg(result.added).arg(result.skipped).toStdString();
    if (result.unreadable > 0) {
        text += "\n" + muse::qtrc("mixhouse", "%n file(s) could not be read as audio (damaged or not audio) and were not added.",
                                    nullptr, result.unreadable).toStdString();
    }
    interactive()->info(muse::trc("mixhouse", "Library scan finished"), text);
}

void MixHouseActionsController::addFolderToLibrary()
{
    const muse::io::path_t folder = interactive()->selectDirectory(
        muse::trc("mixhouse", "Choose a music folder to add"),
        muse::io::path_t(QStandardPaths::writableLocation(QStandardPaths::MusicLocation)));
    if (folder.empty()) {
        return;   // picker cancelled
    }
    // GlideMix: not the whole disk, a volume or the home folder -- a scan of
    // those walks everything (other apps' data, external disks) for hours.
    const QString path = QDir::cleanPath(folder.toQString());
    if (mixHouseFolderTooBroadToScan(path)) {
        interactive()->warning(muse::trc("mixhouse", "Choose your music folder"),
                               muse::qtrc("mixhouse", "%1 is much more than a music folder -- scanning it would go through "
                                                      "everything on it. Choose the folder your music is in.")
                               .arg(path).toStdString());
        return;
    }
    // In the background, as the library's own Add Folder does: a big folder
    // used to freeze the app until the scan finished. The library panel
    // reports the result when it is done.
    library()->startBackgroundScan({ path });
}

void MixHouseActionsController::reportImportResult(const QString& sourceLabel, const MixHouseImportResult& result)
{
    if (!result.error.isEmpty()) {
        interactive()->error(muse::qtrc("mixhouse", "Couldn't import from %1").arg(sourceLabel).toStdString(),
                              result.error.toStdString());
        return;
    }
    const std::string text = muse::qtrc("mixhouse",
        "%1 playlist(s) created, %2 track(s) referenced: %3 added to the library, %4 already known.")
        .arg(result.playlistsCreated).arg(result.tracksFound).arg(result.tracksAdded).arg(result.tracksMatched)
        .toStdString();
    interactive()->info(muse::qtrc("mixhouse", "Import from %1 finished").arg(sourceLabel).toStdString(), text);
}

void MixHouseActionsController::importFromRekordboxXml()
{
    static const std::vector<std::string> filter = { "Rekordbox XML (*.xml)" };
    const muse::io::path_t file = interactive()->selectOpeningFileSync(
        muse::trc("mixhouse", "Choose a rekordbox.xml collection export"), muse::io::path_t(), filter);
    if (file.empty()) {
        return;
    }
    // GlideMix: shown under Rekordbox XML in the library, read-only and live,
    // like Lexicon DJ; its row there shows the reading progress. (Copying
    // it into the Music Collection is on that row's right-click menu.)
    if (rekordboxXml()) {
        rekordboxXml()->setPath(file.toQString());
    }
}

void MixHouseActionsController::importFromItunesXml()
{
    static const std::vector<std::string> filter = { "iTunes Library XML (*.xml)" };
    const muse::io::path_t file = interactive()->selectOpeningFileSync(
        muse::trc("mixhouse", "Choose an iTunes/Apple Music Library.xml export"), muse::io::path_t(), filter);
    if (file.empty()) {
        return;
    }
    reportImportResult(QStringLiteral("iTunes/Apple Music"), library()->importFromItunesXml(file.toQString()));
}

namespace {
QString keyName(int keyId)
{
    static const char* kNames[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    if (keyId >= 1 && keyId <= 12) {
        return QString::fromLatin1(kNames[keyId - 1]);
    }
    if (keyId >= 13 && keyId <= 24) {
        return QString::fromLatin1(kNames[keyId - 13]) + QStringLiteral("m");
    }
    return {};
}
}

std::optional<MixHouseActionsController::SampleTarget> MixHouseActionsController::selectedSampleTarget()
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !selectionController() || !interactive()) {
        return std::nullopt;
    }
    SampleTarget target;
    target.t0 = selectionController()->dataSelectedStartTime();
    target.t1 = selectionController()->dataSelectedEndTime();
    const trackedit::TrackIdList tracks = selectionController()->selectedTracks();
    bool found = false;
    if (tracks.size() == 1 && target.t1 > target.t0) {
        for (const trackedit::Clip& c : project->clipList(tracks.front())) {
            if (c.startTime <= target.t0 + 1e-6 && c.endTime >= target.t1 - 1e-6) {
                target.clip = c.key;
                target.title = c.title.toQString().trimmed();
                found = true;
            }
        }
    }
    if (!found) {
        interactive()->info(muse::trc("mixhouse", "Select part of a song"),
                            muse::trc("mixhouse", "Select part of one song first: one lane, inside one song."));
        return std::nullopt;
    }
    if (target.title.isEmpty()) {
        target.title = muse::qtrc("mixhouse", "Sample");
    }

    // " 8 bars 128 Am": the Samples tab reads tempo and key from the name.
    const double bpm = beatGrids() ? beatGrids()->clipBpm(target.clip) : 0.0;
    double ratio = 1.0;
    IMixHouseBeatGrids::ClipFile file;
    if (beatGrids() && beatGrids()->clipFile(target.clip, file) && file.ratio > 0.0) {
        ratio = file.ratio;
    }
    QString keyText;
    const int libraryId = beatGrids() ? beatGrids()->clipLibraryTrackId(target.clip) : -1;
    if (libraryId >= 0 && library()) {
        IMixHouseLibrary::TrackQuery query;
        query.trackIds = { libraryId };
        const MixHouseTrackList tracksFound = library()->tracks(query);
        if (!tracksFound.empty()) {
            keyText = keyName(tracksFound.front().keyId);
        }
    }
    if (bpm > 0.0) {
        const double bars = (target.t1 - target.t0) / ratio * bpm / 240.0;
        const double whole = std::round(bars);
        if (whole >= 1.0 && std::abs(bars - whole) <= 0.03 * whole) {
            target.suffix += QStringLiteral(" %1 %2").arg(int(whole)).arg(whole == 1.0 ? QStringLiteral("bar") : QStringLiteral("bars"));
        }
        target.suffix += QStringLiteral(" ") + (std::abs(bpm - std::round(bpm)) < 0.05 ? QString::number(int(std::round(bpm)))
                                                                                       : QString::number(bpm, 'f', 1));
    }
    if (!keyText.isEmpty()) {
        target.suffix += QStringLiteral(" ") + keyText;
    }
    static const QRegularExpression kBad(QStringLiteral("[\\\\/:*?\"<>|%]"));
    target.title.replace(kBad, QStringLiteral(" "));
    return target;
}

QString MixHouseActionsController::samplesRoot() const
{
    // Next to the projects (Chris): the Projects folder's Samples folder.
    return (projectConfiguration() ? projectConfiguration()->userProjectsPath().toQString()
            : QDir::homePath() + QStringLiteral("/Music/Glidemix/Projects")) + QStringLiteral("/Samples");
}

QString MixHouseActionsController::sampleSaveFolder() const
{
    // The folder picked last time, else the Samples folder. The save window is
    // the system's own, so any other folder can be picked there.
    QString dir = QSettings().value(QStringLiteral("GlideMix/saveSampleFolder")).toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        dir = samplesRoot();
    }
    QDir().mkpath(dir);
    return dir;
}

void MixHouseActionsController::samplesSavedIn(const QString& savedDir)
{
    QSettings().setValue(QStringLiteral("GlideMix/saveSampleFolder"), savedDir);
    addToSamplesTab(savedDir);
}

void MixHouseActionsController::addToSamplesTab(const QString& savedDir)
{
    // Into the Samples tab: a folder it already scans picks the files up on a
    // rescan; else the Samples folder (or the chosen folder) is added.
    if (!samples()) {
        return;
    }
    const auto real = [](const QString& p) {
        const QString canonical = QFileInfo(p).canonicalFilePath();
        return canonical.isEmpty() ? p : canonical;
    };
    const QString saved = real(savedDir);
    const QString root = real(samplesRoot());
    // addFolder() just rescans when the folder is already covered.
    samples()->addFolder(saved == root || saved.startsWith(root + QStringLiteral("/")) ? samplesRoot() : savedDir);
}

void MixHouseActionsController::saveSelectionAsSample()
{
    const std::optional<SampleTarget> target = selectedSampleTarget();
    if (!target) {
        return;
    }
    const QString dir = sampleSaveFolder();
    const QString name = target->title + target->suffix;
    QString proposed = dir + QStringLiteral("/") + name + QStringLiteral(".wav");
    for (int n = 2; QFileInfo::exists(proposed); ++n) {
        proposed = dir + QStringLiteral("/") + name + QStringLiteral(" %1.wav").arg(n);
    }
    const muse::io::path_t chosen = interactive()->selectSavingFileSync(muse::trc("mixhouse", "Save as sample"),
                                                                        muse::io::path_t(proposed),
                                                                        { muse::trc("mixhouse", "WAV audio") + " (*.wav)" });
    if (chosen.empty()) {
        return;
    }
    QString path = chosen.toQString();
    if (!path.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".wav");
    }
    double seconds = 0.0;
    if (!writeClipRangeWav(globalContext(), target->clip, target->t0, target->t1, path, seconds)) {
        interactive()->error(muse::trc("mixhouse", "Save as sample"),
                             muse::qtrc("mixhouse", "Could not write %1.").arg(path).toStdString());
        return;
    }
    samplesSavedIn(QFileInfo(path).absolutePath());
    if (activity()) {
        activity()->post(muse::qtrc("mixhouse", "Saved as sample: %1").arg(QFileInfo(path).completeBaseName()));
    }
    LOGI() << "GlideMix save as sample: " << path << " (" << seconds << " s)";
}

bool MixHouseActionsController::stemsReady()
{
    if (stems() && stems()->available()) {
        return true;
    }
    if (interactive()) {
        interactive()->info(muse::trc("mixhouse", "Stems"),
                            muse::trc("mixhouse", "Install the stems engine in Preferences > Stems first."));
    }
    return false;
}

void MixHouseActionsController::saveSelectionStems(const QList<int>& which)
{
    if (!stemsReady()) {
        return;
    }
    const std::optional<SampleTarget> target = selectedSampleTarget();
    if (!target) {
        return;
    }
    // One stem: the save window with its name; all four: a folder.
    QString dir;
    QString pattern = target->title + QStringLiteral(" %1") + target->suffix;
    if (which.size() == 1) {
        static const char* kWords[] = { "drums", "bass", "vocals", "other" };
        const QString name = pattern.arg(QLatin1String(kWords[std::clamp(which.front(), 0, 3)]));
        const QString folder = sampleSaveFolder();
        QString proposed = folder + QStringLiteral("/") + name + QStringLiteral(".wav");
        for (int n = 2; QFileInfo::exists(proposed); ++n) {
            proposed = folder + QStringLiteral("/") + name + QStringLiteral(" %1.wav").arg(n);
        }
        const muse::io::path_t chosen = interactive()->selectSavingFileSync(
            muse::trc("mixhouse", "Save stem as sample"), muse::io::path_t(proposed),
            { muse::trc("mixhouse", "WAV audio") + " (*.wav)" });
        if (chosen.empty()) {
            return;
        }
        const QFileInfo fi(chosen.toQString());
        dir = fi.absolutePath();
        // The name typed there, with the stem word kept where it was.
        // Exactly the name chosen there (the window already asked about
        // replacing a file).
        pattern = fi.completeBaseName().replace(QStringLiteral("%"), QStringLiteral(" "));
    } else {
        const muse::io::path_t chosen = interactive()->selectDirectory(muse::trc("mixhouse", "Save the stems as samples in"),
                                                                       muse::io::path_t(sampleSaveFolder()));
        if (chosen.empty()) {
            return;
        }
        dir = chosen.toQString();
    }
    const QStringList written = stems()->saveClipStems(target->clip, target->t0, target->t1, which, dir, pattern,
                                                       which.size() > 1);
    if (written.isEmpty()) {
        return;
    }
    samplesSavedIn(dir);
    if (activity()) {
        activity()->post(written.size() == 1
                         ? muse::qtrc("mixhouse", "Saved as sample: %1").arg(QFileInfo(written.front()).completeBaseName())
                         : muse::qtrc("mixhouse", "Saved %1 stems as samples").arg(written.size()));
    }
}

QString MixHouseActionsController::clipSourcePath(const trackedit::ClipKey& key) const
{
    // A library song: its file. A sample: the Samples tab entry of that name,
    // then the beats and saved samples next to the projects.
    IMixHouseBeatGrids::ClipFile file;
    if (beatGrids() && beatGrids()->clipFile(key, file) && QFileInfo::exists(file.path)) {
        return file.path;
    }
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project) {
        return {};
    }
    const QString title = project->clip(key).title.toQString();
    if (samples()) {
        for (const MixHouseSample& s : samples()->samples()) {
            if (s.name == title && QFileInfo::exists(s.path)) {
                return s.path;
            }
        }
    }
    for (const QString& dir : { samplesRoot() + QStringLiteral("/Beats"), samplesRoot() }) {
        for (const QString& ext : { QStringLiteral(".wav"), QStringLiteral(".aif"), QStringLiteral(".mp3") }) {
            const QString p = dir + QStringLiteral("/") + title + ext;
            if (QFileInfo::exists(p)) {
                return p;
            }
        }
    }
    return {};
}

void MixHouseActionsController::clipShowInFinder(const trackedit::ClipKey& key)
{
    const QString path = clipSourcePath(key);
    if (path.isEmpty()) {
        if (interactive()) {
            interactive()->info(muse::trc("mixhouse", "Show in Finder"),
                                muse::trc("mixhouse", "GlideMix doesn't know which file this clip came from (it may have been recorded or edited)."));
        }
        return;
    }
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"), { QStringLiteral("/select,"), QDir::toNativeSeparators(path) });
#else
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), { QStringLiteral("-R"), path });
#endif
}

void MixHouseActionsController::clipExport(const trackedit::ClipKey& key)
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !interactive()) {
        return;
    }
    // The clip as trimmed on the timeline, at its own speed, as a WAV.
    const trackedit::Clip clip = project->clip(key);
    QString name = clip.title.toQString().trimmed();
    static const QRegularExpression kBad(QStringLiteral("[\\\\/:*?\"<>|]"));
    name.replace(kBad, QStringLiteral(" "));
    if (name.isEmpty()) {
        name = muse::qtrc("mixhouse", "Clip");
    }
    const QString dir = sampleSaveFolder();
    const muse::io::path_t chosen = interactive()->selectSavingFileSync(muse::trc("mixhouse", "Export audio"),
                                                                        muse::io::path_t(dir + QStringLiteral("/") + name + QStringLiteral(".wav")),
                                                                        { muse::trc("mixhouse", "WAV audio") + " (*.wav)" });
    if (chosen.empty()) {
        return;
    }
    QString path = chosen.toQString();
    if (!path.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".wav");
    }
    double seconds = 0.0;
    if (!writeClipRangeWav(globalContext(), key, clip.startTime, clip.endTime, path, seconds)) {
        interactive()->error(muse::trc("mixhouse", "Export audio"), muse::qtrc("mixhouse", "Could not write %1.").arg(path).toStdString());
        return;
    }
    if (activity()) {
        activity()->post(muse::qtrc("mixhouse", "Exported: %1").arg(QFileInfo(path).fileName()));
    }
#ifndef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), { QStringLiteral("-R"), path });
#else
    QProcess::startDetached(QStringLiteral("explorer.exe"), { QStringLiteral("/select,"), QDir::toNativeSeparators(path) });
#endif
}

// ---- Vibseek's clip menu (ideas from its right-click, our own code) ----

const MixHouseSample* MixHouseActionsController::sampleForClip(const trackedit::ClipKey& key) const
{
    const QString path = clipSourcePath(key);
    if (path.isEmpty() || !samples()) {
        return nullptr;
    }
    const QString real = QFileInfo(path).canonicalFilePath();
    for (const MixHouseSample& s : samples()->samples()) {
        if (s.path == path || s.path == real) {
            return &s;
        }
    }
    return nullptr;
}

void MixHouseActionsController::openSamplesTab(const QString& path, bool similar)
{
    if (samples()) {
        samples()->showInSamplesTab(path, similar);
    }
    dispatcher()->dispatch("dock-set-open", muse::actions::ActionData::make_arg2<QString, bool>(
                               QStringLiteral("mixHouseSamplesPanel"), true));
}

void MixHouseActionsController::clipShowInSamplesTab(const trackedit::ClipKey& key, bool similar)
{
    const QString path = clipSourcePath(key);
    if (path.isEmpty()) {
        if (activity()) {
            activity()->post(muse::qtrc("mixhouse", "This sound isn't in the Samples tab (it may have been recorded or edited)"));
        }
        return;
    }
    if (!sampleForClip(key)) {
        // A file of its own (a beat, a saved sample) the tab doesn't scan yet.
        addToSamplesTab(QFileInfo(path).absolutePath());
    }
    openSamplesTab(path, similar);
}

QString MixHouseActionsController::renderClipToFolder(const trackedit::ClipKey& key, const QString& dir, const QString& suffix)
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project) {
        return {};
    }
    const trackedit::Clip clip = project->clip(key);
    QString name = clip.title.toQString().trimmed();
    static const QRegularExpression kBad(QStringLiteral("[\\\\/:*?\"<>|]"));
    name.replace(kBad, QStringLiteral(" "));
    if (name.isEmpty()) {
        name = muse::qtrc("mixhouse", "Clip");
    }
    QDir().mkpath(dir);
    QString path = dir + QStringLiteral("/") + name + suffix + QStringLiteral(".wav");
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = dir + QStringLiteral("/") + name + suffix + QStringLiteral(" %1.wav").arg(n);
    }
    double seconds = 0.0;
    if (!writeClipRangeWav(globalContext(), key, clip.startTime, clip.endTime, path, seconds)) {
        if (activity()) {
            activity()->post(muse::qtrc("mixhouse", "Could not write %1.").arg(path));
        }
        return {};
    }
    return path;
}

void MixHouseActionsController::clipResample(const trackedit::ClipKey& key)
{
    // The clip as trimmed on the timeline becomes a new sample of its own
    // (Samples > Resampled), ready in the Samples tab.
    const QString path = renderClipToFolder(key, samplesRoot() + QStringLiteral("/Resampled"), QString());
    if (path.isEmpty()) {
        return;
    }
    addToSamplesTab(samplesRoot());
    openSamplesTab(path, false);
    if (activity()) {
        activity()->post(muse::qtrc("mixhouse", "Resampled: %1").arg(QFileInfo(path).completeBaseName()));
    }
}

void MixHouseActionsController::clipVariations(const trackedit::ClipKey& key)
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project) {
        return;
    }
    const trackedit::Clip clip = project->clip(key);
    const QString base = QFileInfo(renderClipToFolder(key, QDir::tempPath() + QStringLiteral("/GlideMixVariations"), QString())).filePath();
    if (base.isEmpty() || !QFileInfo(base).isFile()) {
        return;
    }
    // A loop keeps its length (its tempo still fits); a one-shot may change length.
    const MixHouseSample* known = sampleForClip(key);
    const SampleNameInfo parsed = parseSampleName(clip.title.toQString());
    const bool loop = known ? known->loop
                      : (parsed.type == SampleNameInfo::Type::Loop || parsed.bpm > 0.0 || clip.endTime - clip.startTime > 2.0);
    const QString name = QFileInfo(base).completeBaseName();
    const QString folder = samplesRoot() + QStringLiteral("/Variations/") + name;
    const QStringList made = makeSoundVariations(base, folder, name, loop);
    QFile::remove(base);
    if (made.isEmpty()) {
        if (activity()) {
            activity()->post(muse::qtrc("mixhouse", "Could not make variations of this sound"));
        }
        return;
    }
    addToSamplesTab(samplesRoot());
    openSamplesTab(made.front(), false);
    if (activity()) {
        activity()->post(muse::qtrc("mixhouse", "%n variation(s) in Samples > Variations", nullptr, int(made.size())));
    }
}

void MixHouseActionsController::clipSendToBeat(const trackedit::ClipKey& key)
{
    // Vibseek's "Send to Sampler": the sound becomes a row of the Beat window.
    QString path = clipSourcePath(key);
    if (path.isEmpty()) {
        path = renderClipToFolder(key, samplesRoot() + QStringLiteral("/Resampled"), QString());
    }
    if (path.isEmpty()) {
        return;
    }
    queueBeatSample(path);
    muse::UriQuery beat("audacity://mixhouse/beat");
    beat.addParam("sampleLine", muse::Val(static_cast<int>(sampleLineFor(key))));
    interactive()->open(beat);
}

au::trackedit::TrackId MixHouseActionsController::sampleLineFor(const trackedit::ClipKey& key)
{
    // A sample's own line; for a song, the line right under it (made when missing,
    // like the SAMPLES button does).
    const auto prj = globalContext() ? globalContext()->currentProject() : nullptr;
    const auto editPrj = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!prj || !editPrj || !prj->viewState()) {
        return -1;
    }
    const auto vs = prj->viewState();
    if (vs->isSampleLine(key.trackId)) {
        return key.trackId;
    }
    const std::vector<trackedit::TrackId> ids = editPrj->trackIdList();
    const auto at = std::find(ids.begin(), ids.end(), key.trackId);
    if (at == ids.end()) {
        return -1;
    }
    if (at + 1 != ids.end() && vs->isSampleLine(*(at + 1))) {
        return *(at + 1);
    }
    if (!trackeditInteraction() || !trackeditInteraction()->newStereoTrack()) {
        return -1;
    }
    trackedit::TrackId added = -1;
    for (const auto id : editPrj->trackIdList()) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
            added = id;
        }
    }
    if (added == -1) {
        return -1;
    }
    vs->setSampleLine(added, true);
    trackeditInteraction()->changeTrackTitle(added, muse::String(trackedit::SAMPLE_LINE_TITLE));
    trackeditInteraction()->moveTracksTo({ added }, static_cast<int>(std::distance(ids.begin(), at)) + 1);
    vs->setTrackHeight(added, 110);
    return added;
}

void MixHouseActionsController::clipAddCompatible(const trackedit::ClipKey& key)
{
    const auto prj = globalContext() ? globalContext()->currentProject() : nullptr;
    const auto editPrj = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!prj || !editPrj || !samples() || !prj->viewState()) {
        return;
    }
    const auto post = [&](const QString& text) {
        if (activity()) {
            activity()->post(text);
        }
    };
    const trackedit::Clip clip = editPrj->clip(key);
    const bool onSampleLine = prj->viewState()->isSampleLine(key.trackId);
    const MixHouseSample* ref = onSampleLine ? sampleForClip(key) : nullptr;

    // The song: this clip, or the lane right above the sample line.
    trackedit::TrackId song = key.trackId;
    if (onSampleLine) {
        const std::vector<trackedit::TrackId> ids = editPrj->trackIdList();
        const auto at = std::find(ids.begin(), ids.end(), key.trackId);
        song = at != ids.end() && at != ids.begin() ? *(at - 1) : trackedit::TrackId(-1);
    }
    const trackedit::TimeSignature ts = editPrj->timeSignature();
    const double tempo = ts.tempo > 0.0 ? ts.tempo : 120.0;
    const double bar = 60.0 / tempo * (4.0 / std::max(1, ts.lower)) * std::max(1, ts.upper);
    const auto state = globalContext()->playbackState();
    const double playhead = state ? state->playbackPosition().to_double() : 0.0;
    // Where it goes: after a sample; on a song, the bar under the playhead
    // when it is inside the song, else the song's first bar.
    double start = onSampleLine ? std::ceil(clip.endTime / bar - 1e-3) * bar
                   : (playhead >= clip.startTime && playhead < clip.endTime ? std::floor(playhead / bar + 1e-6) * bar
                      : std::ceil(clip.startTime / bar - 1e-3) * bar);
    int songKey = 0;
    for (const trackedit::Clip& c : editPrj->clipList(song)) {
        if (c.startTime <= start + 1e-3 && start < c.endTime) {
            const int id = beatGrids() ? beatGrids()->clipLibraryTrackId(c.key) : -1;
            if (id >= 0 && library()) {
                IMixHouseLibrary::TrackQuery query;
                query.trackIds = { id };
                const MixHouseTrackList found = library()->tracks(query);
                if (!found.empty()) {
                    songKey = found.front().keyId;
                }
            }
        }
    }
    // The pool: loops for a song (or the same kind as the sample, of its
    // category), in a key that mixes with the song; keyless ones (drums)
    // when nothing keyed fits.
    std::vector<const MixHouseSample*> keyed, keyless;
    for (const MixHouseSample& s : samples()->samples()) {
        if (s.missing || (ref && s.path == ref->path)) {
            continue;
        }
        if (ref ? (s.loop != ref->loop || s.category != ref->category) : !s.loop) {
            continue;
        }
        if (s.keyId > 0) {
            if (songKey > 0 && IMixHouseKeyMatch::shortestStepsToCompatibleKey(s.keyId, songKey) == 0) {
                keyed.push_back(&s);
            }
        } else {
            keyless.push_back(&s);
        }
    }
    const std::vector<const MixHouseSample*>& pool = !keyed.empty() ? keyed : keyless;
    if (pool.empty()) {
        post(muse::qtrc("mixhouse", "No sound in the Samples tab fits here (add sample folders, or analyse the song's key)"));
        return;
    }
    const MixHouseSample* pick = pool[size_t(QRandomGenerator::global()->bounded(int(pool.size())))];

    const trackedit::TrackId line = sampleLineFor(key);
    if (line < 0) {
        return;
    }
    const double length = pick->loop && pick->bpm > 0.0 && pick->bars > 0 ? pick->bars * bar : pick->durationSec;
    for (bool moved = true; moved;) {
        moved = false;
        for (const trackedit::Clip& c : editPrj->clipList(line)) {
            if (c.startTime < start + length - 1e-3 && c.endTime > start + 1e-3) {
                start = std::ceil(c.endTime / bar - 1e-3) * bar;
                moved = true;
            }
        }
    }
    std::vector<trackedit::ClipKey> before;
    for (const trackedit::Clip& c : editPrj->clipList(line)) {
        before.push_back(c.key);
    }
    prj->importIntoTracks({ muse::io::path_t(pick->path) }, { line }, start, { false });
    trackedit::ClipKeyList created;
    for (const trackedit::Clip& c : editPrj->clipList(line)) {
        if (std::none_of(before.begin(), before.end(), [&](const trackedit::ClipKey& k) { return k.itemId == c.key.itemId; })) {
            created.push_back(c.key);
        }
    }
    if (created.empty()) {
        return;
    }
    prepareSampleClips(globalContext(), created, pick->loop ? pick->bpm : 0.0);
    if (projectHistory()) {
        projectHistory()->modifyState();
    }
    post(muse::qtrc("mixhouse", "Added: %1").arg(pick->name));
}

