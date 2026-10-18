/*
 * GlideMix: the Learn page on Home -- its buttons run app actions.
 */
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include "framework/global/modularity/ioc.h"
#include "actions/iactionsdispatcher.h"

namespace au::appshell {
class HomeLearnModel : public QObject, public muse::Contextable
{
    Q_OBJECT
    QML_ELEMENT

    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher { this };

public:
    explicit HomeLearnModel(QObject* parent = nullptr);

    Q_INVOKABLE void dispatch(const QString& actionCode);
};
}
