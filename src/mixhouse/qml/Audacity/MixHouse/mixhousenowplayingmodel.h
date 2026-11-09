/*
 * GlideMix: the values in the capsule display at the top of the project
 * window -- key, energy and place in the mix of the song under the playhead,
 * whether it plays, and how busy the computer's processor is with GlideMix.
 */
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QQmlParserStatus>
#include <QTimer>
#include <QtQmlIntegration>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "context/iglobalcontext.h"
#include "mixhouse/imixhousebeatgrids.h"
#include "mixhouse/imixhousekeymatch.h"
#include "mixhouse/imixhouselibrary.h"
#include "mixhouse/imixhouselibraryconfiguration.h"
#include "trackedit/iprojecthistory.h"

namespace au::mixhouse {
class MixHouseNowPlayingModel : public QObject, public QQmlParserStatus, public muse::Contextable,
    public muse::async::Asyncable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(QString keyText READ keyText NOTIFY songChanged)
    Q_PROPERTY(QColor keyColor READ keyColor NOTIFY songChanged)
    Q_PROPERTY(int energy READ energy NOTIFY songChanged)
    Q_PROPERTY(int songIndex READ songIndex NOTIFY songChanged)
    Q_PROPERTY(int songCount READ songCount NOTIFY songChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(int cpuPercent READ cpuPercent NOTIFY cpuChanged)
    //! MB per second GlideMix itself read from disk over the last second
    Q_PROPERTY(double diskMBps READ diskMBps NOTIFY cpuChanged)

    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<IMixHouseLibrary> library { this };
    muse::ContextInject<IMixHouseBeatGrids> beatGrids { this };
    muse::ContextInject<IMixHouseKeyMatch> keyMatch { this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory { this };
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;

public:
    explicit MixHouseNowPlayingModel(QObject* parent = nullptr);

    QString keyText() const { return m_keyText; }
    QColor keyColor() const { return m_keyColor; }
    int energy() const { return m_energy; }
    int songIndex() const { return m_songIndex; }
    int songCount() const { return m_songCount; }
    bool playing() const { return m_playing; }
    int cpuPercent() const { return m_cpuPercent; }
    double diskMBps() const { return m_diskMBps; }

    void classBegin() override {}
    void componentComplete() override;

signals:
    void songChanged();
    void playingChanged();
    void cpuChanged();

private:
    void update();
    void sampleCpu();

    struct SongInfo {
        int keyId = 0;
        int energy = 0;
    };
    SongInfo songFor(const QString& path);

    QTimer m_updateTimer;
    QTimer m_cpuTimer;
    QHash<QString, SongInfo> m_songs;   // by file path, so the library is asked once per song

    QString m_keyText;
    QColor m_keyColor;
    int m_energy = 0;
    int m_songIndex = 0;
    int m_songCount = 0;
    bool m_playing = false;

    int m_cpuPercent = 0;
    double m_lastCpuSecs = -1.0;
    double m_diskMBps = 0.0;
    double m_lastDiskBytes = -1.0;
    QElapsedTimer m_cpuClock;
};
}
