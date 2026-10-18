/**********************************************************************

  Audacity: A Digital Audio Editor

**********************************************************************/

#include "timelinecontextmenumodel.h"

#include "framework/global/translation.h"

using namespace au::projectscene;
using namespace muse;
using namespace muse::uicomponents;
using namespace muse::actions;

void TimelineContextMenuModel::load()
{
    AbstractMenuModel::load();

    MenuItemList items = makeRulerItems();
    setItems(items);
}

MenuItemList TimelineContextMenuModel::makeRulerItems()
{
    // Handled by Timeline.qml, which knows where the ruler was clicked.
    auto makeBarOneItem = [this](const char* id, const QString& title, bool enabled) {
        auto* item = new MenuItem(this);
        item->setId(QString::fromUtf8(id));
        item->setTitle(TranslatableString::untranslatable(title));
        item->setEnabled(enabled);
        return item;
    };

    MenuItemList items {
        makeBarOneItem("mixtape-set-bar-one", muse::qtrc("timeline", "Set bar 1 here"), true),
        makeBarOneItem("mixtape-reset-bar-one", muse::qtrc("timeline", "Reset bar 1 to start"), m_barOneSet),
        makeSeparator(),
        makeMenuItem("minutes-seconds-ruler"),
        makeMenuItem("beats-measures-ruler"),
        makeSeparator(),
        makeMenuItem("toggle-update-display-while-playing"),
        makeMenuItem("toggle-pinned-play-head"),
        makeMenuItem("toggle-playback-on-ruler-click-enabled"),
        makeSeparator(),
        makeMenuItem("toggle-loop-region"),
        makeMenuItem("clear-loop-region"),
        makeMenuItem("set-loop-region-to-selection"),
        makeMenuItem("set-selection-to-loop"),
        makeMenuItem("toggle-selection-follows-loop-region"),
        makeSeparator(),
        makeMenuItem("toggle-vertical-rulers"),
    };

    return items;
}
