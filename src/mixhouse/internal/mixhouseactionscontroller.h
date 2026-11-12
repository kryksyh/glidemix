/*
 * MixHouse DJ 2
 */
#pragma once

#include "modularity/ioc.h"
#include "framework/actions/actionable.h"
#include "framework/actions/iactionsdispatcher.h"
#include "framework/interactive/iinteractive.h"
#include "mixhouse/imixhouselibrary.h"
#include "mixhouse/imixhouserekordboxxml.h"
#include "mixhouse/imixhouselibraryconfiguration.h"
#include "broadcast/ibroadcaster.h"
#include "mixhouse/imixhousebeatgrids.h"
#include "mixhouse/imixhousegrideditor.h"
#include "mixhouse/imixhousetransitions.h"
#include "context/iglobalcontext.h"
#include "playback/iperformance.h"
#include "automation/iclipgaininteraction.h"
#include "trackedit/iprojecthistory.h"
#include "trackedit/itrackeditinteraction.h"
#include "trackedit/iselectioncontroller.h"
#include "project/iprojectconfiguration.h"
#include "mixhouse/imixhousesamples.h"
#include "mixhouse/imixhouseactivity.h"
#include "mixhouse/imixhousestems.h"
#include <optional>
#include "async/asyncable.h"

#include <functional>

namespace au::mixhouse {
/// The real handlers behind MixHouseUiActions' File-menu items, reached via
/// Audacity's native action dispatcher -- the same string-keyed action-code
/// -> callback registry every other File-menu action uses (mirrors
/// au::project::ProjectActionsController::importFiles for "project-import").
/// Talks to the SAME IMixHouseLibrary the sidebar's own right-click "Add
/// Folder to Library" already uses: this is a second, File-menu-native entry
/// point into identical, already-proven backend code, not a new import path.
class MixHouseActionsController : public muse::actions::Actionable, public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher { this };
    muse::ContextInject<IMixHouseLibrary> library { this };
    muse::ContextInject<IMixHouseRekordboxXml> rekordboxXml { this };
    muse::ContextInject<muse::IInteractive> interactive { this };
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;
    muse::GlobalInject<broadcast::IBroadcaster> broadcaster;
    muse::ContextInject<IMixHouseBeatGrids> beatGrids { this };
    muse::ContextInject<IMixHouseGridEditor> gridEditor { this };
    muse::ContextInject<IMixHouseTransitions> transitions { this };
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<playback::IPerformance> performance { this };
    muse::ContextInject<automation::IClipGainInteraction> clipGain { this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory { this };
    muse::ContextInject<trackedit::ISelectionController> selectionController { this };
    muse::GlobalInject<project::IProjectConfiguration> projectConfiguration;
    muse::GlobalInject<IMixHouseSamples> samples;
    muse::ContextInject<IMixHouseActivity> activity { this };
    muse::ContextInject<IMixHouseStems> stems { this };
    muse::ContextInject<trackedit::ITrackeditInteraction> trackeditInteraction { this };

public:
    //! Told whether any automation line is overridden (lights the Re-enable button).
    std::function<void(bool)> automationOverrideChanged;
    std::function<void(bool)> automationArmChanged;
    explicit MixHouseActionsController(const muse::modularity::ContextPtr& iocCtx);

    void init();

private:
    void addFilesToLibrary();
    void addFolderToLibrary();
    void importFromRekordboxXml();
    void importFromItunesXml();
    /// Samples B6: the selected part of one song, as a WAV in the Samples tab.
    void saveSelectionAsSample();
    /// Samples B7: stems of the selection (0 drums .. 3 other) saved as samples.
    void saveSelectionStems(const QList<int>& which);
    bool stemsReady();   ///< says how to install the engine when it is missing
    QString clipSourcePath(const trackedit::ClipKey& key) const;
    void clipShowInFinder(const trackedit::ClipKey& key);
    void clipExport(const trackedit::ClipKey& key);
    // Vibseek's clip menu, our own code (2026-11-12).
    void clipShowInSamplesTab(const trackedit::ClipKey& key, bool similar);
    void clipResample(const trackedit::ClipKey& key);
    void clipVariations(const trackedit::ClipKey& key);
    void clipSendToBeat(const trackedit::ClipKey& key);
    void clipAddCompatible(const trackedit::ClipKey& key);
    void addToSamplesTab(const QString& dir);
    void openSamplesTab(const QString& path, bool similar);
    QString renderClipToFolder(const trackedit::ClipKey& key, const QString& dir, const QString& suffix);
    trackedit::TrackId sampleLineFor(const trackedit::ClipKey& key);
    const MixHouseSample* sampleForClip(const trackedit::ClipKey& key) const;
    /// The selected part of one song and its name parts; says why when there is none.
    struct SampleTarget {
        trackedit::ClipKey clip;
        double t0 = 0.0;
        double t1 = 0.0;
        QString title;    ///< the song's name, safe for a file name
        QString suffix;   ///< " 8 bars 128 Am"
    };
    std::optional<SampleTarget> selectedSampleTarget();
    QString samplesRoot() const;
    QString sampleSaveFolder() const;
    void samplesSavedIn(const QString& dir);

    /// Shared by both XML importers: reports the real MixHouseImportResult
    /// via IInteractive, in the spirit of mixxx's own scan-summary dialogs.
    void reportImportResult(const QString& sourceLabel, const MixHouseImportResult& result);
};
}
