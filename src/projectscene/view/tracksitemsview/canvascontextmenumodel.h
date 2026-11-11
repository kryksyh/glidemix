/**********************************************************************

  Audacity: A Digital Audio Editor

**********************************************************************/

#pragma once

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

namespace au::projectscene {
class CanvasContextMenuModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT

    muse::ContextInject<context::IGlobalContext> globalContext{ this };   // GlideMix: the grid section

public:
    Q_INVOKABLE void load() override;

private:

    muse::uicomponents::MenuItemList makeItems();
    muse::uicomponents::MenuItemList gridItems();
};
}
