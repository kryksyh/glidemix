/*
 * MixHouse DJ 2
 */
#pragma once

#include "ui/iuiactionsmodule.h"
#include "broadcast/ibroadcaster.h"
#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "mixhouse/imixhouselibraryconfiguration.h"
#include "playback/iperformance.h"

namespace au::mixhouse {
/// The File-menu actions for MixHouse -- "Add Folder/File(s) to Library",
/// joined later by the Rekordbox/iTunes XML importers. Structurally this is
/// the exact same pattern every other module's File-menu contribution uses
/// (see au::project::ProjectUiActions for "project-import"): a static
/// UiActionList registered at runtime via IUiActionsRegister, with the
/// actual work reached through MixHouseActionsController's dispatcher
/// registration, not QML. Real mixxx's own equivalent is the Computer
/// sidebar's right-click "Add to Library" (library/browse/browsefeature.cpp)
/// -- this is a second, File-menu-native entry point into the identical,
/// already-proven IMixHouseLibrary::addTracksFromFolder() backend the
/// sidebar's own "Add Folder to Library..." already uses.
///
/// No enable/disable state needed: unlike most File-menu actions, these
/// don't depend on a project being open -- they write straight to the
/// library database, independent of the current Audacity project.
class MixHouseUiActions final : public muse::ui::IUiActionsModule, public muse::async::Asyncable
{
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;

public:
    void init();
    //! The context's performance, so PERFORM shows whether the audience is playing.
    void setPerformance(std::shared_ptr<playback::IPerformance> performance);
    //! Lights Re-enable while any automation line is overridden (Ableton's Control Bar button).
    void setAutomationOverridden(bool overridden);
    void setAutomationArmed(bool armed);

    const muse::ui::UiActionList& actionsList() const override;

    bool actionEnabled(const muse::ui::UiAction& act) const override;
    muse::async::Channel<muse::actions::ActionCodeList> actionEnabledChanged() const override;

    bool actionChecked(const muse::ui::UiAction& act) const override;
    muse::async::Channel<muse::actions::ActionCodeList> actionCheckedChanged() const override;

private:
    static const muse::ui::UiActionList m_actions;
    std::shared_ptr<playback::IPerformance> m_performance;
    bool m_automationOverridden = false;
    bool m_automationArmed = false;
    muse::GlobalInject<broadcast::IBroadcaster> m_broadcaster;
    muse::async::Channel<muse::actions::ActionCodeList> m_actionEnabledChanged;
    muse::async::Channel<muse::actions::ActionCodeList> m_actionCheckedChanged;
};
}
