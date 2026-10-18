/*
 * GlideMix
 */
#pragma once

#include <QObject>
#include <QQmlParserStatus>
#include <QtQmlIntegration>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "playback/iperformance.h"

namespace au::mixhouse {
//! The performance (the audience playback, MixMeister Perform) for the
//! timeline: the red playhead and the locked region it has played.
class MixHousePerformanceModel : public QObject, public QQmlParserStatus, public muse::Contextable,
    public muse::async::Asyncable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool performing READ performing NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)

    muse::ContextInject<playback::IPerformance> performance { this };

public:
    explicit MixHousePerformanceModel(QObject* parent = nullptr);

    bool active() const;
    bool performing() const;
    double position() const;

    Q_INVOKABLE void setPosition(double seconds);

    void classBegin() override {}
    void componentComplete() override;

signals:
    void stateChanged();
    void positionChanged();
};
}
