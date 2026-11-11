/*
* Audacity: A Digital Audio Editor
*/
#include "playbacktoolbarcustomisemodel.h"

#include <QList>
#include <algorithm>

#include "internal/projectsceneuiactions.h"

#include "playbacktoolbarcustomiseitem.h"

#include "translation.h"
#include "log.h"

using namespace au::projectscene;
using namespace muse;
using namespace muse::ui;
using namespace muse::uicomponents;
using namespace muse::actions;

static const QString TOOLBAR_NAME("playbackToolBar");

static const ActionQuery PLAYBACK_TOGGLE_PLAY_PAUSE_QUERY("action://playback/toggle-play-pause");
static const ActionQuery RECORD_START_QUERY("action://record/start");

PlaybackToolBarCustomiseModel::PlaybackToolBarCustomiseModel(QObject* parent)
    : SelectableItemListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void PlaybackToolBarCustomiseModel::load()
{
    TRACEFUNC;

    QList<Item*> items;

    ToolConfig toolConfig = uiState()->toolConfig(TOOLBAR_NAME,
                                                  projectscene::ProjectSceneUiActions::defaultPlaybackToolBarConfig());

    bool lastWasSeparator = false;
    for (const auto& configItem : toolConfig.items) {
        if (configItem.isSeparator()) {
            if (!lastWasSeparator) {
                items << makeSeparatorItem();
            }
            lastWasSeparator = true;
            continue;
        }
        lastWasSeparator = false;

        // GlideMix's own items (Flexible Space, the Icon/Text marker) are no
        // actions; looking one up as an action can throw on its text.
        if (configItem.intent.rfind("glidemix-", 0) == 0) {
            continue;
        }

        UiAction action = actionsRegister()->action(configItem.intent);
        if (action.code.empty()) {
            continue;
        }

        items << makeItem(action, configItem.show);
    }

    setItems(items);
}

QVariant PlaybackToolBarCustomiseModel::data(const QModelIndex& index, int role) const
{
    PlaybackToolBarCustomiseItem* item = modelIndexToItem(index);
    if (!item) {
        return QVariant();
    }

    switch (role) {
    case ItemRole: return QVariant::fromValue(item);
    default: break;
    }

    return SelectableItemListModel::data(index, role);
}

QHash<int, QByteArray> PlaybackToolBarCustomiseModel::roleNames() const
{
    QHash<int, QByteArray> roles = SelectableItemListModel::roleNames();
    roles[ItemRole] = "item";

    return roles;
}

void PlaybackToolBarCustomiseModel::addSeparatorLine()
{
    if (!hasSelection()) {
        return;
    }

    QModelIndex selectedItemIndex = selection()->selectedIndexes().first();
    if (!selectedItemIndex.isValid()) {
        return;
    }

    QModelIndex prevItemIndex = index(selectedItemIndex.row() - 1);
    if (!prevItemIndex.isValid()) {
        return;
    }

    insertItem(prevItemIndex.row() + 1, makeSeparatorItem());

    onUpdateOperationsAvailability();
    saveActions();
}

QItemSelectionModel* PlaybackToolBarCustomiseModel::selectionModel() const
{
    return selection();
}

bool PlaybackToolBarCustomiseModel::isAddSeparatorAvailable() const
{
    return m_isAddSeparatorAvailable;
}

void PlaybackToolBarCustomiseModel::setIsAddSeparatorAvailable(bool isAddSeparatorAvailable)
{
    if (m_isAddSeparatorAvailable == isAddSeparatorAvailable) {
        return;
    }

    m_isAddSeparatorAvailable = isAddSeparatorAvailable;
    emit isAddSeparatorAvailableChanged(m_isAddSeparatorAvailable);
}

PlaybackToolBarCustomiseItem* PlaybackToolBarCustomiseModel::modelIndexToItem(const QModelIndex& index) const
{
    return dynamic_cast<PlaybackToolBarCustomiseItem*>(item(index));
}

void PlaybackToolBarCustomiseModel::onUpdateOperationsAvailability()
{
    TRACEFUNC;

    SelectableItemListModel::onUpdateOperationsAvailability();
    updateRemovingAvailability();
    updateAddSeparatorAvailability();
}

void PlaybackToolBarCustomiseModel::onRowsMoved()
{
    saveActions();
}

void PlaybackToolBarCustomiseModel::onRowsRemoved()
{
    saveActions();
}

void PlaybackToolBarCustomiseModel::updateRemovingAvailability()
{
    TRACEFUNC;

    auto hasActionInSelection = [this](const QModelIndexList& selectedIndexes) {
        for (const QModelIndex& index : selectedIndexes) {
            const PlaybackToolBarCustomiseItem* item = modelIndexToItem(index);

            if (item && item->type() == PlaybackToolBarCustomiseItem::ACTION) {
                return true;
            }
        }

        return false;
    };

    QModelIndexList selectedIndexes = selection()->selectedIndexes();
    bool removingAvailable = !selectedIndexes.empty();

    if (removingAvailable) {
        removingAvailable = !hasActionInSelection(selectedIndexes);
    }

    setIsRemovingAvailable(removingAvailable);
}

void PlaybackToolBarCustomiseModel::updateAddSeparatorAvailability()
{
    TRACEFUNC;

    bool addingAvailable = !selection()->selectedIndexes().empty();
    if (!addingAvailable) {
        setIsAddSeparatorAvailable(addingAvailable);
        return;
    }

    addingAvailable = selection()->selectedIndexes().count() == 1;
    if (!addingAvailable) {
        setIsAddSeparatorAvailable(addingAvailable);
        return;
    }

    QModelIndex selectedItemIndex = selection()->selectedIndexes().first();

    const PlaybackToolBarCustomiseItem* selectedItem = modelIndexToItem(selectedItemIndex);
    addingAvailable = selectedItem && selectedItem->type() == PlaybackToolBarCustomiseItem::ACTION;

    if (!addingAvailable) {
        setIsAddSeparatorAvailable(addingAvailable);
        return;
    }

    QModelIndex prevItemIndex = index(selectedItemIndex.row() - 1);
    addingAvailable = prevItemIndex.isValid();
    if (addingAvailable) {
        PlaybackToolBarCustomiseItem* prevItem = modelIndexToItem(prevItemIndex);
        addingAvailable = prevItem && prevItem->type() == PlaybackToolBarCustomiseItem::ACTION;
    }

    setIsAddSeparatorAvailable(addingAvailable);
}

PlaybackToolBarCustomiseItem* PlaybackToolBarCustomiseModel::makeItem(const UiAction& action, bool checked)
{
    PlaybackToolBarCustomiseItem* item = new PlaybackToolBarCustomiseItem(PlaybackToolBarCustomiseItem::ItemType::ACTION, this);
    item->setId(QString::fromStdString(action.code));
    item->setTitle(action.title.qTranslatedWithoutMnemonic());
    item->setIcon(action.iconCode);
    item->setIconColor(iconColor(action));
    item->setChecked(checked);

    connect(item, &PlaybackToolBarCustomiseItem::checkedChanged, this, [this](bool) {
        saveActions();
    });

    return item;
}

PlaybackToolBarCustomiseItem* PlaybackToolBarCustomiseModel::makeSeparatorItem()
{
    PlaybackToolBarCustomiseItem* item = new PlaybackToolBarCustomiseItem(PlaybackToolBarCustomiseItem::ItemType::SEPARATOR, this);
    item->setTitle(QString("-------  %1  -------").arg(muse::qtrc("projectscene", "Separator line")));
    item->setChecked(true); //! NOTE Can't be unchecked
    return item;
}

QColor PlaybackToolBarCustomiseModel::iconColor(const muse::ui::UiAction& action) const
{
    QColor color = QColor(uiConfiguration()->currentTheme().values.value(muse::ui::FONT_PRIMARY_COLOR).toString());
    if (action.code == PLAYBACK_TOGGLE_PLAY_PAUSE_QUERY.toString()) {
        color = uiConfiguration()->currentTheme().values.value(muse::ui::PLAY_COLOR).value<QColor>();
    } else if (action.code == RECORD_START_QUERY.toString()) {
        color = uiConfiguration()->currentTheme().values.value(muse::ui::RECORD_COLOR).value<QColor>();
    }

    return color;
}

void PlaybackToolBarCustomiseModel::saveActions()
{
    TRACEFUNC;

    ToolConfig config;
    for (const Item* item : items()) {
        auto customiseItem = dynamic_cast<const PlaybackToolBarCustomiseItem*>(item);
        if (!customiseItem) {
            continue;
        }

        ToolConfig::Item citem;
        citem.intent = customiseItem->id().toStdString();
        citem.show = customiseItem->checked();
        config.items.append(citem);
    }

    uiState()->setToolConfig(TOOLBAR_NAME, config);
}

QVariantList PlaybackToolBarCustomiseModel::entriesOf(const ToolConfig& config) const
{
    QVariantList out;
    for (const auto& item : config.items) {
        if (item.isSeparator()) {
            out << QVariantMap{ { "id", QString() }, { "title", muse::qtrc("projectscene", "Space") }, { "icon", 0 },
                                { "shown", true }, { "separator", true } };
            continue;
        }
        if (item.intent == "glidemix-flexible-space") {
            out << QVariantMap{ { "id", QStringLiteral("glidemix-flexible-space") },
                                { "title", muse::qtrc("projectscene", "Flexible Space") }, { "icon", 0 },
                                { "shown", true }, { "separator", false }, { "flex", true } };
            continue;
        }
        if (item.intent.rfind("glidemix-", 0) == 0) {
            continue;   // the Icon/Text choice or another non-action
        }
        const UiAction action = actionsRegister()->action(item.intent);
        if (action.code.empty()) {
            continue;
        }
        out << QVariantMap{ { "id", QString::fromStdString(action.code) },
                            { "title", action.title.qTranslatedWithoutMnemonic() },
                            // 0: no icon (the item shows its name, as a pill).
                            { "icon", action.iconCode == IconCode::Code::NONE ? 0 : static_cast<int>(action.iconCode) },
                            { "shown", item.show }, { "separator", false } };
    }
    return out;
}

QVariantList PlaybackToolBarCustomiseModel::entries() const
{
    const ToolConfig config = uiState()->toolConfig(TOOLBAR_NAME, ProjectSceneUiActions::defaultPlaybackToolBarConfig());
    QVariantList list = entriesOf(config);

    // Not arranged by hand yet: the toolbar puts Snap after KEYLOCK and the
    // performance Stop after PERFORM (PlaybackToolBarModel::updateActions).
    // Start the sheet from that same order, so the first change moves nothing.
    bool customised = false;
    for (const auto& item : config.items) {
        if (item.intent.rfind("glidemix-toolbar-style-", 0) == 0) {
            customised = true;
        }
    }
    if (!customised) {
        auto indexOf = [&list](const QString& id) {
            for (int i = 0; i < list.size(); ++i) {
                if (list[i].toMap().value("id").toString() == id) {
                    return i;
                }
            }
            return -1;
        };
        auto moveAfter = [&](const QString& id, const QString& anchor) {
            const int from = indexOf(id);
            if (from < 0 || indexOf(anchor) < 0) {
                return;
            }
            const QVariant moved = list.takeAt(from);
            list.insert(indexOf(anchor) + 1, moved);
        };
        moveAfter(QStringLiteral("snap"), QStringLiteral("mixhouse-link"));   // after SYNC / KEYLOCK / LINK
        moveAfter(QStringLiteral("mixhouse-reset-performance"), QStringLiteral("mixhouse-perform"));
    }
    return list;
}

QVariantList PlaybackToolBarCustomiseModel::defaultEntries() const
{
    return entriesOf(ProjectSceneUiActions::defaultPlaybackToolBarConfig());
}

void PlaybackToolBarCustomiseModel::applyEntries(const QVariantList& entries)
{
    ToolConfig config;
    for (const QVariant& v : entries) {
        const QVariantMap m = v.toMap();
        const bool always = m.value("separator").toBool() || m.value("flex").toBool();
        ToolConfig::Item item;
        item.intent = m.value("separator").toBool() ? std::string() : m.value("id").toString().toStdString();
        item.show = always || m.value("shown").toBool();
        if (m.value("flex").toBool() && !m.value("shown").toBool()) {
            continue;   // a Flexible Space taken off is gone
        }
        config.items.append(item);
    }
    // The marker also says "arranged by hand": the toolbar moves nothing for it.
    config.items.append(ToolConfig::Item("glidemix-toolbar-style-" + std::to_string(displayStyle()), false));
    // GlideMix: the one-time icon/category update is done (PlaybackToolBarModel).
    config.items.append(ToolConfig::Item("glidemix-toolbar-groups-1", false));
    uiState()->setToolConfig(TOOLBAR_NAME, config);
    load();
}

void PlaybackToolBarCustomiseModel::restoreDefault()
{
    uiState()->setToolConfig(TOOLBAR_NAME, ProjectSceneUiActions::defaultPlaybackToolBarConfig());
    load();
}

int PlaybackToolBarCustomiseModel::displayStyle() const
{
    const ToolConfig config = uiState()->toolConfig(TOOLBAR_NAME, ProjectSceneUiActions::defaultPlaybackToolBarConfig());
    for (const auto& item : config.items) {
        const QString intent = QString::fromStdString(item.intent);
        if (intent.startsWith(QStringLiteral("glidemix-toolbar-style-"))) {
            return std::clamp(intent.mid(23).toInt(), 0, 2);
        }
    }
    return 1;
}

void PlaybackToolBarCustomiseModel::setDisplayStyle(int style)
{
    ToolConfig config = uiState()->toolConfig(TOOLBAR_NAME, ProjectSceneUiActions::defaultPlaybackToolBarConfig());
    for (int i = config.items.size() - 1; i >= 0; --i) {
        if (QString::fromStdString(config.items[i].intent).startsWith(QStringLiteral("glidemix-toolbar-style-"))) {
            config.items.removeAt(i);
        }
    }
    config.items.append(ToolConfig::Item("glidemix-toolbar-style-" + std::to_string(std::clamp(style, 0, 2)), false));
    bool marked = false;
    for (const auto& item : config.items) {
        marked = marked || item.intent == "glidemix-toolbar-groups-1";
    }
    if (!marked) {
        config.items.append(ToolConfig::Item("glidemix-toolbar-groups-1", false));   // the user chose: keep it
    }
    uiState()->setToolConfig(TOOLBAR_NAME, config);
    load();
}
