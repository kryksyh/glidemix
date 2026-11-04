/*
 * Audacity: A Digital Audio Editor
 */
#include "effectsproviderinitializer.h"

namespace au::effects {
void EffectsProviderInitializer::callAfterSplashScreen()
{
    static bool effectsProviderInitialized = false;
    if (effectsProviderInitialized) {
        return;
    }
    effectsProvider()->initOnce(iocContext(), *interactive(), *registerAudioPluginsScenario());
    effectsProviderInitialized = true;
}

void EffectsProviderInitializer::validatePendingPlugins()
{
    effectsProvider()->validatePendingPlugins(iocContext(), *interactive(), *registerAudioPluginsScenario());
}
} // namespace au::effects
