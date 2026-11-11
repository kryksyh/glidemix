/*
* GlideMix: the grid section of the arrangement menus (see gridmenuitems.h).
*/
#include "gridmenuitems.h"

using namespace au::projectscene;
using namespace muse;
using namespace muse::uicomponents;
using namespace muse::actions;

MenuItemList au::projectscene::makeGridMenuItems(const Snap& snap, const MakeMenuItem& makeItem, const MakeSubMenu& makeMenu,
                                                 const MakeSeparator& makeSeparator)
{
    MenuItem* snapItem = makeItem("snap-toggle", {});
    snapItem->setCheckable(true);
    snapItem->setChecked(snap.enabled);

    struct Size {
        SnapType type;
        const char* title;
    };
    static const Size sizes[] = {
        { SnapType::Bar, "1 Bar" },
        { SnapType::Half, "1/2" },
        { SnapType::Quarter, "1/4" },
        { SnapType::Eighth, "1/8" },
        { SnapType::Sixteenth, "1/16" },
        { SnapType::ThirtySecond, "1/32" },
    };
    MenuItemList grid;
    for (const Size& size : sizes) {
        MenuItem* item = makeItem("grid-set", TranslatableString("grid", size.title));
        // Every item shares one action code, so each needs its own id.
        item->setId(QStringLiteral("grid-set-%1").arg(static_cast<int>(size.type)));
        item->setArgs(ActionData::make_arg1<int>(static_cast<int>(size.type)));
        item->setCheckable(true);
        item->setChecked(snap.enabled && snap.type == size.type);
        grid.push_back(item);
    }
    grid.push_back(makeSeparator());
    grid.push_back(makeItem("grid-narrow", {}));
    grid.push_back(makeItem("grid-widen", {}));
    MenuItem* triplets = makeItem("grid-triplets", {});
    triplets->setCheckable(true);
    triplets->setChecked(snap.isSnapTriplets);
    grid.push_back(triplets);

    return {
        snapItem,
        makeMenu(TranslatableString("grid", "Grid"), grid, QStringLiteral("menu-grid")),
    };
}
