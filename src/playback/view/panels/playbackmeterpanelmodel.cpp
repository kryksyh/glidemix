#include "playbackmeterpanelmodel.h"

#include "playback/playbacktypes.h"

using namespace au::playback;

PlaybackMeterPanelModel::PlaybackMeterPanelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    m_meterModel = new PlaybackMeterModel(this);
}

//! The panel carries the headphone cue while the top bar carries Perform,
//! and the other way round when the position is swapped.
bool PlaybackMeterPanelModel::showsCueOutput() const
{
    return playbackUiState()->playbackMeterPosition()
           == PlaybackMeterPosition::MeterPosition::TopBar;
}

void PlaybackMeterPanelModel::showCueOutput(bool cue)
{
    playbackUiState()->setPlaybackMeterPosition(cue ? PlaybackMeterPosition::MeterPosition::TopBar
                                                : PlaybackMeterPosition::MeterPosition::SideBar);
}

void PlaybackMeterPanelModel::init()
{
    m_meterModel->init();
    emit meterModelChanged();

    //! Mirror of the top-bar meter: this panel shows whichever output the top
    //! bar is not showing. TopBar position = Perform on the top bar, so the cue
    //! lands here; SideBar swaps them.
    const auto apply = [this](const trackedit::audioch_t audioChNum, const audio::MeterSignal& meterSignal) {
        setAudioChannelVolumePressure(audioChNum, meterSignal.peak.pressure);
        setAudioChannelRMS(audioChNum, meterSignal.rms.pressure);
    };

    playback()->audioOutput()->playbackSignalChanges().onReceive(this,
                                                                 [this, apply](const trackedit::audioch_t audioChNum,
                                                                               const audio::MeterSignal& meterSignal) {
        if (showsCueOutput()) {
            apply(audioChNum, meterSignal);
        }
    });

    playback()->audioOutput()->performSignalChanges().onReceive(this,
                                                                [this, apply](const trackedit::audioch_t audioChNum,
                                                                              const audio::MeterSignal& meterSignal) {
        if (!showsCueOutput()) {
            apply(audioChNum, meterSignal);
        }
    });

    //! Clear on swap so the bar drops to silence instead of holding the level
    //! the other output left behind.
    playbackUiState()->playbackMeterPositionChanged().onNotify(this, [this]() {
        setAudioChannelVolumePressure(0, MIN_DISPLAYED_DBFS);
        setAudioChannelVolumePressure(1, MIN_DISPLAYED_DBFS);
        setAudioChannelRMS(0, MIN_DISPLAYED_DBFS);
        setAudioChannelRMS(1, MIN_DISPLAYED_DBFS);
        emit showsCueOutputChanged();
        // The slider now belongs to the other output: show its level.
        m_meterModel->setVolume(showsCueOutput()
                                ? playback()->audioOutput()->cueVolume()
                                : playback()->audioOutput()->playbackVolume());
        emit levelChanged();
    });

    // GlideMix: the slider beside a meter sets the level of the output
    // that meter shows -- the headphone cue here while the top bar carries the
    // audience, and the other way round when the Position setting swaps them.
    const auto readLevel = [this]() {
        m_meterModel->setVolume(showsCueOutput()
                                ? playback()->audioOutput()->cueVolume()
                                : playback()->audioOutput()->playbackVolume());
        emit levelChanged();
    };

    playback()->audioOutput()->playbackVolumeChanged().onReceive(this, [this, readLevel](audio::volume_dbfs_t){
        if (!showsCueOutput()) {
            readLevel();
        }
    });
    playback()->audioOutput()->cueVolumeChanged().onReceive(this, [this, readLevel](audio::volume_dbfs_t){
        if (showsCueOutput()) {
            readLevel();
        }
    });

    readLevel();

    playbackController()->isPlayingChanged().onNotify(this, [this]() {
        emit isPlayingChanged();
    });

    resetAudioChannelsVolumePressure();
}

float PlaybackMeterPanelModel::leftChannelPressure() const
{
    return m_leftChannelPressure;
}

float PlaybackMeterPanelModel::rightChannelPressure() const
{
    return m_rightChannelPressure;
}

float PlaybackMeterPanelModel::level() const
{
    return m_meterModel->volume();
}

bool PlaybackMeterPanelModel::isPlaying() const
{
    return playbackController()->isPlaying();
}

void PlaybackMeterPanelModel::setLeftChannelPressure(float leftChannelPressure)
{
    if (qFuzzyCompare(m_leftChannelPressure, leftChannelPressure)) {
        return;
    }

    m_leftChannelPressure = leftChannelPressure;
    emit leftChannelPressureChanged(m_leftChannelPressure);
}

void PlaybackMeterPanelModel::setRightChannelPressure(float rightChannelPressure)
{
    if (qFuzzyCompare(m_rightChannelPressure, rightChannelPressure)) {
        return;
    }

    m_rightChannelPressure = rightChannelPressure;
    emit rightChannelPressureChanged(m_rightChannelPressure);
}

void PlaybackMeterPanelModel::setAudioChannelVolumePressure(const audio::audioch_t chNum, const float newValue)
{
    float clampedValue = std::clamp(newValue, playback::MIN_DISPLAYED_DBFS, playback::MAX_DISPLAYED_DBFS);
    chNum == 0 ? setLeftChannelPressure(clampedValue) : setRightChannelPressure(clampedValue);
}

void PlaybackMeterPanelModel::setAudioChannelRMS(const audio::audioch_t chNum, const float newValue)
{
    float clampedValue = std::clamp(newValue, playback::MIN_DISPLAYED_DBFS, playback::MAX_DISPLAYED_DBFS);
    chNum == 0 ? setLeftChannelRMS(clampedValue) : setRightChannelRMS(clampedValue);
}

void PlaybackMeterPanelModel::resetAudioChannelsVolumePressure()
{
    setLeftChannelPressure(playback::MIN_DISPLAYED_DBFS);
    setRightChannelPressure(playback::MIN_DISPLAYED_DBFS);
}

float PlaybackMeterPanelModel::leftChannelRMS() const
{
    return m_leftChannelRMS;
}

void PlaybackMeterPanelModel::setLeftChannelRMS(float leftChannelRMS)
{
    if (qFuzzyCompare(m_leftChannelRMS, leftChannelRMS)) {
        return;
    }

    m_leftChannelRMS = leftChannelRMS;
    emit leftChannelRMSChanged(m_leftChannelRMS);
}

float PlaybackMeterPanelModel::rightChannelRMS() const
{
    return m_rightChannelRMS;
}

void PlaybackMeterPanelModel::setRightChannelRMS(float rightChannelRMS)
{
    if (qFuzzyCompare(m_rightChannelRMS, rightChannelRMS)) {
        return;
    }

    m_rightChannelRMS = rightChannelRMS;
    emit rightChannelRMSChanged(m_rightChannelRMS);
}

au::playback::PlaybackMeterModel* PlaybackMeterPanelModel::meterModel() const
{
    return m_meterModel;
}

void PlaybackMeterPanelModel::volumeLevelChangeRequested(float level)
{
    if (qFuzzyCompare(m_meterModel->volume(), level)) {
        return;
    }

    if (showsCueOutput()) {
        playback()->audioOutput()->setCueVolume(level);
    } else {
        playback()->audioOutput()->setPlaybackVolume(level);
    }
}
