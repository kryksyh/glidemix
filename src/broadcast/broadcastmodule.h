/*
 * GlideMix
 */
#pragma once

#include <memory>

#include "modularity/imodulesetup.h"

namespace au::broadcast {
class Broadcaster;
class BroadcastConfiguration;

class BroadcastModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override;
    void registerExports() override;
    void onInit(const muse::IApplication::RunMode& mode) override;
    void onDeinit() override;

private:
    std::shared_ptr<Broadcaster> m_broadcaster;
    std::shared_ptr<BroadcastConfiguration> m_configuration;
};
}
