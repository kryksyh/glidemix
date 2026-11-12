/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "framework/global/modularity/ioc.h"
#include "projectscene/iprojectsceneconfiguration.h"
#include "trackedit/dom/clip.h"

#include "viewtrackitem.h"

namespace au::projectscene {
class TrackClipItem : public ViewTrackItem
{
    Q_OBJECT

    muse::GlobalInject<IProjectSceneConfiguration> configuration;

    Q_PROPERTY(bool isGrouped READ isGrouped NOTIFY groupIdChanged FINAL)
    Q_PROPERTY(int pitch READ pitch NOTIFY pitchChanged FINAL)
    Q_PROPERTY(int speedPercentage READ speedPercentage NOTIFY speedPercentageChanged FINAL)
    Q_PROPERTY(bool isPitchModified READ isPitchModified NOTIFY pitchChanged FINAL)
    Q_PROPERTY(bool isSpeedModified READ isSpeedModified NOTIFY speedPercentageChanged FINAL)
    //! GlideMix: the track's colour intensity (0..1), see IMixHouseClipColors.
    Q_PROPERTY(double colorIntensity READ colorIntensity NOTIFY colorChanged FINAL)
    //! GlideMix: Disable on the clip menu (drawn faded, plays silent).
    Q_PROPERTY(bool clipDisabled READ clipDisabled NOTIFY contentChanged FINAL)
    // Q_PROPERTY(QObject * envelopeModel READ envelopeModel NOTIFY envelopeModelChanged)

public:
    explicit TrackClipItem(QObject* parent);

    void setClip(const trackedit::Clip& clip);
    //! GlideMix: Colour by key (an invalid colour keeps the clip's own) and intensity (0..255).
    void setMixtapeColors(const QColor& keyColor, int intensity);
    double colorIntensity() const { return m_colorIntensity; }

    bool isGrouped() const;
    bool clipDisabled() const { return m_disabled; }

    int pitch() const;
    int speedPercentage() const;
    bool isPitchModified() const;
    bool isSpeedModified() const;
    double speed() const;

signals:
    void groupIdChanged();
    void pitchChanged();
    void speedPercentageChanged();
    void contentChanged();

private:
    int m_groupId = -1;
    bool m_disabled = false;
    int m_pitch = 0;
    double m_speed = 1.0;
    QColor m_keyColor;
    double m_colorIntensity = 1.0;
    int m_colorIndex = 0;
};
}
