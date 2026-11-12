/*
 * MixHouse DJ 2
 */
#include "mixhouselink.h"
#include "mixhousetracklist.h"
#include "mixhouse/mixhousecoverart.h"
#include "mixhouseactionscontroller.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>
#include <optional>
#include <QSettings>
#include "mixhouseprojectaligner.h"
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
