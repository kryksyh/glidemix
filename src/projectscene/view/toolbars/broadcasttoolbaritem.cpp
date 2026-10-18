/*
 * GlideMix
 */
#include "broadcasttoolbaritem.h"

using namespace au::projectscene;
using namespace au::broadcast;
using namespace muse::ui;
using namespace muse::uicomponents;

namespace {
const QString SOURCE_PERFORM = "broadcast-source-perform";
const QString SOURCE_CUE = "broadcast-source-cue";
const QString SETTINGS = "broadcast-settings";
}

BroadcastToolBarItem::BroadcastToolBarItem(const UiAction& action, ToolBarItemType::Type type, QObject* parent)
    : ToolBarItem(action, type, parent)
{
    setIsMenuSecondary(true);
    rebuild();

    if (broadcaster()) {
        broadcaster()->stateChanged().onNotify(this, [this]() {
            rebuild();
            emit broadcastChanged();
        });
    }
}

int BroadcastToolBarItem::broadcastState() const
{
    if (!broadcaster()) {
        return 0;
    }
    switch (broadcaster()->state()) {
    case IBroadcaster::State::Off: return 0;
    case IBroadcaster::State::Connecting: return 1;
    case IBroadcaster::State::Live: return 2;
    case IBroadcaster::State::Failed: return 3;
    }
    return 0;
}

QString BroadcastToolBarItem::sourceName() const
{
    if (!broadcaster()) {
        return {};
    }
    return broadcaster()->source() == IBroadcaster::Source::Cue
           ? muse::qtrc("projectscene", "CUE") : muse::qtrc("projectscene", "PERFORM");
}

void BroadcastToolBarItem::handleMenuItem(const QString& itemId)
{
    if (!broadcaster()) {
        return;
    }
    if (itemId == SOURCE_PERFORM) {
        broadcaster()->setSource(IBroadcaster::Source::Perform);
    } else if (itemId == SOURCE_CUE) {
        broadcaster()->setSource(IBroadcaster::Source::Cue);
    } else {
        ToolBarItem::handleMenuItem(itemId);
        return;
    }
    rebuild();
    emit broadcastChanged();
}

void BroadcastToolBarItem::rebuild()
{
    const bool cue = broadcaster() && broadcaster()->source() == IBroadcaster::Source::Cue;

    auto choice = [this](const QString& id, const muse::TranslatableString& title, bool checked) {
        MenuItem* item = new MenuItem(this);
        item->setId(id);
        UiAction action;
        action.checkable = Checkable::Yes;
        item->setAction(action);
        UiActionState state;
        state.enabled = true;
        state.checked = checked;
        item->setState(state);
        item->setTitle(title);
        return item;
    };

    MenuItemList items;
    items << choice(SOURCE_PERFORM, muse::TranslatableString("projectscene", "Broadcast the Perform output"), !cue);
    items << choice(SOURCE_CUE, muse::TranslatableString("projectscene", "Broadcast the headphone cue"), cue);

    setMenuItems(items);

    // The tooltip carries the server, or why it stopped.
    if (broadcaster()) {
        const std::string status = broadcaster()->statusText();
        UiAction action = this->action();
        action.description = status.empty()
                             ? muse::TranslatableString("action_description", "Stream what is playing to your radio server")
                             : muse::TranslatableString::untranslatable(muse::String::fromStdString(status));
        setAction(action);
    }
}
