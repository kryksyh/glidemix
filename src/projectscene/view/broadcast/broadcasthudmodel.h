/*
 * GlideMix
 */
#pragma once

#include <QObject>
#include <QString>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "interactive/iinteractive.h"
#include "actions/iactionsdispatcher.h"

#include "broadcast/ibroadcaster.h"
#include "broadcast/ibroadcastconfiguration.h"

namespace au::projectscene {
//! Backs the broadcast window: what is on air, what it is streaming, and where
//! to. The full settings live in Preferences; this is the operator's desk.
class BroadcastHudModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    muse::GlobalInject<broadcast::IBroadcaster> broadcaster;
    muse::GlobalInject<broadcast::IBroadcastConfiguration> configuration;
    muse::ContextInject<muse::IInteractive> interactive { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher { this };

    //! 0 off, 1 connecting, 2 live, 3 failed.
    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(int secondsOnAir READ secondsOnAir NOTIFY changed)
    //! 0 = the Perform output, 1 = the headphone cue.
    Q_PROPERTY(int source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(QString serverSummary READ serverSummary NOTIFY changed)
    //! What the listeners are getting, dBFS per channel, for the meters.
    Q_PROPERTY(double peakLeft READ peakLeft NOTIFY changed)
    Q_PROPERTY(double peakRight READ peakRight NOTIFY changed)
    //! The stream's own level, dB.
    Q_PROPERTY(double gainDb READ gainDb WRITE setGainDb NOTIFY changed)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY changed)

public:
    explicit BroadcastHudModel(QObject* parent = nullptr);

    Q_INVOKABLE void init();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void reconnect();
    //! True when the window may close; asks first if it would go off air.
    Q_INVOKABLE bool confirmClose();
    Q_INVOKABLE void openSettings();

    int state() const;
    QString statusText() const;
    int secondsOnAir() const;
    int source() const;
    void setSource(int source);
    QString serverSummary() const;
    double peakLeft() const;
    double peakRight() const;
    double gainDb() const;
    void setGainDb(double db);
    bool autoReconnect() const;
    void setAutoReconnect(bool on);

signals:
    void changed();

private:
    QTimer* m_tick = nullptr;
};
}
