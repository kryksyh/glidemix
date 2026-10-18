/*
 * GlideMix
 *
 * The Mixxx effects GlideMix registers, one class each.
 */
#pragma once

#include "mixxxeffect.h"

namespace au::effects {
#define MIXTAPE_MIXXX_EFFECT(ClassName, MixxxId, Name, Group)            \
    class ClassName final : public MixxxEffectBase                      \
    {                                                                   \
    public:                                                             \
        static const ComponentInterfaceSymbol Symbol;                   \
        ClassName() : MixxxEffectBase(MixxxId, EffectGroup::Group) {}   \
    };
#include "mixxxeffectlist.h"
#undef MIXTAPE_MIXXX_EFFECT

//! Symbols of every registered Mixxx effect (they share one view).
std::vector<const ComponentInterfaceSymbol*> mixxxEffectSymbols();
}
