/*
 * GlideMix: the licence window's model (Help > Licence…).
 */
#pragma once

#include <QObject>
#include <QtQmlIntegration>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "imixhouselicence.h"

namespace au::mixhouse {
class MixHouseLicenceModel : public QObject, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isDemo READ isDemo NOTIFY changed)
    Q_PROPERTY(QString tierName READ tierName NOTIFY changed)
    Q_PROPERTY(QString key READ key NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)

    muse::GlobalInject<IMixHouseLicence> licence;

public:
    explicit MixHouseLicenceModel(QObject* parent = nullptr);

    bool isDemo() const;
    QString tierName() const;
    QString key() const;
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    bool messageIsError() const { return m_messageIsError; }

    Q_INVOKABLE void activate(const QString& key);
    Q_INVOKABLE void deactivate();
    //! Opens the checkout: pro = GlideMix Pro, otherwise GlideMix.
    Q_INVOKABLE void buy(bool pro);

signals:
    void changed();
    void busyChanged();
    void messageChanged();
    void activated();

private:
    void setBusy(bool busy);
    void setMessage(const QString& text, bool error);

    bool m_busy = false;
    QString m_message;
    bool m_messageIsError = false;
};
}
