/*
 * GlideMix
 */
#include "djeffectspreferencesmodel.h"

#include <algorithm>

#include "effects/effects_base/internal/effectsutils.h"

using namespace au::appshell;
using namespace au::effects;

namespace {
QVariantList toVariantList(const EffectMetaList& metas)
{
    QVariantList list;
    for (const EffectMeta& meta : metas) {
        list.push_back(QVariantMap { { "id", meta.id.toQString() }, { "title", meta.title.toQString() } });
    }
    return list;
}

std::vector<EffectId> ids(const EffectMetaList& metas)
{
    std::vector<EffectId> result;
    for (const EffectMeta& meta : metas) {
        result.push_back(meta.id);
    }
    return result;
}
}

DjEffectsPreferencesModel::DjEffectsPreferencesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void DjEffectsPreferencesModel::load()
{
    reload();
    // Cancel / Restore Defaults change the settings underneath the page.
    effectsConfiguration()->djEffectListsChanged().onNotify(this, [this]() { reload(); });
    effectsProvider()->effectMetaListChanged().onNotify(this, [this]() { reload(); });
    effectsConfiguration()->djKeepMetaknobPositionChanged().onNotify(this, [this]() { emit keepMetaknobPositionChanged(); });
}

bool DjEffectsPreferencesModel::keepMetaknobPosition() const
{
    return effectsConfiguration()->djKeepMetaknobPosition();
}

void DjEffectsPreferencesModel::setKeepMetaknobPosition(bool keep)
{
    effectsConfiguration()->setDjKeepMetaknobPosition(keep);
}

void DjEffectsPreferencesModel::reload()
{
    const EffectMetaList all = effectsProvider()->effectMetaList();
    auto lists = utils::djEffectLists(all, effectsConfiguration()->djVisibleEffects(),
                                      effectsConfiguration()->djHiddenEffects());
    // The same names as the Add effect menu, or two plain "Reverb"s.
    utils::prefixClashingDjTitles(lists.visible, all);
    utils::prefixClashingDjTitles(lists.hidden, all);
    m_visible = lists.visible;
    m_hidden = lists.hidden;
    emit listsChanged();
}

void DjEffectsPreferencesModel::save()
{
    effectsConfiguration()->setDjEffectLists(ids(m_visible), ids(m_hidden));
    emit listsChanged();
}

QVariantList DjEffectsPreferencesModel::visibleEffects() const
{
    return toVariantList(m_visible);
}

QVariantList DjEffectsPreferencesModel::hiddenEffects() const
{
    return toVariantList(m_hidden);
}

void DjEffectsPreferencesModel::moveEffect(const QString& effectId, bool toHidden, int toIndex)
{
    const EffectId id = muse::String::fromQString(effectId);
    auto byId = [&id](const EffectMeta& meta) { return meta.id == id; };
    EffectMeta moved;
    bool found = false;
    for (EffectMetaList* list : { &m_visible, &m_hidden }) {
        const auto it = std::find_if(list->begin(), list->end(), byId);
        if (it != list->end()) {
            // Moving down within one list: the index was counted with the
            // effect still in place.
            if (list == (toHidden ? &m_hidden : &m_visible) && toIndex > int(it - list->begin())) {
                --toIndex;
            }
            moved = *it;
            list->erase(it);
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }
    EffectMetaList& target = toHidden ? m_hidden : m_visible;
    const int index = toIndex < 0 ? int(target.size()) : std::clamp(toIndex, 0, int(target.size()));
    target.insert(target.begin() + index, moved);
    save();
}

QVariantMap DjEffectsPreferencesModel::effectInfo(const QString& effectId) const
{
    const EffectId id = muse::String::fromQString(effectId);
    for (const EffectMetaList* list : { &m_visible, &m_hidden }) {
        for (const EffectMeta& meta : *list) {
            if (meta.id != id) {
                continue;
            }
            return QVariantMap {
                { "name", meta.title.toQString() },
                { "version", meta.version.toQString() },
                { "type", QStringLiteral("Built-in") },
                { "author", meta.vendor.toQString() },
                { "description", meta.description.toQString() },
            };
        }
    }
    return {};
}
