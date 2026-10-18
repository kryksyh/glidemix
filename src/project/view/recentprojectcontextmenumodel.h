/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "framework/uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"
#include "framework/global/modularity/ioc.h"
#include "framework/interactive/iplatforminteractive.h"

#include <functional>

namespace au::project {
class RecentProjectContextMenuModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT

    muse::GlobalInject<muse::IPlatformInteractive> platformInteractive;

public:
    RecentProjectContextMenuModel(bool isCloudProject, QString path, QString displayNameOverride, QString cloudProjectId,
                                  QObject* parent = nullptr);

    //! GlideMix: "Delete…" on a local project (asks, then moves it to the Trash).
    void setDeleteHandler(std::function<void(const QString& path)> handler) { m_deleteHandler = std::move(handler); }

    Q_INVOKABLE void load() override;
    void handleMenuItem(const QString& itemId) override;

private:
    bool isCloudProject;
    QString m_path;
    QString m_displayNameOverride;
    QString m_cloudProjectId;
    std::function<void(const QString& path)> m_deleteHandler;
};
}
