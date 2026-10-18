/*
 * Audacity: A Digital Audio Editor
 */
#include "effectsmenuprovider.h"
#include "effectsutils.h"

#include "global/log.h"

namespace au::effects {
void EffectsMenuProvider::init()
{
    configuration()->effectMenuOrganizationChanged().onNotify(this, [this] {
        m_effectMenusChanged.notify();
    });

    effectsProvider()->effectMetaListChanged().onNotify(this, [this] { m_effectMenusChanged.notify(); });
    configuration()->djEffectListsChanged().onNotify(this, [this] { m_effectMenusChanged.notify(); });
}

muse::uicomponents::MenuItemList EffectsMenuProvider::destructiveEffectMenu(IEffectMenuItemFactory& effectMenu, EffectFilter filterType)
{
    std::function<bool(const EffectMeta&)> filter;
    switch (filterType) {
    case EffectFilter::GeneratorsOnly:
        filter = [](const EffectMeta& meta) { return meta.type != EffectType::Generator; };
        break;
    case EffectFilter::RealtimeProcessorsOnly:
        LOGE() << "Unexpected filter type RealtimeProcessorsOnly for destructiveEffectMenu";
    case EffectFilter::ProcessorsOnly:
        filter = [](const EffectMeta& meta) { return meta.type != EffectType::Processor; };
        break;
    case EffectFilter::ToolsOnly:
        filter = [](const EffectMeta& meta) { return meta.type != EffectType::Tool; };
        break;
    case EffectFilter::AnalyzersOnly:
        filter = [](const EffectMeta& meta) { return meta.type != EffectType::Analyzer; };
        break;
    default:
        LOGE() << "EffectsMenuProvider::destructiveEffectMenu: Unknown filter type:" << static_cast<int>(filterType);
        break;
    }

    IF_ASSERT_FAILED(filter) {
        return {};
    }

    return au::effects::utils::destructiveEffectMenu(configuration()->effectMenuOrganization(),
                                                     effectsProvider()->effectMetaList(), std::move(filter), effectMenu);
}

muse::uicomponents::MenuItemList EffectsMenuProvider::realtimeEffectMenu(IEffectMenuItemFactory& effectMenu)
{
    auto filter = [](const EffectMeta& meta) { return !(meta.type == EffectType::Processor && meta.isRealtimeCapable); };

    // GlideMix: DJ Effects in the order set in Preferences > Effects,
    // hidden ones left out; everything else keeps its place after them.
    const EffectMetaList all = effectsProvider()->effectMetaList();
    const auto dj = utils::djEffectLists(all, configuration()->djVisibleEffects(), configuration()->djHiddenEffects());
    EffectMetaList effects = dj.visible;
    for (const EffectMeta& meta : all) {
        if (!utils::isDjEffect(meta)) {
            effects.push_back(meta);
        }
    }
    return au::effects::utils::realtimeEffectMenu(configuration()->effectMenuOrganization(),
                                                  std::move(effects), std::move(filter), effectMenu);
}

muse::async::Notification EffectsMenuProvider::effectMenusChanged() const
{
    return m_effectMenusChanged;
}
}
