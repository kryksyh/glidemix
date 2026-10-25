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

    /// Shared by both XML importers: reports the real MixHouseImportResult
    /// via IInteractive, in the spirit of mixxx's own scan-summary dialogs.
    void reportImportResult(const QString& sourceLabel, const MixHouseImportResult& result);
};
}
