/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "au3-components/ParameterAutomationSnapshot.h"

class WaveTrack;

//! Prepare on the main thread, before playback/export, for one effect instance
//! in this track's effect chain. Missing or differently bound instances are not
//! rebound by display name or chain position.
WAVE_TRACK_API ParameterAutomationSnapshot BuildTrackParameterAutomation(
    const WaveTrack& track, const std::string& effectInstanceId, const std::string& pluginId);
