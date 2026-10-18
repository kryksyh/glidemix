/*
 * GlideMix
 */
#include "mixxxeffectids.h"

#include "effects/effects_base/internal/effectsutils.h"

#include "mixxxeffects.h"

using namespace au::effects;

const std::map<std::string, EffectId>& MixxxEffectIds::ids() const
{
    static const std::map<std::string, EffectId> map = [] {
        std::map<std::string, EffectId> result;
#define MIXTAPE_MIXXX_EFFECT(ClassName, MixxxId, Name, Group) \
    {                                                        \
        const ClassName effect;                              \
        result[MixxxId] = utils::effectId(&effect);          \
    }
#include "mixxxeffectlist.h"
#undef MIXTAPE_MIXXX_EFFECT
        return result;
    }();
    return map;
}

EffectId MixxxEffectIds::registeredId(const EffectId& builtId) const
{
    // Looked up once: the lanes ask on every redraw.
    static std::map<EffectId, EffectId> resolved;
    if (const auto it = resolved.find(builtId); it != resolved.end()) {
        return it->second;
    }
    if (!effectsProvider() || effectsProvider()->effectMetaList().empty()) {
        return builtId;   // not loaded yet: don't remember a guess
    }
    const EffectId id = findRegisteredId(builtId);
    resolved[builtId] = id;
    return id;
}

EffectId MixxxEffectIds::findRegisteredId(const EffectId& builtId) const
{
    for (const EffectMeta& meta : effectsProvider()->effectMetaList()) {
        if (meta.id == builtId) {
            return builtId;
        }
    }
    const std::string path = utils::parseEffectPath(builtId);
    for (const EffectMeta& meta : effectsProvider()->effectMetaList()) {
        if (utils::parseEffectPath(meta.id) == path) {
            return meta.id;
        }
    }
    return builtId;
}

EffectId MixxxEffectIds::effectIdForMixxxId(const std::string& mixxxId) const
{
    const auto it = ids().find(mixxxId);
    return it != ids().end() ? registeredId(it->second) : EffectId {};
}

std::string MixxxEffectIds::mixxxIdForEffectId(const EffectId& effectId) const
{
    const std::string path = utils::parseEffectPath(effectId);
    for (const auto& [mixxxId, id] : ids()) {
        if (id == effectId || utils::parseEffectPath(id) == path) {
            return mixxxId;
        }
    }
    return {};
}
