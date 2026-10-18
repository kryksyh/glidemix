/*
 * GlideMix
 */
#include "stemspreferencesmodel.h"

#include "translation.h"

using namespace au::preferences;
using namespace au::mixhouse;

StemsPreferencesModel::StemsPreferencesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void StemsPreferencesModel::subscribe()
{
    if (m_subscribed || !stems()) {
        return;
    }
    m_subscribed = true;
    stems()->installProgress().onReceive(this, [this](const QString& text) {
        m_installStatus = text;
        emit installChanged();
    });
    stems()->installFinished().onReceive(this, [this](bool) {
        emit installChanged();
    });
}

bool StemsPreferencesModel::installing() const
{
    const_cast<StemsPreferencesModel*>(this)->subscribe();
    return stems() && stems()->installing();
}

bool StemsPreferencesModel::engineInHelpers() const
{
    return stems() && stems()->engineInHelpers();
}

void StemsPreferencesModel::installEngine()
{
    subscribe();
    if (stems()) {
        stems()->installEngine();
    }
    emit installChanged();
}

void StemsPreferencesModel::cancelInstall()
{
    if (stems()) {
        stems()->cancelInstall();
    }
}

void StemsPreferencesModel::uninstallEngine()
{
    if (stems()) {
        stems()->uninstallEngine();
    }
    m_installStatus.clear();
    emit installChanged();
}

bool StemsPreferencesModel::available() const
{
    return stems() && stems()->available();
}

int StemsPreferencesModel::device() const
{
    return configuration() ? static_cast<int>(configuration()->stemsDevice()) : 0;
}

void StemsPreferencesModel::setDevice(int device)
{
    if (configuration() && device != this->device()) {
        configuration()->setStemsDevice(static_cast<MixHouseStemsDevice>(device));
        emit changed();
    }
}

bool StemsPreferencesModel::highQuality() const
{
    return configuration() && configuration()->stemsHighQuality();
}

void StemsPreferencesModel::setHighQuality(bool high)
{
    if (configuration() && high != highQuality()) {
        configuration()->setStemsHighQuality(high);
        emit changed();
    }
}

int StemsPreferencesModel::cacheLimitGb() const
{
    return configuration() ? configuration()->stemsCacheLimitGb() : 10;
}

void StemsPreferencesModel::setCacheLimitGb(int gb)
{
    if (configuration() && gb != cacheLimitGb()) {
        configuration()->setStemsCacheLimitGb(gb);
        emit changed();
    }
}

QString StemsPreferencesModel::cacheUsed() const
{
    const double mb = stems() ? stems()->cacheSize() / (1024.0 * 1024.0) : 0.0;
    return mb >= 1024.0 ? muse::qtrc("preferences", "%1 GB used").arg(mb / 1024.0, 0, 'f', 1)
           : muse::qtrc("preferences", "%1 MB used").arg(qRound(mb));
}

void StemsPreferencesModel::clearCache()
{
    if (stems()) {
        stems()->clearCache();
    }
    emit cacheChanged();
}
