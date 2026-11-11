/*
* GlideMix: the grid section Ableton puts at the bottom of its arrangement
* menus -- Snap to grid, the fixed grid sizes, Narrow / Widen, Triplets.
*/
#pragma once

#include <functional>

#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"
#include "projectscene/types/projectscenetypes.h"

namespace au::projectscene {
using MakeMenuItem = std::function<muse::uicomponents::MenuItem*(const muse::actions::ActionCode&, const muse::TranslatableString&)>;
using MakeSubMenu = std::function<muse::uicomponents::MenuItem*(const muse::TranslatableString&, const muse::uicomponents::MenuItemList&,
                                                                 const QString&)>;
using MakeSeparator = std::function<muse::uicomponents::MenuItem*()>;

//! "Snap to grid" (checked when on) and a "Grid" submenu. `snap` is the
//! project's current snapping; without a project pass a default Snap.
muse::uicomponents::MenuItemList makeGridMenuItems(const Snap& snap, const MakeMenuItem& makeItem, const MakeSubMenu& makeMenu,
                                                   const MakeSeparator& makeSeparator);
}
