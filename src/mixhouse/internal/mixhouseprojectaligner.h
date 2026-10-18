/*
 * MixHouse DJ 2
 */
#pragma once

#include <QString>

#include "context/iglobalcontext.h"
#include "trackedit/itrackeditinteraction.h"
#include "trackedit/trackedittypes.h"

namespace au::mixhouse {
enum class MixHouseBarPlacement {
    NextBarAtPlayhead,   ///< loaded from the library: first bar line at/after the playhead
    NearestBar,          ///< dropped on the timeline: bar line nearest to where it landed
};

/// A library track that was just imported into the project, and what the
/// library knows about its beats.
struct MixHouseAlignRequest {
    trackedit::TrackIdList importedTracks;   ///< every clip on these tracks, unless `clips` is set
    trackedit::ClipKeyList clips;            ///< exactly these clips (a drop into a track that had others)
    MixHouseBarPlacement placement = MixHouseBarPlacement::NextBarAtPlayhead;
    double bpm = 0.0;             ///< library.bpm (Queen Mary grid)
    double firstBeatSec = 0.0;    ///< the grid's own first beat
    double downbeatSec = -1.0;    ///< Beat This! downbeat (mixhouse_downbeats), -1 = none
    double downbeatConfidence = 0.0;
    bool projectWasEmpty = false; ///< no audio content BEFORE this import
    bool lockToBars = true;       ///< SYNC: place the downbeat on a bar; off keeps the placement (tempo still follows)
    int snapBars = 1;             ///< bar 1 lands on a bar index that is a multiple of this (phrase snap)
};

struct MixHouseAlignResult {
    bool aligned = false;
    bool setProjectTempo = false;
    double projectTempo = 0.0;
    double stretchRatio = 1.0;
    double barSec = 0.0;          ///< project time the downbeat now sits on
    bool usedDetectedDownbeat = false;
    QString reason;               ///< why nothing was aligned, when aligned == false
};

/// Tempo a track should be stretched to: its BPM as-is, doubled or halved --
/// whichever needs the smallest stretch to reach `projectBpm`, so an 87 BPM
/// track in a 174 BPM set plays at its own speed on every second beat instead
/// of twice as fast (the parked MixHouse DJ app's disambiguateOctave()).
double compatibleTrackBpm(double trackBpm, double projectBpm);

/// the rules (2026-09-13): the first track in an empty project sets the
/// project tempo to its BPM (no stretching); every later track is
/// time-stretched to the project tempo (Audacity 4's own clip raw-tempo
/// mechanism, the same calls TempoDetection makes for loops); then the
/// track's downbeat is placed on the first bar line at or after the playhead,
/// with any intro hanging to the left of that bar (never cut, never before 0).
MixHouseAlignResult alignImportedTrackToBars(const std::shared_ptr<context::IGlobalContext>& globalContext,
                                             const std::shared_ptr<trackedit::ITrackeditInteraction>& trackeditInteraction,
                                             const MixHouseAlignRequest& request);
}
