/*
 * GlideMix
 */
#include "arrangementpreferencesmodel.h"

using namespace au::preferences;

ArrangementPreferencesModel::ArrangementPreferencesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

int ArrangementPreferencesModel::waveformStyle() const
{
    return uiState() && uiState()->clipWaveformRgb() ? 1 : 0;
}

void ArrangementPreferencesModel::setWaveformStyle(int style)
{
    if (!uiState() || style == waveformStyle()) {
        return;
    }
    uiState()->setClipWaveformRgb(style == 1);
    emit changed();
}

int ArrangementPreferencesModel::laneBackground() const
{
    return uiState() ? uiState()->laneBackground() : 0;
}

void ArrangementPreferencesModel::setLaneBackground(int mode)
{
    if (!uiState() || mode == laneBackground()) {
        return;
    }
    uiState()->setLaneBackground(mode);
    emit changed();
}

bool ArrangementPreferencesModel::beatGridVisible() const
{
    return !uiState() || uiState()->gridLinesVisible();
}

void ArrangementPreferencesModel::setBeatGridVisible(bool visible)
{
    if (!uiState() || visible == beatGridVisible()) {
        return;
    }
    uiState()->setGridLinesVisible(visible);
    emit changed();
}

bool ArrangementPreferencesModel::stereoWaveform() const
{
    return uiState() && uiState()->clipWaveformStereo();
}

void ArrangementPreferencesModel::setStereoWaveform(bool stereo)
{
    if (!uiState() || stereo == stereoWaveform()) {
        return;
    }
    uiState()->setClipWaveformStereo(stereo);
    emit changed();
}

int ArrangementPreferencesModel::trackView() const
{
    return uiState() ? uiState()->allTracksView() : -1;
}

void ArrangementPreferencesModel::setTrackView(int view)
{
    static const char* const codes[] = {
        "track-view-all-waveform", "track-view-all-single-wave", "track-view-all-half-wave",
        "track-view-all-spectrogram", "track-view-all-multi",
    };
    if (view < 0 || view > 4 || view == trackView()) {
        return;
    }
    if (uiState()) {
        uiState()->setAllTracksView(view);   // kept with no project open too
    }
    // The View menu's own action changes the open project's tracks.
    if (dispatcher()) {
        dispatcher()->dispatch(codes[view]);
    }
    emit changed();
}
