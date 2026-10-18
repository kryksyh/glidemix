/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include "effects/builtin/ibuiltineffectsviewregister.h"
#include "effects/effects_base/ieffectsconfiguration.h"
#include "framework/global/modularity/ioc.h"

namespace au::effects {
class BuiltinCollectionLoader : public muse::Contextable
{
    muse::GlobalInject<IBuiltinEffectsViewRegister> builtinEffectsViewRegister;
    muse::GlobalInject<IEffectsConfiguration> effectsConfiguration;

public:
    BuiltinCollectionLoader(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    static void preInit();
    void init();
};
}
