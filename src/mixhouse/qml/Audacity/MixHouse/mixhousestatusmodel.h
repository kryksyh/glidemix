/*
 * GlideMix: the status line at the bottom of the window, like Ableton's and
 * rekordbox's. What the app is doing in the background (scanning, analysis,
 * the rekordbox XML, energy, the stems engine), a short message when that
 * ends, and a notice when a new version is out.
 */
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QQmlParserStatus>
#include <QTimer>
#include <QtQmlIntegration>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "mixhouse/imixhouseactivity.h"
#include "mixhouse/imixhouselibrary.h"
#include "mixhouse/imixhouserekordboxxml.h"
#include "update/iappupdatescenario.h"
#include "update/iappupdateservice.h"
#include "update/iupdateconfiguration.h"

namespace au::mixhouse {
class MixHouseStatusModel : public QObject, public QQmlParserStatus, public muse::Contextable,
    public muse::async::Asyncable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(QString text READ text NOTIFY statusChanged)
    Q_PROPERTY(int percent READ percent NOTIFY statusChanged)
    Q_PROPERTY(bool canStop READ canStop NOTIFY statusChanged)
    Q_PROPERTY(QString updateVersion READ updateVersion NOTIFY updateChanged)

    muse::ContextInject<IMixHouseLibrary> library { this };
    muse::ContextInject<IMixHouseRekordboxXml> rekordboxXml { this };
    muse::ContextInject<IMixHouseActivity> activity { this };
    muse::ContextInject<muse::update::IAppUpdateService> updateService { this };
    muse::ContextInject<muse::update::IAppUpdateScenario> updateScenario { this };
    muse::GlobalInject<muse::update::IUpdateConfiguration> updateConfiguration;

public:
    explicit MixHouseStatusModel(QObject* parent = nullptr);

    bool busy() const;
    QString text() const;
    int percent() const;
    bool canStop() const;
    QString updateVersion() const;

    Q_INVOKABLE void stop();
    Q_INVOKABLE void openUpdate();

    void classBegin() override {}
    void componentComplete() override;

signals:
    void statusChanged();
    void updateChanged();

private:
    void refresh();
    void showMessage(const QString& message);
    QString idleText() const;
    QString trackName(int trackId);
    void checkForUpdate();

    // Library scan
    bool m_scanning = false;
    int m_scanned = 0;
    int m_scanAdded = 0;
    // Analysis
    bool m_analysing = false;
    int m_analysed = 0;
    int m_analyseTotal = 0;
    int m_currentTrack = -1;
    QHash<int, QString> m_trackNames;
    QElapsedTimer m_analysisClock;
    // A finished task's message, shown for a few seconds
    QString m_message;
    QTimer m_messageTimer;
    QTimer m_rekordboxPoll;
    QTimer m_settleTimer;
    QElapsedTimer m_lastBusy;
    QTimer m_updateTimer;
    QString m_updateVersion;
    // What is shown
    bool m_busy = false;
    QString m_text;
    int m_percent = -1;
};
}
