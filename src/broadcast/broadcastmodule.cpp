/*
 * GlideMix
 */
#include "broadcastmodule.h"

#include "modularity/ioc.h"

#include "internal/broadcaster.h"
#include "internal/broadcastconfiguration.h"

using namespace au::broadcast;

std::string BroadcastModule::moduleName() const
{
    return "broadcast";
}

void BroadcastModule::registerExports()
{
    m_configuration = std::make_shared<BroadcastConfiguration>();
    m_broadcaster = std::make_shared<Broadcaster>();

    // Global, not context-scoped: the toolbar button, the preferences page and
    // the engine tap all reach the one broadcaster.
    globalIoc()->registerExport<IBroadcastConfiguration>(moduleName(), m_configuration);
    globalIoc()->registerExport<IBroadcaster>(moduleName(), m_broadcaster);
}

void BroadcastModule::onInit(const muse::IApplication::RunMode& mode)
{
    if (mode != muse::IApplication::RunMode::GuiApp) {
        return;
    }
    m_configuration->init();
    m_broadcaster->init();
}

void BroadcastModule::onDeinit()
{
    if (m_broadcaster) {
        m_broadcaster->stop();
    }
}
