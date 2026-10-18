/*
 * GlideMix
 */
#include "performtoolbaritem.h"

using namespace au::projectscene;
using namespace muse::ui;
using namespace muse::uicomponents;

namespace {
const QString HEADPHONES_PREFIX = "headphones:";
const char* ACTION_CODES[] = { "mixhouse-perform-from-playhead", "mixhouse-reset-performance" };
}

PerformToolBarItem::PerformToolBarItem(const UiAction& action, ToolBarItemType::Type type, QObject* parent)
    : ToolBarItem(action, type, parent)
{
    setShowTitle(true);
    setIsMenuSecondary(true);
    rebuildMenu();
    if (performance()) {
        // Also refreshes the device list, e.g. headphones plugged in since.
        performance()->performanceStateChanged().onNotify(this, [this]() {
            rebuildMenu();
        });
    }
}

void PerformToolBarItem::handleMenuItem(const QString& itemId)
{
    if (itemId.startsWith(HEADPHONES_PREFIX)) {
        if (performance()) {
            performance()->setHeadphonesDevice(itemId.mid(HEADPHONES_PREFIX.size()).toStdString());
        }
        return;
    }
    ToolBarItem::handleMenuItem(itemId);
}

void PerformToolBarItem::rebuildMenu()
{
    MenuItemList items;
    for (const char* code : ACTION_CODES) {
        const UiAction& itemAction = uiActionsRegister()->action(code);
        if (itemAction.isValid()) {
            MenuItem* menuItem = new MenuItem(itemAction, this);
            menuItem->setState(uiActionsRegister()->actionState(code));
            items << menuItem;
        }
    }

    if (performance() && performance()->performanceAvailable()) {
        const std::string current = performance()->headphonesDevice();
        auto makeChoice = [this, &current](const std::string& device, const muse::TranslatableString& title) {
            MenuItem* item = new MenuItem(this);
            item->setId(HEADPHONES_PREFIX + QString::fromStdString(device));
            UiAction choice;
            choice.checkable = Checkable::Yes;
            item->setAction(choice);
            UiActionState state;
            state.enabled = true;
            state.checked = device == current;
            item->setState(state);
            item->setTitle(title);
            return item;
        };
        // Flat, not a submenu: the toolbar's secondary menu shows MenuItem
        // objects directly and doesn't open nested ones.
        items << makeChoice({}, muse::TranslatableString("projectscene", "Headphones: outputs 3-4"));
        for (const std::string& device : performance()->outputDevices()) {
            items << makeChoice(device, muse::TranslatableString::untranslatable(
                                    muse::String::fromStdString("Headphones: " + device)));
        }
    }

    setMenuItems(items);
}
