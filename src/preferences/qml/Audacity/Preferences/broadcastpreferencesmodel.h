/*
 * GlideMix
 */
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "interactive/iinteractive.h"
#include "actions/iactionsdispatcher.h"

#include "broadcast/ibroadcaster.h"
#include "broadcast/ibroadcastconfiguration.h"

namespace au::preferences {
/// Backs BroadcastingPreferencesPage.qml: the one server the app streams to,
/// and a Connect button that puts it on air from here.
class BroadcastPreferencesModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    muse::GlobalInject<broadcast::IBroadcastConfiguration> configuration;
    muse::GlobalInject<broadcast::IBroadcaster> broadcaster;
    muse::ContextInject<muse::IInteractive> interactive { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher { this };

    /// 0 = Icecast 2, 1 = Shoutcast.
    Q_PROPERTY(int protocol READ protocol WRITE setProtocol NOTIFY changed)
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY changed)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY changed)
    Q_PROPERTY(QString mount READ mount WRITE setMount NOTIFY changed)
    Q_PROPERTY(QString login READ login WRITE setLogin NOTIFY changed)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY changed)
    //! Shoutcast 2 only: which of the server's streams this is.
    Q_PROPERTY(int streamId READ streamId WRITE setStreamId NOTIFY changed)

    Q_PROPERTY(QString streamName READ streamName WRITE setStreamName NOTIFY changed)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY changed)
    Q_PROPERTY(QString genre READ genre WRITE setGenre NOTIFY changed)
    Q_PROPERTY(QString website READ website WRITE setWebsite NOTIFY changed)
    Q_PROPERTY(bool publicStream READ publicStream WRITE setPublicStream NOTIFY changed)
    Q_PROPERTY(int bitrate READ bitrate WRITE setBitrate NOTIFY changed)

    /// 0 = the Perform output, 1 = the headphone cue.
    Q_PROPERTY(int source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(bool sendMetadata READ sendMetadata WRITE setSendMetadata NOTIFY changed)
    Q_PROPERTY(bool titleFirst READ titleFirst WRITE setTitleFirst NOTIFY changed)

    /// 0 off, 1 connecting, 2 live, 3 failed.
    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

    //! What a listener types into a player or a browser to hear the stream.
    Q_PROPERTY(QString listenUrl READ listenUrl NOTIFY changed)

public:
    explicit BroadcastPreferencesModel(QObject* parent = nullptr);

    Q_INVOKABLE void init();
    /// Closing the HUD while on air: asks, and takes the stream down on a yes.
    /// Returns true when the window may close.
    Q_INVOKABLE bool confirmClose();
    /// Opens the full Live broadcasting preferences page.
    Q_INVOKABLE void openSettings();
    /// Go on air, or stop: the same button the header has.
    Q_INVOKABLE void toggle();
    /// Off and on again: the quickest fix when a server drops.
    Q_INVOKABLE void reconnect();

    QString listenUrl() const;
    /// Puts the listen URL on the clipboard.
    Q_INVOKABLE void copyListenUrl();

    int protocol() const;
    void setProtocol(int protocol);
    QString host() const;
    void setHost(const QString& host);
    int port() const;
    void setPort(int port);
    QString mount() const;
    void setMount(const QString& mount);
    QString login() const;
    void setLogin(const QString& login);
    QString password() const;
    void setPassword(const QString& password);
    int streamId() const;
    void setStreamId(int streamId);

    QString streamName() const;
    void setStreamName(const QString& name);
    QString description() const;
    void setDescription(const QString& description);
    QString genre() const;
    void setGenre(const QString& genre);
    QString website() const;
    void setWebsite(const QString& website);
    bool publicStream() const;
    void setPublicStream(bool isPublic);
    int bitrate() const;
    void setBitrate(int bitrate);

    int source() const;
    void setSource(int source);
    bool sendMetadata() const;
    bool titleFirst() const;
    void setTitleFirst(bool titleFirst);
    void setSendMetadata(bool send);

    int state() const;
    QString statusText() const;

signals:
    void changed();

private:
    broadcast::BroadcastServer server() const;
    void write(const broadcast::BroadcastServer& server);
};
}
