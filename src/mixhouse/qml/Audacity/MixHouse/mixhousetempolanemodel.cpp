/*
 * GlideMix
 */
#include "mixhousetempolanemodel.h"

using namespace au::mixhouse;

MixHouseTempoLaneModel::MixHouseTempoLaneModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void MixHouseTempoLaneModel::componentComplete()
{
    if (configuration()) {
        configuration()->settingsChanged().onNotify(this, [this]() { emit laneChanged(); });
    }
    emit laneChanged();
    if (tempoMap()) {
        tempoMap()->tempoMapChanged().onNotify(this, [this]() { emit tempoChanged(); });
    }
}

bool MixHouseTempoLaneModel::followsTracks() const
{
    return tempoMap() && tempoMap()->followsTracks();
}

void MixHouseTempoLaneModel::setFollowsTracks(bool follow)
{
    if (tempoMap()) {
        tempoMap()->setFollowsTracks(follow);
    }
}

void MixHouseTempoLaneModel::nudgeMasterBpm(double deltaBpm)
{
    if (!tempoMap()) {
        return;
    }
    const auto playback = globalContext() ? globalContext()->playbackState() : nullptr;
    const double at = playback ? playback->playbackPosition().to_double() : 0.0;
    tempoMap()->nudgeMasterBpm(deltaBpm, at);
}

bool MixHouseTempoLaneModel::laneVisible() const
{
    return configuration() && configuration()->tempoLaneVisible();
}

int MixHouseTempoLaneModel::laneHeight() const
{
    return configuration() ? configuration()->tempoLaneHeight() : defaultLaneHeight();
}

void MixHouseTempoLaneModel::setLaneHeight(int height)
{
    if (configuration() && height != laneHeight()) {
        configuration()->setTempoLaneHeight(height);
    }
}

void MixHouseTempoLaneModel::reset()
{
    if (tempoMap()) {
        tempoMap()->reset();
    }
}
