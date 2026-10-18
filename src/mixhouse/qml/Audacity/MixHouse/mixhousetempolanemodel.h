/*
 * GlideMix
 */
#pragma once

#include <QObject>
#include <QQmlParserStatus>
#include <QtQmlIntegration>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/imixhousetempomap.h"
#include "context/iglobalcontext.h"

namespace au::mixhouse {
/// State shared by the TEMPO lane (under the ruler) and its header row in the
/// tracks panel: whether it is shown, its height, and the Reset button.
class MixHouseTempoLaneModel : public QObject, public QQmlParserStatus, public muse::Contextable,
    public muse::async::Asyncable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(bool laneVisible READ laneVisible NOTIFY laneChanged)
    Q_PROPERTY(int laneHeight READ laneHeight WRITE setLaneHeight NOTIFY laneChanged)
    Q_PROPERTY(int defaultLaneHeight READ defaultLaneHeight CONSTANT)
    Q_PROPERTY(bool followsTracks READ followsTracks WRITE setFollowsTracks NOTIFY tempoChanged)

    muse::GlobalInject<IMixHouseLibraryConfiguration> configuration;
    muse::ContextInject<IMixHouseTempoMap> tempoMap { this };
    muse::ContextInject<context::IGlobalContext> globalContext { this };

public:
    explicit MixHouseTempoLaneModel(QObject* parent = nullptr);

    bool laneVisible() const;
    int laneHeight() const;
    void setLaneHeight(int height);
    int defaultLaneHeight() const { return 96; }

    Q_INVOKABLE void reset();
    Q_INVOKABLE void nudgeMasterBpm(double deltaBpm);

    bool followsTracks() const;
    void setFollowsTracks(bool follow);

    void classBegin() override {}
    void componentComplete() override;

signals:
    void laneChanged();
    void tempoChanged();
};
}
