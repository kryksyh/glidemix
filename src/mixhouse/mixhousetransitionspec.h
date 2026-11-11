/*
 * GlideMix
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "imixhousetransitions.h"

namespace au::mixhouse {
/// How one parameter (the volume, the bass) moves across a transition. The
/// same vocabulary DJ.Studio's transition editor uses, because it is the one DJs
/// already know.
enum class MixHouseCurveKind {
    None,        ///< the parameter is not touched
    Crossfade,   ///< the outgoing track's value falls as the incoming one rises (equal power)
    FadeIn,      ///< only the incoming track's value rises
    FadeOut,     ///< only the outgoing track's value falls
    Swap,        ///< the two trade places at one moment, the crossing point
};

/// How a crossfade or fade moves between its ends. Equal power keeps the
/// level steady through a crossfade; the others trade that for a feel.
enum class MixHouseFadeShape {
    EqualPower,   ///< the default: no dip in the middle of a crossfade
    Linear,       ///< a straight line
    SCurve,       ///< gentle at both ends, quickest in the middle
    Early,        ///< most of the change happens at the start
    Late,         ///< holds, then most of the change happens at the end
};

struct MixHouseTransitionLane {
    MixHouseCurveKind curve = MixHouseCurveKind::None;
    /// Crossfade, fade in and fade out: the shape of the fade.
    MixHouseFadeShape shape = MixHouseFadeShape::EqualPower;
    /// How far the value moves: 1 goes all the way (silence, or the bass fully
    /// out), 0.5 halfway, 0 not at all.
    double strength = 1.0;
    /// Swap only: where the two trade places, 0..1 of the transition length.
    double crossing = 0.5;
    /// Swap only: beats after the crossing during which the incoming track's
    /// value is still held back, so neither track has the low end (or volume)
    /// for that long. DJ.Studio's "leave out one, two or four beats".
    double leaveOutBeats = 0.0;
};

/// DJ.Studio's Filter lane: a filter sweep across the transition, on the
/// track going out (it thins out, or goes dull) or the one coming in (it
/// opens up from thin, or from muffled). Played by the track's Filter effect.
enum class MixHouseFilterSweep {
    None,
    OutHighPass,
    OutLowPass,
    InHighPass,
    InLowPass,
};

/// DJ.Studio's Noise lane: white noise rising over the outgoing track up to
/// the drop, or falling away over the incoming track after it.
enum class MixHouseNoiseSweep {
    None,
    Riser,
    Downsweep,
};

/// DJ.Studio's Effect out lane: the outgoing track's last bar goes into an
/// echo or a reverb that rings on after it is gone.
enum class MixHouseEffectOut {
    None,
    Echo,
    Reverb,
};

/// What the transition builder edits: a length and one lane per parameter.
struct MixHouseTransitionSpec {
    std::string name;
    /// Length of the overlap in beats (4 to a bar); the anchor is the incoming
    /// track's mix point on a bar line.
    double lengthBeats = 8.0;
    MixHouseTransitionLane volume;
    MixHouseTransitionLane bass;
    MixHouseTransitionLane mid;    ///< the transition EQ's mids (GlideMix), like bass
    MixHouseTransitionLane high;   ///< and highs
    MixHouseFilterSweep filter = MixHouseFilterSweep::None;
    MixHouseNoiseSweep noise = MixHouseNoiseSweep::None;
    MixHouseEffectOut effectOut = MixHouseEffectOut::None;
    bool builtIn = false;
    /// GlideMix's blend (Chris, 2026-11-01): the incoming song fades in before
    /// the anchor and is at full volume on it, the outgoing one is full up to
    /// the anchor and fades out after it, and the bass changes hands exactly
    /// there. Off: both fade over the same bars after the anchor.
    bool blendAroundAnchor = false;
    /// MixMeister's Beatmix ending (Chris, 2026-11-11): at the end of the fade
    /// the outgoing song drops to silence (a quarter beat), and the incoming
    /// one is silent until the anchor. Without it, a fade that goes only part
    /// of the way (strength below 100 %) would leave the outgoing song playing
    /// on quietly to its end.
    bool cutAtEnd = false;
};

/// Turns a spec into the concrete markers the transition code writes onto the
/// clips. Volume markers are in dB (the clip volume line's own unit, -50 = silent),
/// bass markers 0..1; all offsets are beats from the anchor.
MixHouseTransitionPreset presetFromSpec(const MixHouseTransitionSpec& spec);

/// The transitions that ship with GlideMix, first among them the one Mix
/// Now uses by default: a quick crossfade with a bass swap on the beat.
const std::vector<MixHouseTransitionSpec>& builtInTransitionSpecs();

/// Every DJ effect parameter a transition lane can automate, with the knob
/// position (0..1) at which it does nothing, so a new transition or removing
/// one can put back what an earlier one wrote.
struct MixHouseTransitionEffectParameter {
    std::string mixxxId;
    std::string parameterId;
    double neutral = 0.0;
};
const std::vector<MixHouseTransitionEffectParameter>& transitionEffectParameters();

/// Empty when the spec can be saved, else what is wrong with it.
std::string validateTransitionSpec(const MixHouseTransitionSpec& spec);

/// The name Mix Now uses when nothing else is chosen.
inline constexpr const char* kDefaultMixNowTransition = "Quick crossfade + bass swap";
//! MixMeister's own fade, the one everybody asked for: the playing song is
//! full up to its OUT anchor, slides to half volume (-6 dB) over 8 bars and
//! then cuts; the next song starts at half volume on that anchor and rises to
//! full over the same 8 bars (MixMeister's Beatmix 8 bars).
inline constexpr const char* kVolumeFadeTransition = "Volume Fade";
//! GlideMix's own way: the S-curve crossfade with a bass swap on the beat.
inline constexpr const char* kGlideMixTransition = kDefaultMixNowTransition;
//! What a new pair of songs gets, and Mix Now uses, until the user picks
//! another. Chris (2026-11-11): MixMeister's Volume Fade (it was GlideMix's
//! quick crossfade + bass swap since 2026-11-01).
inline constexpr const char* kDefaultTransition = kVolumeFadeTransition;
}
