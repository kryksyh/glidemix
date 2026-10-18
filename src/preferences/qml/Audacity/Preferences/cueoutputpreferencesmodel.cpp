/*
 * GlideMix
 */
#include "cueoutputpreferencesmodel.h"

#include "translation.h"

using namespace au::preferences;

CueOutputPreferencesModel::CueOutputPreferencesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void CueOutputPreferencesModel::init()
{
    if (performance()) {
        performance()->performanceStateChanged().onNotify(this, [this]() {
            emit changed();
        });
    }
    emit changed();
}

std::vector<std::string> CueOutputPreferencesModel::devices() const
{
    std::vector<std::string> result { std::string() };
    if (performance()) {
        for (const std::string& device : performance()->outputDevices()) {
            result.push_back(device);
        }
    }
    return result;
}

QStringList CueOutputPreferencesModel::outputs() const
{
    QStringList result;
    result << muse::qtrc("preferences", "Outputs 3-4 of the playback device");

    if (performance()) {
        for (const std::string& device : performance()->outputDevices()) {
            result << QString::fromStdString(device);
        }
    }
    return result;
}

int CueOutputPreferencesModel::currentIndex() const
{
    if (!performance()) {
        return 0;
    }

    const std::string current = performance()->headphonesDevice();
    const std::vector<std::string> all = devices();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i] == current) {
            return static_cast<int>(i);
        }
    }

    //! A device that has been unplugged since it was chosen: fall back to the
    //! outputs, which is what the engine does too.
    return 0;
}

void CueOutputPreferencesModel::setCurrentIndex(int index)
{
    if (!performance()) {
        return;
    }

    const std::vector<std::string> all = devices();
    if (index < 0 || index >= static_cast<int>(all.size())) {
        return;
    }
    if (all[index] == performance()->headphonesDevice()) {
        return;
    }

    performance()->setHeadphonesDevice(all[index]);
    emit changed();
}
