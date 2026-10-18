/*
 * Audacity: A Digital Audio Editor
 */

#include "log.h"

#include "playbackpreferencesmodel.h"

using namespace au::appshell;
using namespace muse::audio;

PlaybackPreferencesModel::PlaybackPreferencesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void PlaybackPreferencesModel::init()
{
    playbackConfiguration()->playbackQualityChanged().onNotify(this, [this](){ emit currentPlaybackQualityChanged(); });
    playbackConfiguration()->ditheringChanged().onNotify(this, [this](){ emit currentDitheringChanged(); });
}

au::playback::PlaybackQualityPrefs::PlaybackQuality PlaybackPreferencesModel::currentPlaybackQuality() const
{
    return playbackConfiguration()->currentPlaybackQuality();
}

QVariantList PlaybackPreferencesModel::playbackQualityList() const
{
    QVariantList result;
    for (const auto& quality: playbackConfiguration()->playbackQualityList()) {
        result << static_cast<int>(quality);
    }

    return result;
}

void PlaybackPreferencesModel::setPlaybackQuality(playback::PlaybackQualityPrefs::PlaybackQuality newQuality)
{
    if (currentPlaybackQuality() == newQuality) {
        return;
    }

    playbackConfiguration()->setPlaybackQuality(newQuality);
}

au::playback::DitherTypePrefs::DitherType PlaybackPreferencesModel::currentDithering() const
{
    return playbackConfiguration()->currentDithering();
}

QVariantList PlaybackPreferencesModel::ditheringList() const
{
    QVariantList result;
    for (const auto& dithering: playbackConfiguration()->ditheringList()) {
        result << static_cast<int>(dithering);
    }

    return result;
}

void PlaybackPreferencesModel::setDithering(playback::DitherTypePrefs::DitherType dithering)
{
    if (currentDithering() == dithering) {
        return;
    }

    playbackConfiguration()->setDithering(dithering);
}




