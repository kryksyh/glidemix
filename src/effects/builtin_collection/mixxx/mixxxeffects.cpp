/*
 * GlideMix
 */
#include "mixxxeffects.h"

using namespace au::effects;

// Internal names match MixxxEffectBase::GetSymbol(): "Mixxx " + the Mixxx name.
#define MIXTAPE_MIXXX_EFFECT(ClassName, MixxxId, Name, Group) \
    const ComponentInterfaceSymbol ClassName::Symbol { wxT("Mixxx " Name), TranslatableString::untranslatable(Name) };
#include "mixxxeffectlist.h"
#undef MIXTAPE_MIXXX_EFFECT

std::vector<const ComponentInterfaceSymbol*> au::effects::mixxxEffectSymbols()
{
    return {
#define MIXTAPE_MIXXX_EFFECT(ClassName, MixxxId, Name, Group) &ClassName::Symbol,
#include "mixxxeffectlist.h"
#undef MIXTAPE_MIXXX_EFFECT
    };
}
