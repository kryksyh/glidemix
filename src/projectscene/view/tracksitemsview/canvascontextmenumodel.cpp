/**********************************************************************

  Audacity: A Digital Audio Editor

**********************************************************************/

#include "canvascontextmenumodel.h"

#include "gridmenuitems.h"

using namespace au::projectscene;
using namespace muse;
using namespace muse::uicomponents;
using namespace muse::actions;

void CanvasContextMenuModel::load()
{
    AbstractMenuModel::load();

    MenuItemList items = makeItems();
    setItems(items);
}

MenuItemList CanvasContextMenuModel::makeItems()
{
    MenuItemList pasteAndItems {
        makeMenuItem("action://trackedit/paste-overlap",
                     muse::TranslatableString("canvas", "Paste and overlap")),
        makeMenuItem("action://trackedit/paste-insert",
                     muse::TranslatableString("canvas", "Paste and make room on this track")),
        makeMenuItem("action://trackedit/paste-insert-all-tracks-ripple",
                     muse::TranslatableString("canvas", "Paste and make room on all tracks")),
    };

    MenuItemList items {
        makeMenuItem("action://trackedit/paste-default"),
        makeMenu(muse::TranslatableString("canvas", "Paste and…"), pasteAndItems, "menu-paste-and"),
        makeSeparator(),
        makeMenuItem("new-mono-track"),
        makeMenuItem("new-stereo-track"),
        makeMenuItem("new-label-track"),
        makeSeparator(),
        makeMenuItem("select-all"),
        makeMenuItem("clear-selection"),
        // GlideMix: Ableton's zoom keys and grid section, here too.
        makeSeparator(),
        makeMenuItem("zoom-back"),
        makeSeparator(),
    };
    for (MenuItem* item : gridItems()) {
        items.push_back(item);
    }

    return items;
}

MenuItemList CanvasContextMenuModel::gridItems()
{
    Snap snap;
    if (const auto project = globalContext()->currentProject(); project && project->viewState()) {
        snap = project->viewState()->getSnap();
    }
    return makeGridMenuItems(snap,
                             [this](const ActionCode& code, const TranslatableString& title) { return makeMenuItem(code, title); },
                             [this](const TranslatableString& title, const MenuItemList& items, const QString& id) {
        return makeMenu(title, items, id);
    },
                             [this]() { return makeSeparator(); });
}
