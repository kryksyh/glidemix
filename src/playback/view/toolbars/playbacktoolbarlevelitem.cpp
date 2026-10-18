/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "playbacktoolbarlevelitem.h"

#include <QVariantMap>

#include "iaudiooutput.h"

using namespace au::playback;
using namespace au::audio;

PlaybackToolBarLevelItem::PlaybackToolBarLevelItem(const muse::ui::UiAction& action, muse::uicomponents::ToolBarItemType::Type type,
                                                   QObject* parent)
    : muse::uicomponents::ToolBarItem(action, type, parent)
{
    // GlideMix: the slider sets the level of the output this meter
    // shows -- the audience's, or the headphone cue's when the Position
    // setting has put the cue on the top bar.
    playback()->audioOutput()->playbackVolumeChanged().onReceive(this, [this](audio::volume_dbfs_t volume){
        if (showsPerformOutput()) {
            m_level = volume;
            emit levelChanged();
        }
    });
    playback()->audioOutput()->cueVolumeChanged().onReceive(this, [this](audio::volume_dbfs_t volume){
        if (!showsPerformOutput()) {
            m_level = volume;
            emit levelChanged();
        }
    });

    //! Perform (audience) and Cue (headphones) are metered by two separate
    //! senders, so performing and cueing at once read independently instead of
    //! summing. Both are subscribed here and the meter position decides which
    //! one this top-bar meter shows: TopBar puts Perform here (and the cue in
    //! the side panel), SideBar swaps them.
    const auto apply = [this](const audioch_t audioChNum, const audio::MeterSignal& meterSignal) {
        setAudioChannelVolumePressure(audioChNum, meterSignal.peak.pressure);
        setAudioChannelRMS(audioChNum, meterSignal.rms.pressure);
    };

    playback()->audioOutput()->performSignalChanges().onReceive(this,
                                                                [this, apply](const audioch_t audioChNum,
                                                                              const audio::MeterSignal& meterSignal) {
        if (showsPerformOutput()) {
            apply(audioChNum, meterSignal);
        }
    });

    playback()->audioOutput()->playbackSignalChanges().onReceive(this,
                                                                 [this, apply](const audioch_t audioChNum,
                                                                               const audio::MeterSignal& meterSignal) {
        if (!showsPerformOutput()) {
            apply(audioChNum, meterSignal);
        }
    });

    //! Swapping the outputs leaves the bar sitting at the old signal's level:
    //! clear it so it falls back to silence rather than a frozen reading.
    playbackUiState()->playbackMeterPositionChanged().onNotify(this, [this]() {
        setAudioChannelVolumePressure(0, MIN_DISPLAYED_DBFS);
        setAudioChannelVolumePressure(1, MIN_DISPLAYED_DBFS);
        setAudioChannelRMS(0, MIN_DISPLAYED_DBFS);
        setAudioChannelRMS(1, MIN_DISPLAYED_DBFS);
        emit showsPerformOutputChanged();
        // The slider now belongs to the other output: show its level.
        m_level = showsPerformOutput() ? playback()->audioOutput()->playbackVolume()
                  : playback()->audioOutput()->cueVolume();
        emit levelChanged();
    });

    m_level = showsPerformOutput() ? playback()->audioOutput()->playbackVolume()
              : playback()->audioOutput()->cueVolume();
    emit levelChanged();

    configuration()->playbackHorizontalMeterSizeChanged().onNotify(this, [this]() {
        emit meterSizeChanged();
    });

    playbackController()->isPlayingChanged().onNotify(this, [this]() {
        emit isPlayingChanged();
    });

    resetAudioChannelsVolumePressure();
}

float PlaybackToolBarLevelItem::level() const
{
    return m_level;
}

void PlaybackToolBarLevelItem::setLevel(float newLevel)
{
    if (m_level == newLevel) {
        return;
    }

    if (showsPerformOutput()) {
        playback()->audioOutput()->setPlaybackVolume(newLevel);
    } else {
        playback()->audioOutput()->setCueVolume(newLevel);
    }
}

//! TopBar position = Perform on the top bar and the cue in the side panel;
//! SideBar swaps them, so the top bar then carries the headphone cue.
bool PlaybackToolBarLevelItem::showsPerformOutput() const
{
    return playbackUiState()->playbackMeterPosition()
           == PlaybackMeterPosition::MeterPosition::TopBar;
}

float PlaybackToolBarLevelItem::leftChannelPressure() const
{
    return m_leftChannelPressure;
}

float PlaybackToolBarLevelItem::rightChannelPressure() const
{
    return m_rightChannelPressure;
}

bool PlaybackToolBarLevelItem::isPlaying() const
{
    return playbackController()->isPlaying();
}

void PlaybackToolBarLevelItem::setLeftChannelPressure(float leftChannelPressure)
{
    if (qFuzzyCompare(m_leftChannelPressure, leftChannelPressure)) {
        return;
    }

    m_leftChannelPressure = leftChannelPressure;
    emit leftChannelPressureChanged(m_leftChannelPressure);
}

void PlaybackToolBarLevelItem::setRightChannelPressure(float rightChannelPressure)
{
    if (qFuzzyCompare(m_rightChannelPressure, rightChannelPressure)) {
        return;
    }

    m_rightChannelPressure = rightChannelPressure;
    emit rightChannelPressureChanged(m_rightChannelPressure);
}

void PlaybackToolBarLevelItem::setAudioChannelVolumePressure(const audio::audioch_t chNum, const float newValue)
{
    float clampedValue = std::clamp(newValue, MIN_DISPLAYED_DBFS, MAX_DISPLAYED_DBFS);
    chNum == 0 ? setLeftChannelPressure(clampedValue) : setRightChannelPressure(clampedValue);
}

void PlaybackToolBarLevelItem::setAudioChannelRMS(const audio::audioch_t chNum, const float newValue)
{
    float clampedValue = std::clamp(newValue, MIN_DISPLAYED_DBFS, MAX_DISPLAYED_DBFS);
    chNum == 0 ? setLeftChannelRMS(clampedValue) : setRightChannelRMS(clampedValue);
}

void PlaybackToolBarLevelItem::resetAudioChannelsVolumePressure()
{
    setLeftChannelPressure(MIN_DISPLAYED_DBFS);
    setRightChannelPressure(MIN_DISPLAYED_DBFS);
}

float PlaybackToolBarLevelItem::leftRecentPeak() const
{
    return m_leftRecentPeak;
}

void PlaybackToolBarLevelItem::setLeftRecentPeak(float newLeftRecentPeak)
{
    if (qFuzzyCompare(m_leftRecentPeak, newLeftRecentPeak)) {
        return;
    }

    m_leftRecentPeak = newLeftRecentPeak;
    emit leftRecentPeakChanged();
}

float PlaybackToolBarLevelItem::leftMaxPeak() const
{
    return m_leftMaxPeak;
}

void PlaybackToolBarLevelItem::setLeftMaxPeak(float newLeftMaxPeak)
{
    if (qFuzzyCompare(m_leftMaxPeak, newLeftMaxPeak)) {
        return;
    }

    m_leftMaxPeak = newLeftMaxPeak;
    emit leftMaxPeakChanged();
}

float PlaybackToolBarLevelItem::rightRecentPeak() const
{
    return m_rightRecentPeak;
}

void PlaybackToolBarLevelItem::setRightRecentPeak(float newRightRecentPeak)
{
    if (qFuzzyCompare(m_rightRecentPeak, newRightRecentPeak)) {
        return;
    }

    m_rightRecentPeak = newRightRecentPeak;
    emit rightRecentPeakChanged();
}

float PlaybackToolBarLevelItem::rightMaxPeak() const
{
    return m_rightMaxPeak;
}

void PlaybackToolBarLevelItem::setRightMaxPeak(float newRightMaxPeak)
{
    if (qFuzzyCompare(m_rightMaxPeak, newRightMaxPeak)) {
        return;
    }

    m_rightMaxPeak = newRightMaxPeak;
    emit rightMaxPeakChanged();
}

float PlaybackToolBarLevelItem::leftChannelRMS() const
{
    return m_leftChannelRMS;
}

void PlaybackToolBarLevelItem::setLeftChannelRMS(float leftChannelRMS)
{
    if (qFuzzyCompare(m_leftChannelRMS, leftChannelRMS)) {
        return;
    }

    m_leftChannelRMS = leftChannelRMS;
    emit leftChannelRMSChanged(m_leftChannelRMS);
}

float PlaybackToolBarLevelItem::rightChannelRMS() const
{
    return m_rightChannelRMS;
}

void PlaybackToolBarLevelItem::setRightChannelRMS(float rightChannelRMS)
{
    if (qFuzzyCompare(m_rightChannelRMS, rightChannelRMS)) {
        return;
    }

    m_rightChannelRMS = rightChannelRMS;
    emit rightChannelRMSChanged(m_rightChannelRMS);
}

void PlaybackToolBarLevelItem::setMeterSize(int size)
{
    if (meterSize() == size) {
        return;
    }

    configuration()->setPlaybackHorizontalMeterSize(size);
}

int PlaybackToolBarLevelItem::meterSize() const
{
    return configuration()->playbackHorizontalMeterSize();
}
