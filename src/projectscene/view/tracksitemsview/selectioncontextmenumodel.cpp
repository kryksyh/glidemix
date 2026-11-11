/**********************************************************************

  Audacity: A Digital Audio Editor

**********************************************************************/

#include "selectioncontextmenumodel.h"

#include "gridmenuitems.h"

using namespace au::projectscene;
using namespace muse;
using namespace muse::uicomponents;
using namespace muse::actions;

void SelectionContextMenuModel::load()
{
    AbstractMenuModel::load();

    MenuItemList items = makeItems();
    setItems(items);
}

MenuItemList SelectionContextMenuModel::gridItems()
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

// GlideMix (2026-11-11): Ableton's order -- edit, zoom, split, loop and
// process, the "…Time" commands, then the grid. The rarer gap options stay
// in one submenu.
MenuItemList SelectionContextMenuModel::makeItems()
{
    MenuItemList moreItems {
        makeMenuItem("cut-leave-gap",
                     muse::TranslatableString("selection", "Cut and leave gap")),
        makeMenuItem("cut-per-clip-ripple",
                     muse::TranslatableString("selection", "Cut and close gap")),
        makeMenuItem("cut-per-track-ripple",
                     muse::TranslatableString("selection", "Cut and close gap on this track")),
        makeSeparator(),
        makeMenuItem("action://trackedit/paste-overlap",
                     muse::TranslatableString("selection", "Paste and overlap")),
        makeMenuItem("action://trackedit/paste-insert",
                     muse::TranslatableString("selection", "Paste and make room on this track")),
        makeSeparator(),
        makeMenuItem("delete-leave-gap",
                     muse::TranslatableString("selection", "Delete and leave gap")),
        makeMenuItem("delete-per-clip-ripple",
                     muse::TranslatableString("selection", "Delete and close gap")),
        makeMenuItem("delete-per-track-ripple",
                     muse::TranslatableString("selection", "Delete and close gap on this track")),
    };

    MenuItemList items {
        makeMenuItem("action://trackedit/cut"),
        makeMenuItem("action://trackedit/copy"),
        makeMenuItem("action://trackedit/paste-default"),
        makeMenuItem("duplicate"),
        makeMenuItem("action://trackedit/delete"),
        makeSeparator(),
        makeMenuItem("zoom-to-selection"),
        makeMenuItem("zoom-back"),
        makeSeparator(),
        makeMenuItem("split"),
        makeMenuItem("join",
                     //: Verb: context menu command that joins the selected audio
                     muse::TranslatableString("selection", "Join")),
        makeMenuItem("disjoin",
                     muse::TranslatableString("selection", "Split at silences")),
        makeSeparator(),
        // GlideMix: loop the selected section (Logic/Ableton).
        makeMenuItem("set-loop-region-to-selection",
                     muse::TranslatableString("selection", "Loop selection")),
        makeMenuItem("trim-audio-outside-selection",
                     muse::TranslatableString("selection", "Crop")),
        makeMenuItem("reverse"),
        makeMenuItem("silence-audio-selection",
                     muse::TranslatableString("selection", "Silence")),
        makeSeparator(),
        // Ableton's "…Time" commands: every track, the song gets shorter or longer.
        makeMenuItem("cut-all-tracks-ripple",
                     muse::TranslatableString("selection", "Cut time")),
        makeMenuItem("action://trackedit/paste-insert-all-tracks-ripple",
                     muse::TranslatableString("selection", "Paste time")),
        makeMenuItem("delete-all-tracks-ripple",
                     muse::TranslatableString("selection", "Delete time")),
        makeMenu(muse::TranslatableString("selection", "More cut, paste and delete"), moreItems, "menu-more-edit"),
        makeSeparator(),
    };
    for (MenuItem* item : gridItems()) {
        items.push_back(item);
    }

    return items;
}
