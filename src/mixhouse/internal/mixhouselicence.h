/*
 * GlideMix licence, through Polar's public licence-key API.
 */
#pragma once

#include <QObject>

#include "../imixhouselicence.h"

class QJsonObject;
class QNetworkAccessManager;
class QNetworkReply;

namespace au::mixhouse {
class MixHouseLicence : public QObject, public IMixHouseLicence
{
    Q_OBJECT
public:
    MixHouseLicence();

    void init();

    LicenceTier tier() const override { return m_tier; }
    QString key() const override { return m_key; }
    void activate(const QString& key, Done done) override;
    void deactivate(Done done) override;
    QString buyUrl(LicenceTier tier) const override;
    muse::async::Notification tierChanged() const override { return m_tierChanged; }

private:
    void setTier(LicenceTier tier);
    void save();
    void validateInBackground();
    QNetworkReply* post(const QString& endpoint, const QJsonObject& body);
    static LicenceTier tierForKey(const QString& key);
    static QString errorText(QNetworkReply* reply, const QByteArray& body);

    QNetworkAccessManager* m_net = nullptr;
    LicenceTier m_tier = LicenceTier::Demo;
    QString m_key;
    QString m_activationId;
    muse::async::Notification m_tierChanged;
};
}
