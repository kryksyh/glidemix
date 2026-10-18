/*
 * GlideMix
 */
#include "homelearnmodel.h"

using namespace au::appshell;

HomeLearnModel::HomeLearnModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void HomeLearnModel::dispatch(const QString& actionCode)
{
    if (dispatcher()) {
        dispatcher()->dispatch(actionCode.toStdString());
    }
}
