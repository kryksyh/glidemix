/*
 * GlideMix: the licence window's model.
 */
#include "mixhouselicencemodel.h"

#include <QDesktopServices>
#include <QUrl>

using namespace au::mixhouse;

MixHouseLicenceModel::MixHouseLicenceModel(QObject* parent)
    : QObject(parent)
{
    if (licence()) {
        licence()->tierChanged().onNotify(this, [this] { emit changed(); });
    }
}

bool MixHouseLicenceModel::isDemo() const
{
    return !licence() || licence()->tier() == LicenceTier::Demo;
}

QString MixHouseLicenceModel::tierName() const
{
    if (!licence()) {
        return tr("Demo version");
    }
    switch (licence()->tier()) {
    case LicenceTier::Pro: return QStringLiteral("GlideMix Pro");
    case LicenceTier::Standard: return QStringLiteral("GlideMix");
    case LicenceTier::Demo: break;
    }
    return tr("Demo version");
}

QString MixHouseLicenceModel::key() const
{
    return licence() ? licence()->key() : QString();
}

void MixHouseLicenceModel::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void MixHouseLicenceModel::setMessage(const QString& text, bool error)
{
    m_message = text;
    m_messageIsError = error;
    emit messageChanged();
}

void MixHouseLicenceModel::activate(const QString& key)
{
    if (!licence() || m_busy) {
        return;
    }
    setBusy(true);
    setMessage(tr("Checking the licence key…"), false);
    licence()->activate(key, [this](bool ok, const QString& text) {
        setBusy(false);
        setMessage(text, !ok);
        emit changed();
        if (ok) {
            emit activated();
        }
    });
}

void MixHouseLicenceModel::deactivate()
{
    if (!licence() || m_busy) {
        return;
    }
    setBusy(true);
    licence()->deactivate([this](bool ok, const QString& text) {
        setBusy(false);
        setMessage(text, !ok);
        emit changed();
    });
}

void MixHouseLicenceModel::buy(bool pro)
{
    if (licence()) {
        QDesktopServices::openUrl(QUrl(licence()->buyUrl(pro ? LicenceTier::Pro : LicenceTier::Standard)));
    }
}
