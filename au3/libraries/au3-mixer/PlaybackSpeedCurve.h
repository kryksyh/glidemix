/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  GlideMix

  PlaybackSpeedCurve.h

  The tempo lane's playback time warp: speed as linear knots over track
  time. It is installed "live" behind a token envelope given to streams
  through MixerOptions::Warp::DefaultWarp. Code that would read that
  envelope (the playback schedule, mixer resampling, automation warp) reads
  the current curve instead, so an edit is heard while playing; clip
  segments read it to shift pitch back, so the key holds.

**********************************************************************/
#pragma once

#include <memory>
#include <utility>
#include <vector>

class BoundedEnvelope;

class MIXER_API PlaybackSpeedCurve final
{
public:
    //! (track time, speed) knots sorted by time, speeds > 0; the first and
    //! last speeds hold before and after them.
    std::vector<std::pair<double, double> > knots;

    double SpeedAt(double trackTime) const;
    //! Real seconds to play track times t0..t1 (negative when t1 < t0).
    double IntegralOfInverse(double t0, double t1) const;
    //! Track time reached after `realSeconds` (signed) of playback from t0.
    double SolveIntegralOfInverse(double t0, double realSeconds) const;

    //! The live curve and the envelope that stands for it. Thread-safe:
    //! written on the main thread, read by audio threads.
    static void SetLive(const BoundedEnvelope* token, std::shared_ptr<const PlaybackSpeedCurve> curve);
    //! The live curve when `envelope` is its token, else null.
    static std::shared_ptr<const PlaybackSpeedCurve> LiveFor(const BoundedEnvelope* envelope);
    //! The live curve, whatever the stream (clip segments don't know it).
    static std::shared_ptr<const PlaybackSpeedCurve> Live();

    //! The project tempo the curve's speeds are relative to (0 = unknown).
    //! Tempo-synced effects read it to count beats.
    static void SetProjectTempo(double bpm);
    static double ProjectTempo();
    //! GlideMix: timeline time of bar 1 ("Set bar 1 here"); beats fall
    //! every 60 / ProjectTempo() timeline seconds from it.
    static void SetBeatOrigin(double time);
    static double BeatOrigin();

    //! GlideMix: what a host tells VST3 / Audio Unit plug-ins about the
    //! music at timeline time `t`: the tempo heard there (project tempo times
    //! the tempo lane's speed) and quarter-note beats counted from bar 1, the
    //! way the DJ effects count them. False while the project tempo is unknown.
    //! Tempo-synced plug-ins (loopers, tape stops, gates) do nothing without it.
    struct MusicalTime {
        double bpm = 0.0;
        double beats = 0.0;
    };
    static bool MusicalTimeAt(double timelineTime, MusicalTime& out);
};
