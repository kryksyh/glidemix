/*
 * GlideMix
 */
#pragma once

#include "modularity/ioc.h"
#include "ui/iuiactionsregister.h"

#include "uicomponents/qml/Muse/UiComponents/toolbaritem.h"
#include "uicomponents/qml/Muse/UiComponents/menuitem.h"

#include "playback/iperformance.h"

namespace au::projectscene {
//! PERFORM: click to perform or pause; right-click for Perform from the
//! playhead, Reset performance position and where the headphones are.
class PerformToolBarItem : public muse::uicomponents::ToolBarItem
{
    Q_OBJECT

    muse::ContextInject<playback::IPerformance> performance { this };
    muse::ContextInject<muse::ui::IUiActionsRegister> uiActionsRegister { this };

public:
    explicit PerformToolBarItem(const muse::ui::UiAction& action, muse::uicomponents::ToolBarItemType::Type type,
                                QObject* parent = nullptr);

    Q_INVOKABLE void handleMenuItem(const QString& itemId);

private:
    void rebuildMenu();
};
}
