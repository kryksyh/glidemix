/*
 * GlideMix
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"
#include "trackedit/trackedittypes.h"

namespace au::mixhouse {
/// BEATMATCH zones: wherever a clip on one track overlaps a clip on another,
/// the parked MixHouse DJ app drew an orange zone with a BEATMATCH pill. Here
/// the pill applies a simple crossfade across that overlap, written into both
/// clips' ordinary volume lines (so it can be edited by hand afterwards).
/// One <marker gain="dB" offset="..."/> of MixMeister Fusion's
/// transitions.xml: `offset` in beats from the anchor when the preset is a
/// Beatmix, else seconds.
struct MixHouseTransitionMarker {
    double offset = 0.0;
    double gainDb = 0.0;
    //! GlideMix: the shape of the line from this marker to the next -- an
    //! S-curve, or bent (the volume line's curvature, -1..1); else straight.
    float curvature = 0.0f;
    bool eased = false;
};

/// One point of a clip's bass line: `offset` in beats from the anchor, `value`
/// 0..1 (1 leaves the bass alone, 0 removes it, as the engine's low band does).
struct MixHouseBassMarker {
    double offset = 0.0;
    double value = 1.0;
};

/// One effect automation line a transition writes (a filter sweep, an echo
/// out ...): a parameter of the DJ effect `mixxxId` on the outgoing or the
/// incoming clip's track, values 0..1 of the parameter's range, offsets in
/// beats from the anchor like the bass markers. The first value holds back to
/// the clip's start and the last one on to its end.
struct MixHouseEffectLine {
    std::string mixxxId;
    std::string parameterId;
    bool outgoing = true;
    std::vector<MixHouseBassMarker> markers;
};

/// A transition preset. The MixMeister ones are transcribed from its
/// transitions.xml (via the parked MixHouse DJ app's transitions.cpp): the
/// volume markers written onto the outgoing clip (outro) and the incoming clip
/// (intro) around the anchor where the two meet. Mixtape's own also move the
/// bass (MixMeister's presets never touch the EQ): a bass swap hands the low
/// end from one track to the other, on the beat, while the volumes cross.
struct MixHouseTransitionPreset {
    std::string name;
    bool beatmix = false;
    std::vector<MixHouseTransitionMarker> outro;
    std::vector<MixHouseTransitionMarker> intro;
    /// Empty = that clip's bass is left as it is. The outgoing line is 1 before
    /// its first marker and holds its last value to the clip's end; the incoming
    /// line holds its first value back to the clip's start and its last one on.
    std::vector<MixHouseBassMarker> bassOutro;
    std::vector<MixHouseBassMarker> bassIntro;
    /// The transition EQ's mids and highs, the same way (GlideMix).
    std::vector<MixHouseBassMarker> midOutro, midIntro, highOutro, highIntro;
    /// Filter sweeps, noise, echo or reverb out (GlideMix's own transitions).
    std::vector<MixHouseEffectLine> effects;
};

class IMixHouseTransitions : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IMixHouseTransitions)

public:
    virtual ~IMixHouseTransitions() = default;

    struct Zone {
        trackedit::ClipKey outgoing;   ///< starts first
        trackedit::ClipKey incoming;   ///< starts later, inside the outgoing clip
        double start = 0.0;            ///< project seconds
        double end = 0.0;
        bool crossfaded = false;
        std::string preset;            ///< name of the transition last applied here ("" = none this session)
        bool disabled = false;         ///< switched off (right-click on its pill): drawn grey
    };

    /// MixMeister's presets (Simple Cut/Crossfade, Beatmix Cut/8/16/32/64 bars)
    /// plus "Crossfade (fit overlap)", the equal-power fade across the whole overlap.
    static const std::vector<MixHouseTransitionPreset>& presets();

    /// Writes preset `index` into both clips' volume lines. The anchor is the
    /// first project bar line at or after the incoming clip's start (the
    /// start itself for seconds-based presets); markers past a clip's edges
    /// are clipped to it. After the transition the outgoing clip stays
    /// silent and the incoming stays at full (MixMeister figures 11/12/21).
    /// One undo step.
    virtual bool applyPreset(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, int index) = 0;

    /// Writes the transition called `name` -- one of Mixtape's built-ins or one
    /// of your own from Preferences > Transitions -- the same way applyPreset
    /// does. False if there is no such transition.
    virtual bool applyTransition(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                 const std::string& name) = 0;

    virtual std::vector<Zone> zones() const = 0;
    virtual muse::async::Notification zonesChanged() const = 0;

    /// Outgoing fades from full to silent across the overlap and stays silent
    /// to its end; incoming is silent from its start and fades up to full.
    /// Equal-power curves. One undo step.
    virtual bool applyCrossfade(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming) = 0;

    /// MixMeister's Mix Now: pulls the next track forward so its Intro
    /// anchor lands on the next bar line after `playhead`, together with
    /// every track after it (the chain stays linked). A crossfade between
    /// the playing track and the pulled one is redone from that bar.
    /// Returns what happened, for the log / status; empty = nothing to do.
    virtual std::string mixNow(double playhead) = 0;
    /// Puts both volume lines back to full over the regions a crossfade owns.
    virtual bool removeCrossfade(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming) = 0;

    /// The lanes the transition pill shows (DropLab's dots).
    enum class Lane {
        Volume,   ///< the clip volume line, 0..1 (full = 1)
        Low,      ///< the bass line, 0..1
        Color,    ///< the Filter DJ effect as one knob: 0.5 open, below low-pass, above high-pass
        Mid,      ///< the transition EQ's mids, 0..1
        High,     ///< its highs, 0..1
    };
    struct LaneLine {
        std::vector<std::pair<double, double> > samples;   ///< (project seconds, 0..1) across the zone
        std::vector<std::pair<double, double> > points;    ///< its breakpoints inside the zone
    };
    /// One lane of both clips across their overlap, for drawing.
    virtual bool laneLines(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, Lane lane,
                           LaneLine& out, LaneLine& in) const = 0;
    /// Edits one lane of one side (the outgoing clip, or the incoming one):
    /// `points` (project seconds, 0..1) replace that lane's breakpoints inside
    /// the overlap; outside it the line is kept. One undo step; with
    /// `consolidate` the edit joins the previous lane edit's step (the writes
    /// of one knob turn, made while it turns).
    virtual bool setLanePoints(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, Lane lane,
                               bool outgoingSide, const std::vector<std::pair<double, double> >& points,
                               bool consolidate = false) = 0;
    /// ADD FX in the transition panel: one effect over the overlap, on top of
    /// what the transition already does. `kind`: filter-out, filter-in, echo,
    /// reverb, riser, downsweep. One undo step.
    virtual bool addEffect(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                           const std::string& kind) = 0;

    /// GlideMix: an effect ADD FX put on this transition. It acts inside the
    /// transition only; outside, its line holds the effect's neutral value.
    struct AddedEffect {
        std::string kind;       ///< addEffect's kind
        std::string name;       ///< for the panel
        double amount = 1.0;    ///< 0..1: how far its line moves from neutral
        bool enabled = true;    ///< its power button: off holds the line at neutral
    };
    virtual std::vector<AddedEffect> addedEffects(const trackedit::ClipKey& outgoing,
                                                  const trackedit::ClipKey& incoming) const = 0;
    /// Rewrites the effect's line at `amount` (0..1). One undo step.
    virtual bool setEffectAmount(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                 const std::string& kind, double amount) = 0;
    /// Its power button: off holds the lines at neutral, on brings them back. One undo step.
    virtual bool setEffectEnabled(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                  const std::string& kind, bool enabled) = 0;
    /// Takes the effect off the transition: its lines go back to neutral. One undo step.
    virtual bool removeEffect(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                              const std::string& kind) = 0;
    /// Off: removes everything the transition wrote (volume fades, EQ, effects)
    /// and keeps the zone grey. On: applies the transition that was there.
    virtual bool setTransitionEnabled(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                      bool enabled) = 0;
};
}
