/*
 * GlideMix licence, through Polar's public licence-key API.
 */
#include "mixhouselicence.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QTimer>

#include "au3-mixtape-engine/MixtapeDemo.h"
#include "global/settings.h"
#include "log.h"

using namespace au::mixhouse;

namespace {
const std::string module_name("mixhouse");
const muse::Settings::Key LICENCE_KEY(module_name, "mixhouse/licence/key");
const muse::Settings::Key LICENCE_ACTIVATION(module_name, "mixhouse/licence/activationId");
const muse::Settings::Key LICENCE_TIER(module_name, "mixhouse/licence/tier");
const muse::Settings::Key LICENCE_CHECKED(module_name, "mixhouse/licence/lastChecked");

const QString kApi = QStringLiteral("https://api.polar.sh/v1/customer-portal/license-keys/");
const QString kOrganization = QStringLiteral("22232ab0-827f-40d2-8860-74c637d399b9");
const QString kBuyStandard = QStringLiteral("https://buy.polar.sh/polar_cl_TDU3Z0o0w98m8mkhaIMHA9gKkNpOMlv3VPZEr0sWq9G");
const QString kBuyPro = QStringLiteral("https://buy.polar.sh/polar_cl_4uiOdwpksKcRSApaB9BWrDJ0Scu1AFet9FgtC4ZQyQJ");
}

MixHouseLicence::MixHouseLicence()
{
}

void MixHouseLicence::init()
{
    muse::settings()->setDefaultValue(LICENCE_KEY, muse::Val(std::string()));
    muse::settings()->setDefaultValue(LICENCE_ACTIVATION, muse::Val(std::string()));
    muse::settings()->setDefaultValue(LICENCE_TIER, muse::Val(0));
    muse::settings()->setDefaultValue(LICENCE_CHECKED, muse::Val(std::string()));

    m_net = new QNetworkAccessManager(this);
    m_key = QString::fromStdString(muse::settings()->value(LICENCE_KEY).toString());
    m_activationId = QString::fromStdString(muse::settings()->value(LICENCE_ACTIVATION).toString());
    const int stored = muse::settings()->value(LICENCE_TIER).toInt();
    m_tier = (!m_key.isEmpty() && !m_activationId.isEmpty() && stored >= 1 && stored <= 2)
             ? static_cast<LicenceTier>(stored) : LicenceTier::Demo;
    mixtape::SetDemoMode(m_tier == LicenceTier::Demo);
    LOGI() << "GlideMix licence: " << (m_tier == LicenceTier::Demo ? "demo" : m_tier == LicenceTier::Pro ? "Pro" : "GlideMix");
    if (m_tier != LicenceTier::Demo) {
        // Once a session, as Polar recommends; offline, the stored licence holds.
        QTimer::singleShot(5000, this, [this] { validateInBackground(); });
    }
}

QString MixHouseLicence::buyUrl(LicenceTier tier) const
{
    return tier == LicenceTier::Pro ? kBuyPro : kBuyStandard;
}

LicenceTier MixHouseLicence::tierForKey(const QString& key)
{
    const QString k = key.trimmed().toUpper();
    if (k.startsWith(QStringLiteral("GLIDEMIXPRO"))) {
        return LicenceTier::Pro;
    }
    if (k.startsWith(QStringLiteral("GLIDEMIX"))) {
        return LicenceTier::Standard;
    }
    return LicenceTier::Demo;
}

void MixHouseLicence::setTier(LicenceTier tier)
{
    mixtape::SetDemoMode(tier == LicenceTier::Demo);
    if (m_tier != tier) {
        m_tier = tier;
        m_tierChanged.notify();
    }
}

void MixHouseLicence::save()
{
    muse::settings()->setSharedValue(LICENCE_KEY, muse::Val(m_key.toStdString()));
    muse::settings()->setSharedValue(LICENCE_ACTIVATION, muse::Val(m_activationId.toStdString()));
    muse::settings()->setSharedValue(LICENCE_TIER, muse::Val(static_cast<int>(m_tier)));
}

QNetworkReply* MixHouseLicence::post(const QString& endpoint, const QJsonObject& body)
{
    QNetworkRequest request(QUrl(kApi + endpoint));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(15000);
    return m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

QString MixHouseLicence::errorText(QNetworkReply* reply, const QByteArray& body)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0) {
        return QObject::tr("No connection to the licence server. Check the internet connection and try again.");
    }
    if (status == 404) {
        return QObject::tr("This licence key was not found. Check it for typing errors (copy it from your order email).");
    }
    const QString detail = QJsonDocument::fromJson(body).object().value(QStringLiteral("detail")).toString();
    if (status == 403 && detail.contains(QStringLiteral("limit"), Qt::CaseInsensitive)) {
        return QObject::tr("This key is already used on 3 computers. Free one in your Polar customer portal "
                           "(or with Deactivate on that computer), then try again.");
    }
    return detail.isEmpty() ? QObject::tr("The licence server answered with an error (%1).").arg(status) : detail;
}

void MixHouseLicence::activate(const QString& rawKey, Done done)
{
    const QString key = rawKey.trimmed();
    const LicenceTier tier = tierForKey(key);
    if (tier == LicenceTier::Demo) {
        done(false, QObject::tr("This is not a GlideMix licence key. It starts with GLIDEMIX."));
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("key"), key);
    body.insert(QStringLiteral("organization_id"), kOrganization);
    body.insert(QStringLiteral("label"), QSysInfo::machineHostName() + QStringLiteral(" (") + QSysInfo::prettyProductName()
                + QStringLiteral(")"));
    QNetworkReply* reply = post(QStringLiteral("activate"), body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, tier, done] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject json = QJsonDocument::fromJson(data).object();
        if (status != 200 || json.value(QStringLiteral("id")).toString().isEmpty()) {
            LOGW() << "GlideMix licence: activation failed, status " << status << ": " << QString::fromUtf8(data.left(300));
            done(false, errorText(reply, data));
            return;
        }
        const QString keyStatus = json.value(QStringLiteral("license_key")).toObject().value(QStringLiteral("status")).toString();
        if (!keyStatus.isEmpty() && keyStatus != QStringLiteral("granted")) {
            done(false, QObject::tr("This licence key is no longer valid (%1).").arg(keyStatus));
            return;
        }
        m_key = key;
        m_activationId = json.value(QStringLiteral("id")).toString();
        setTier(tier);
        save();
        muse::settings()->setSharedValue(LICENCE_CHECKED, muse::Val(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()));
        LOGI() << "GlideMix licence: activated (" << (tier == LicenceTier::Pro ? "Pro" : "GlideMix") << ")";
        done(true, tier == LicenceTier::Pro ? QObject::tr("Thank you! GlideMix Pro is active on this computer.")
             : QObject::tr("Thank you! GlideMix is active on this computer."));
    });
}

void MixHouseLicence::deactivate(Done done)
{
    if (m_key.isEmpty() || m_activationId.isEmpty()) {
        done(true, QString());
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("key"), m_key);
    body.insert(QStringLiteral("organization_id"), kOrganization);
    body.insert(QStringLiteral("activation_id"), m_activationId);
    QNetworkReply* reply = post(QStringLiteral("deactivate"), body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 404: the activation is already gone (freed in the portal): fine.
        if (status != 200 && status != 204 && status != 404) {
            done(false, errorText(reply, data));
            return;
        }
        m_key.clear();
        m_activationId.clear();
        setTier(LicenceTier::Demo);
        save();
        LOGI() << "GlideMix licence: deactivated on this computer";
        done(true, QObject::tr("This computer is free again. GlideMix runs as the demo version."));
    });
}

void MixHouseLicence::validateInBackground()
{
    QJsonObject body;
    body.insert(QStringLiteral("key"), m_key);
    body.insert(QStringLiteral("organization_id"), kOrganization);
    body.insert(QStringLiteral("activation_id"), m_activationId);
    QNetworkReply* reply = post(QStringLiteral("validate"), body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0 || status >= 500) {
            LOGI() << "GlideMix licence: no check possible now (offline?), the licence stays";
            return;
        }
        const QString keyStatus = QJsonDocument::fromJson(data).object().value(QStringLiteral("status")).toString();
        if (status == 200 && (keyStatus.isEmpty() || keyStatus == QStringLiteral("granted"))) {
            muse::settings()->setSharedValue(LICENCE_CHECKED,
                                             muse::Val(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()));
            return;
        }
        // Refunded, revoked, or this computer's activation was freed: demo again.
        LOGW() << "GlideMix licence: no longer valid (" << status << " " << keyStatus << "), back to the demo";
        m_activationId.clear();
        setTier(LicenceTier::Demo);
        save();
    });
}
