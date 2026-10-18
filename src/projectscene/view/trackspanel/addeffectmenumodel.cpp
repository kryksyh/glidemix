/*
 * Audacity: A Digital Audio Editor
 */
#include "addeffectmenumodel.h"

#include <algorithm>
#include "effects/effects_base/effectstypes.h"
#include "log.h"
#include "global/translation.h"

using namespace muse;
using namespace au::projectscene;
using namespace muse::uicomponents;

namespace {
// GlideMix: Add effect > Chain presets.
const QString CHAIN_PRESET_LOAD_PREFIX = QStringLiteral("mixtape-chain-preset-load:");
const QString CHAIN_PRESET_SAVE = QStringLiteral("mixtape-chain-preset-save");
}

AddEffectMenuModel::AddEffectMenuModel(QObject* parent)
    : RealtimeEffectMenuModelBase(parent) {}

void AddEffectMenuModel::init()
{
    RealtimeEffectMenuModelBase::init();
    if (chainPresets()) {
        chainPresets()->presetsChanged().onNotify(this, [this]() { invalidateMenu(); });
    }
}

void AddEffectMenuModel::doPopulateMenu()
{
    MenuItemList items = effectMenus();
    if (chainPresets() && !isMasterTrack()) {
        items << makeSeparator();
        items << makeChainPresetsMenu();
    }
    setItems(items);
}

MenuItem* AddEffectMenuModel::makeChainPresetsMenu()
{
    auto makeItem = [this](const QString& id, const QString& title) {
        auto* item = new MenuItem(this);
        item->setId(id);
        QString escaped = title;
        escaped.replace(u'&', QStringLiteral("&&"));   // menu titles are mnemonic strings
        item->setTitle(muse::TranslatableString::untranslatable(escaped));
        item->setEnabled(true);
        return item;
    };
    MenuItemList presets;
    presets << makeItem(CHAIN_PRESET_SAVE, muse::qtrc("projectscene", "Save effects as chain preset…"));
    const auto names = chainPresets()->presetNames();
    if (!names.empty()) {
        presets << makeSeparator();
    }
    for (const std::string& name : names) {
        const QString title = QString::fromStdString(name);
        MenuItem* item = makeItem(CHAIN_PRESET_LOAD_PREFIX + title, title);
        // A preset none of whose effects are installed here can't load.
        const auto effects = chainPresets()->presetEffects(name);
        item->setEnabled(std::any_of(effects.begin(), effects.end(), [](const auto& e) { return e.available; }));
        presets << item;
    }
    return makeMenu(muse::TranslatableString("projectscene", "Chain presets"), presets);
}

void AddEffectMenuModel::saveChainPreset()
{
    const auto tId = trackId();
    if (!tId) {
        return;
    }
    muse::UriQuery query("audacity://effects/presets/input_name");
    query.addParam("title", muse::Val(muse::qtrc("projectscene", "Save chain preset").toStdString()));
    const auto rv = interactive()->openSync(query);
    const std::string name = rv.val.toString();
    if (rv.ret.code() == static_cast<int>(muse::Ret::Code::Cancel) || name.empty()) {
        return;
    }
    if (chainPresets()->hasPreset(name)) {
        const auto answer = interactive()->questionSync(
            muse::trc("projectscene", "Save chain preset"),
            muse::mtrc("projectscene", "Chain preset “%1” already exists, replace?").arg(muse::String::fromStdString(name)).toStdString(),
            { muse::IInteractive::Button::No, muse::IInteractive::Button::Yes });
        if (answer.button() != static_cast<int>(muse::IInteractive::Button::Yes)) {
            return;
        }
    }
    if (!chainPresets()->saveTrackAsPreset(*tId, name)) {
        interactive()->error(muse::trc("projectscene", "Save chain preset"),
                             muse::trc("projectscene", "The chain preset could not be saved."));
    }
}

void AddEffectMenuModel::handleMenuItem(const QString& itemId)
{
    if (itemId == CHAIN_PRESET_SAVE) {
        saveChainPreset();
        return;
    }
    const auto tId = trackId();
    IF_ASSERT_FAILED(tId.has_value()) {
        return;
    }
    if (itemId.startsWith(CHAIN_PRESET_LOAD_PREFIX)) {
        chainPresets()->loadPresetOntoTrack(itemId.mid(CHAIN_PRESET_LOAD_PREFIX.size()).toStdString(), *tId);
        return;
    }

    const MenuItem& menuItem = findItem(itemId);

    const auto effectId = effects::effectIdFromAction(menuItem.id());
    if (const auto state = realtimeEffectService()->addRealtimeEffect(*tId, effectId)) {
        effectViewController()->showEffect(state);
    }
}

muse::uicomponents::MenuItem* AddEffectMenuModel::makeMenuEffectItem(const effects::EffectId& effectId)
{
    return makeMenuItem(effects::makeEffectAction(effects::REALTIME_EFFECT_ADD_ACTION, effectId));
}

muse::uicomponents::MenuItem* AddEffectMenuModel::makeMenuEffect(const muse::String& title,
                                                                 const muse::uicomponents::MenuItemList& items)
{
    return makeMenu(muse::TranslatableString::untranslatable(title), items);
}
