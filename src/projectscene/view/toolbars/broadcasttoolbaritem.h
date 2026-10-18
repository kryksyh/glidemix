/*
 * GlideMix
 */
#pragma once

#include "modularity/ioc.h"

#include "uicomponents/qml/Muse/UiComponents/toolbaritem.h"

#include "broadcast/ibroadcaster.h"

namespace au::projectscene {
//! The radio tower in the header: click to go on air with the output named on
//! the button, right-click to choose which output that is.
class BroadcastToolBarItem : public muse::uicomponents::ToolBarItem
{
    Q_OBJECT

    //! 0 off, 1 connecting, 2 live, 3 failed -- the button's colour.
    Q_PROPERTY(int broadcastState READ broadcastState NOTIFY broadcastChanged)
    //! "PERFORM" or "CUE": which output the listeners get.
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY broadcastChanged)

    muse::GlobalInject<broadcast::IBroadcaster> broadcaster;

public:
    explicit BroadcastToolBarItem(const muse::ui::UiAction& action, muse::uicomponents::ToolBarItemType::Type type,
                                  QObject* parent = nullptr);

    int broadcastState() const;
    QString sourceName() const;

    Q_INVOKABLE void handleMenuItem(const QString& itemId);

signals:
    void broadcastChanged();

private:
    void rebuild();
};
}
