/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include "realtimeeffectmenumodelbase.h"

#include "effects/effects_base/ieffectchainpresets.h"
#include "framework/interactive/iinteractive.h"

namespace au::projectscene {
class AddEffectMenuModel : public RealtimeEffectMenuModelBase
{
    Q_OBJECT

    muse::ContextInject<effects::IEffectChainPresets> chainPresets{ this };
    muse::ContextInject<muse::IInteractive> interactive{ this };

public:
    explicit AddEffectMenuModel(QObject* parent = nullptr);

    Q_INVOKABLE void init();

private:
    void handleMenuItem(const QString& itemId) override;
    void doPopulateMenu() override;
    muse::uicomponents::MenuItem* makeChainPresetsMenu();
    void saveChainPreset();

    // effects::IEffectMenuItemFactory
    muse::uicomponents::MenuItem* makeMenuEffectItem(const effects::EffectId& effectId) override;
    muse::uicomponents::MenuItem* makeMenuEffect(const muse::String& title, const muse::uicomponents::MenuItemList& items) override;
};
}
