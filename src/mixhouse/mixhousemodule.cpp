/*
 * MixHouse DJ 2
 *
 * The first MixHouse module inside Audacity. Registers the library reader as an
 * injectable service and exposes its track list to QML. Added alongside
 * Audacity's own modules -- none of theirs is modified.
 */
#include "mixhousemodule.h"

#include <QQmlEngine>

#include "modularity/ioc.h"
#include "framework/ui/iuiengine.h"
#include "framework/ui/iuiactionsregister.h"
#include "internal/mixhousegrideditor.h"
#include "internal/mixhouselibrary.h"
#include "internal/mixhousefolderwatcher.h"
#include "internal/mixhousehdwaveforms.h"
#include "internal/mixhouseuiactions.h"
#include "internal/mixhouseactionscontroller.h"
#include "internal/mixhousetrackaligner.h"
#include "internal/mixhousebeatgrids.h"
#include "internal/mixhousetempomap.h"
#include "internal/mixhousetransitions.h"
#include "internal/mixhousekeymatch.h"
#include "internal/mixhouselexicon.h"
#include "internal/mixhouserekordboxxml.h"
#include "internal/mixhouserekordboxphrases.h"
#include "internal/mixhouseclipcolors.h"
#include "internal/mixhousestems.h"
#include "internal/mixhouselibraryconfiguration.h"
#include "internal/mixhouselicence.h"
#include "internal/mixhousetransitionconfiguration.h"
#include "mixhousecoverart.h"
#include "qml/Audacity/MixHouse/mixhousetracklistmodel.h"

using namespace au::mixhouse;

static const std::string mname = "mixhouse";

std::string MixHouseModule::moduleName() const
{
    return mname;
}

void MixHouseModule::registerExports()
{
    // See mixhousemodule.h's own comment on this override for why this
    // export lives at the MODULE level (globalIoc()) rather than alongside
    // IMixHouseLibrary in MixHouseModuleContext::registerExports() below
    // (the context-scoped ioc()).
    m_libraryConfiguration = std::make_shared<MixHouseLibraryConfiguration>();
    globalIoc()->registerExport<IMixHouseLibraryConfiguration>(mname, m_libraryConfiguration);
    m_transitionConfiguration = std::make_shared<MixHouseTransitionConfiguration>();
    globalIoc()->registerExport<IMixHouseTransitionConfiguration>(mname, m_transitionConfiguration);
    m_hdWaveforms = std::make_shared<MixHouseHdWaveforms>();
    globalIoc()->registerExport<IMixHouseHdWaveforms>(mname, m_hdWaveforms);
    m_licence = std::make_shared<MixHouseLicence>();
    globalIoc()->registerExport<IMixHouseLicence>(mname, m_licence);
}

void MixHouseModule::onInit(const muse::IApplication::RunMode& mode)
{
    if (mode != muse::IApplication::RunMode::GuiApp) {
        return;
    }
    // Registration (this module's own and every other module's) always
    // completes before any onInit() runs, so MixHouseLibrary::init()
    // (MixHouseModuleContext::onInit() below) can safely resolve this
    // export by the time it needs to, regardless of module- vs. context-
    // level onInit() ordering between the two.
    m_libraryConfiguration->init();
    m_transitionConfiguration->init();
    m_licence->init();
}

void MixHouseModule::registerResources()
{
}

void MixHouseModule::registerUiTypes()
{
}

muse::modularity::IContextSetup* MixHouseModule::newContext(const muse::modularity::ContextPtr& ctx) const
{
    return new MixHouseModuleContext(ctx);
}

void MixHouseModuleContext::registerExports()
{
    m_library = std::make_shared<MixHouseLibrary>();
    ioc()->registerExport<IMixHouseLibrary>(mname, m_library);

    // Beat-to-bar alignment for library tracks dropped on the timeline --
    // resolved by projectscene's DropController (see imixhousetrackaligner.h).
    ioc()->registerExport<IMixHouseTrackAligner>(mname, std::make_shared<MixHouseTrackAligner>(iocContext()));

    // Beat ticks on clips and the grid tools (nudge, tempo, undo).
    m_beatGrids = std::make_shared<MixHouseBeatGrids>(iocContext());
    ioc()->registerExport<IMixHouseBeatGrids>(mname, m_beatGrids);

    // The master tempo line edited in the TEMPO lane.
    m_tempoMap = std::make_shared<MixHouseTempoMap>(iocContext());
    ioc()->registerExport<IMixHouseTempoMap>(mname, m_tempoMap);

    // BEATMATCH zones and their crossfades.
    m_transitions = std::make_shared<MixHouseTransitions>(iocContext());
    ioc()->registerExport<IMixHouseTransitions>(mname, m_transitions);

    // The Beatgrid editor (its panel lives in the Beatgrid workspace).
    ioc()->registerExport<IMixHouseGridEditor>(mname, std::make_shared<MixHouseGridEditor>(iocContext()));

    // Match Key on the clip menu.
    ioc()->registerExport<IMixHouseKeyMatch>(mname, std::make_shared<MixHouseKeyMatch>(iocContext()));
    // Lexicon DJ's library, read-only (sidebar "Lexicon DJ").
    auto lexicon = std::make_shared<MixHouseLexicon>();
    ioc()->registerExport<IMixHouseLexicon>(mname, lexicon);
    // A rekordbox collection XML export, read-only (sidebar "Rekordbox XML").
    m_rekordboxXml = std::make_shared<MixHouseRekordboxXml>();
    ioc()->registerExport<IMixHouseRekordboxXml>(mname, m_rekordboxXml);
    // rekordbox's phrase analysis (the phrase ribbon on clips).
    m_rekordboxPhrases = std::make_shared<MixHouseRekordboxPhrases>();
    ioc()->registerExport<IMixHouseRekordboxPhrases>(mname, m_rekordboxPhrases);
    m_library->setExternalAnalysis(m_rekordboxPhrases, lexicon);

    // Clip colour intensity and Colour by key, per track (clip menu).
    ioc()->registerExport<IMixHouseClipColors>(mname, std::make_shared<MixHouseClipColors>(iocContext()));
    ioc()->registerExport<IMixHouseStems>(mname, std::make_shared<MixHouseStems>(iocContext()));

    // The File-menu's "Add to Library" submenu (Add File(s)/Add Folder,
    // Import from Rekordbox/iTunes XML) -- same IUiActionsModule +
    // Actionable-dispatcher pattern every other module's File-menu
    // contribution uses (au::project::ProjectUiActions/
    // ProjectActionsController for "project-import"). See
    // MixHouseUiActions/MixHouseActionsController for the real work.
    // Watch folders: new songs in the music directories, added and analysed.
    m_folderWatcher = std::make_shared<MixHouseFolderWatcher>(iocContext());

    m_actionsController = std::make_shared<MixHouseActionsController>(iocContext());
    m_uiActions = std::make_shared<MixHouseUiActions>();
}

void MixHouseModuleContext::onInit(const muse::IApplication::RunMode& mode)
{
    if (mode != muse::IApplication::RunMode::GuiApp) {
        return;
    }
    m_library->init();
    m_beatGrids->init();
    m_tempoMap->init();
    m_transitions->init();
    m_rekordboxXml->init();
    m_rekordboxPhrases->init();
    m_folderWatcher->init();

    m_actionsController->init();
    m_uiActions->init();
    m_uiActions->setPerformance(ioc()->resolve<playback::IPerformance>(mname));
    m_actionsController->automationOverrideChanged = [ui = m_uiActions](bool overridden) {
        ui->setAutomationOverridden(overridden);
    };
    m_actionsController->automationArmChanged = [ui = m_uiActions](bool armed) { ui->setAutomationArmed(armed); };
    auto ar = ioc()->resolve<muse::ui::IUiActionsRegister>(mname);
    if (ar) {
        ar->reg(m_uiActions);
    }

    // Phase 8 cover art: image://mixhousecovers/<trackId>, backed by real
    // TagLib embedded-picture extraction (mixhousecoverart.h). Same
    // IUiEngine::qmlEngine() access pattern already used for KDDockWidgets
    // setup in muse/framework/dockwindow/dockmodule.cpp -- the engine exists
    // by this point in the GuiApp lifecycle and takes ownership of the
    // provider instance.
    if (auto uiEngine = ioc()->resolve<muse::ui::IUiEngine>(mname)) {
        if (QQmlEngine* engine = uiEngine->qmlEngine()) {
            engine->addImageProvider(QLatin1String("mixhousecovers"),
                                      new MixHouseCoverArtImageProvider(m_library));
            engine->addImageProvider(QLatin1String("glidemixwave"), new MixHouseHdWaveImageProvider(
                                          muse::modularity::globalIoc()->resolve<IMixHouseHdWaveforms>(mname)));
        }
    }
}
