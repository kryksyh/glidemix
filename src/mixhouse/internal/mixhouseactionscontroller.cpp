/*
 * MixHouse DJ 2
 */
#include "mixhouselink.h"
#include "mixhousetracklist.h"
#include "mixhouse/mixhousecoverart.h"
#include "mixhouseactionscontroller.h"

#include <QDir>
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
