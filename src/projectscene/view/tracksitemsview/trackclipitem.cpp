/*
* Audacity: A Digital Audio Editor
*/
#include "trackclipitem.h"

#include "framework/global/realfn.h"

#include <algorithm>

using namespace au::projectscene;

TrackClipItem::TrackClipItem(QObject* parent)
    : ViewTrackItem(parent)
{
}

void TrackClipItem::setClip(const trackedit::Clip& clip)
{
    const TrackItemKey key(clip.key);
    const bool keyMoved = !(m_key.key == key.key);
    m_key = key;
    m_title = clip.title;
    m_colorIndex = clip.colorIndex;
    m_color = m_keyColor.isValid() ? m_keyColor : configuration()->clipColor(clip.colorIndex).toQColor();
    m_selectedColor = m_keyColor.isValid() ? m_keyColor.lighter(125)
                      : configuration()->clipSelectedColor(clip.colorIndex).toQColor();
    m_groupId = clip.groupId;
    m_pitch = clip.pitch;
    m_speed = clip.speed;

    if (keyMoved) {
        emit keyChanged();
    }
    emit titleChanged();
    emit pitchChanged();
    emit speedPercentageChanged();
    emit colorChanged();
    emit groupIdChanged();
    emit contentChanged();
    emit timeChanged();
}

void TrackClipItem::setMixtapeColors(const QColor& keyColor, int intensity)
{
    const double newIntensity = std::clamp(intensity, 0, 255) / 255.0;
    if (m_keyColor == keyColor && muse::RealIsEqual(m_colorIntensity, newIntensity)) {
        return;
    }
    m_keyColor = keyColor;
    m_colorIntensity = newIntensity;
    m_color = m_keyColor.isValid() ? m_keyColor : configuration()->clipColor(m_colorIndex).toQColor();
    m_selectedColor = m_keyColor.isValid() ? m_keyColor.lighter(125)
                      : configuration()->clipSelectedColor(m_colorIndex).toQColor();
    emit colorChanged();
}

bool TrackClipItem::isGrouped() const
{
    return m_groupId != -1;
}

int TrackClipItem::pitch() const
{
    return m_pitch;
}

int TrackClipItem::speedPercentage() const
{
    return qRound(100.0 / m_speed);
}

bool TrackClipItem::isPitchModified() const
{
    return m_pitch != 0;
}

bool TrackClipItem::isSpeedModified() const
{
    return !muse::RealIsEqual(m_speed, 1.0);
}

double TrackClipItem::speed() const
{
    return m_speed;
}
