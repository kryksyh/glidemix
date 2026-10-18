/*
 * GlideMix
 */
#pragma once

#include <map>

#include "modularity/ioc.h"
#include "effects/effects_base/ieffectchainpresets.h"
#include "effects/effects_base/ieffectsprovider.h"

namespace au::effects {
class MixxxEffectIds : public IMixxxEffectIds
{
    muse::GlobalInject<IEffectsProvider> effectsProvider;

public:
    EffectId effectIdForMixxxId(const std::string& mixxxId) const override;
    std::string mixxxIdForEffectId(const EffectId& effectId) const override;

private:
    const std::map<std::string, EffectId>& ids() const;
    //! The id the effect is registered under. The vendor is part of an effect
    //! id and changed with the renames (Mixxx, Mixtape Studio, GlideMix), but the
    //! registry keeps the id it first saw; the path ("Built-in Effect: Mixxx
    //! Filter") never changed, so the effect is found by it.
    EffectId registeredId(const EffectId& builtId) const;
    EffectId findRegisteredId(const EffectId& builtId) const;
};
}
