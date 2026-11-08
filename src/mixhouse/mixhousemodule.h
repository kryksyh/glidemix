/*
 * MixHouse DJ 2
 */
#pragma once

#include <memory>
#include "modularity/imodulesetup.h"

namespace au::mixhouse {
class MixHouseLibrary;
class MixHouseUiActions;
class MixHouseBeatGrids;
class MixHouseTempoMap;
class MixHouseTransitions;
class MixHouseRekordboxXml;
class MixHouseRekordboxPhrases;
class MixHouseActionsController;
class MixHouseLink;
class MixHouseLibraryConfiguration;
class MixHouseTransitionConfiguration;

class MixHouseModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override;
    // Module-level (not per-context) registration/init, for exactly one
    // export: IMixHouseLibraryConfiguration, registered via globalIoc() so
    // the separate `preferences` module's QML preferences model can resolve
    // it via muse::GlobalInject, the same way MusicPreferencesModel resolves
    // IImporterConfiguration -- the context-scoped ioc() IMixHouseLibrary
    // itself uses (MixHouseModuleContext below) isn't visible cross-module.
    void registerExports() override;
    void onInit(const muse::IApplication::RunMode& mode) override;
    void registerResources() override;
    void registerUiTypes() override;
    muse::modularity::IContextSetup* newContext(const muse::modularity::ContextPtr& ctx) const override;

private:
    std::shared_ptr<MixHouseLibraryConfiguration> m_libraryConfiguration;
    std::shared_ptr<MixHouseTransitionConfiguration> m_transitionConfiguration;
    std::shared_ptr<class MixHouseHdWaveforms> m_hdWaveforms;
    std::shared_ptr<class MixHouseLicence> m_licence;
};

class MixHouseModuleContext : public muse::modularity::IContextSetup
{
public:
    MixHouseModuleContext(const muse::modularity::ContextPtr& ctx)
        : muse::modularity::IContextSetup(ctx) {}

    void registerExports() override;
    void onInit(const muse::IApplication::RunMode& mode) override;

private:
    std::shared_ptr<MixHouseLibrary> m_library;
    std::shared_ptr<MixHouseActionsController> m_actionsController;
    std::shared_ptr<MixHouseLink> m_link;
    std::shared_ptr<MixHouseUiActions> m_uiActions;
    std::shared_ptr<MixHouseBeatGrids> m_beatGrids;
    std::shared_ptr<MixHouseTempoMap> m_tempoMap;
    std::shared_ptr<MixHouseTransitions> m_transitions;
    std::shared_ptr<MixHouseRekordboxXml> m_rekordboxXml;
    std::shared_ptr<MixHouseRekordboxPhrases> m_rekordboxPhrases;
    std::shared_ptr<class MixHouseFolderWatcher> m_folderWatcher;
};
}
