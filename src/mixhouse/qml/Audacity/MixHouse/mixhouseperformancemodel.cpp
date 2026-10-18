/*
 * GlideMix
 */
#include "mixhouseperformancemodel.h"

using namespace au::mixhouse;

MixHousePerformanceModel::MixHousePerformanceModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void MixHousePerformanceModel::componentComplete()
{
    if (!performance()) {
        return;
    }
    performance()->performanceStateChanged().onNotify(this, [this]() {
        emit stateChanged();
        emit positionChanged();
    });
    performance()->performancePositionChanged().onReceive(this, [this](muse::secs_t) { emit positionChanged(); });
}

bool MixHousePerformanceModel::active() const
{
    return performance() && performance()->performanceActive();
}

bool MixHousePerformanceModel::performing() const
{
    return performance() && performance()->performing();
}

double MixHousePerformanceModel::position() const
{
    return performance() ? performance()->performancePosition().to_double() : 0.0;
}

void MixHousePerformanceModel::setPosition(double seconds)
{
    if (performance()) {
        performance()->setPerformancePosition(seconds);
    }
}
