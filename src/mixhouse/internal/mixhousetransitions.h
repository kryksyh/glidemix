/*
 * GlideMix
 */
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/iprojecthistory.h"
#include "automation/iclipgaininteraction.h"
#include "effects/effects_base/ieffectchainpresets.h"
#include "trackedit/itrackeditinteraction.h"

#include "mixhouse/imixhousetransitions.h"
#include "mixhouse/mixhousetransitionspec.h"
#include "mixhouse/imixhousetransitionconfiguration.h"
#include "mixhouse/imixhousebeatgrids.h"

namespace au::mixhouse {
class MixHouseTransitions : public IMixHouseTransitions, public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory { this };
    muse::ContextInject<automation::IClipGainInteraction> clipGain { this };
    muse::ContextInject<IMixHouseBeatGrids> beatGrids { this };
    muse::ContextInject<trackedit::ITrackeditInteraction> trackeditInteraction { this };
    muse::GlobalInject<IMixHouseTransitionConfiguration> transitionConfiguration;
    muse::GlobalInject<effects::IMixxxEffectIds> mixxxEffectIds;

public:
    explicit MixHouseTransitions(const muse::modularity::ContextPtr& iocCtx);

    void init();

    std::vector<Zone> zones() const override;
    muse::async::Notification zonesChanged() const override;
    bool applyCrossfade(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming) override;
    bool removeCrossfade(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming) override;
    bool applyPreset(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, int index) override;
    bool applyTransition(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                         const std::string& name) override;
    std::string mixNow(double playhead) override;
    void beginEdit() override;
    void endEdit(const std::string& name) override;
    void applyDefaultTransitions(const trackedit::ClipKeyList& clips) override;
    void followAnchors(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, double oldAnchor,
                       double incomingShift) override;

private:
    //! The zone's OUT and IN anchors meet (a MixMeister link).
    bool anchorsLinked(const Zone& zone) const;
    //! `out`'s OUT anchor sits on `in`'s IN anchor.
    bool outLinkedToIn(const trackedit::ClipKey& out, const trackedit::ClipKey& in) const;
    //! Laid over each other (a song and its stems), not a mix.
    bool isLayer(const trackedit::Clip& a, const trackedit::Clip& b) const;
    struct GainPoint {
        double time = 0.0;
        double value = 1.0;
        float curvature = 0.0f;
        bool eased = false;   ///< an S-curve on to the next point
    };

    double gainAt(const trackedit::ClipKey& clip, double time) const;
    //! applyCrossfade, starting the fade at `fadeStart` when it lies inside the overlap.
    bool crossfadeFrom(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, double fadeStart);
    bool findZone(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, Zone& zone) const;
    //! The transition names saved with the project (mixhousetransitionnames.h).
    void* au3Project() const;
    std::string savedNames() const;
    void restoreSavedNames();
public:
    bool laneLines(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, Lane lane,
                   LaneLine& out, LaneLine& in) const override;
    bool setLanePoints(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, Lane lane,
                       bool outgoingSide, const std::vector<std::pair<double, double> >& points,
                       bool consolidate = false) override;
    bool addEffect(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, const std::string& kind) override;
    std::vector<AddedEffect> addedEffects(const trackedit::ClipKey& outgoing,
                                          const trackedit::ClipKey& incoming) const override;
    bool setEffectAmount(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                         const std::string& kind, double amount) override;
    bool removeEffect(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                      const std::string& kind) override;
    bool setEffectEnabled(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                          const std::string& kind, bool enabled) override;
    bool setTransitionEnabled(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                              bool enabled) override;
private:
    using ZoneKey = std::pair<trackedit::ClipKey, trackedit::ClipKey>;
    //! An ADD FX effect: its lines, where they sit, and how strong.
    struct EffectEntry {
        AddedEffect info;
        std::vector<MixHouseEffectLine> lines;
        double anchor = 0.0;   ///< project seconds of offset 0
        double unit = 1.0;     ///< seconds per offset unit (a beat)
    };
    //! Writes one effect's lines at `amount`: neutral up to its first point,
    //! its points scaled toward neutral, neutral again after its last one.
    bool writeEffectLines(const ZoneKey& zone, const EffectEntry& entry, double amount);
    //! Replaces the clip's volume line over [from, to] with `points`, keeping
    //! whatever lies outside that range.
    bool replaceGain(const trackedit::ClipKey& clip, double from, double to, const std::vector<GainPoint>& points,
                     bool completed);
    //! Writes `preset` onto the two clips with its anchor at `anchor` seconds: the
    //! volume markers, and the bass lines when it has them, as one undo step.
    //! Switches off every effect line a transition can write on `clip`.
    void clearTransitionEffects(const trackedit::ClipKey& clip);
    bool writePreset(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                     const MixHouseTransitionPreset& preset, double anchor);
    //! applyPreset's body, for a preset that is not necessarily in presets().
    bool applyPresetValue(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                          const MixHouseTransitionPreset& preset);

    //! Undo and redo of a Mix Now's anchor move, which history doesn't hold.
    void syncMixNowAnchors(trackedit::HistoryEvent event);
    //! Undo and redo of the transitions' names.
    void syncTransitionNames(trackedit::HistoryEvent event);
    std::map<size_t, std::map<ZoneKey, std::string> > m_namesAtState;

    struct MixNowMove {
        trackedit::ClipKey outgoing;
        trackedit::ClipKey incoming;
        double pulledStart = 0.0;   ///< where the Mix Now put the incoming clip
        std::optional<std::pair<double, double> > undone;   ///< the move, while undone
    };
    std::vector<MixNowMove> m_mixNowMoves;

    muse::async::Notification m_zonesChanged;
    std::map<std::pair<trackedit::ClipKey, trackedit::ClipKey>, std::string> m_applied;
    std::map<ZoneKey, std::vector<EffectEntry> > m_effects;
    //! Switched-off zones, with the transition that was there ("" = a crossfade).
    std::map<ZoneKey, std::string> m_disabled;
    int m_editDepth = 0;   ///< > 0 between beginEdit() and endEdit()

    //! A song and its stems are clips laid over each other on several lanes:
    //! one transition for all of them (FINDINGS F47). `key` first, then the
    //! clips layered with it, top lane first.
    std::vector<trackedit::ClipKey> layersOf(const trackedit::ClipKey& key) const;
    //! Runs `op` for every pair of layers as one undo step named `undoName`
    //! ("" = folded into the step on top). Nothing (nullopt) when neither side
    //! has layers: the caller then just does the plain edit.
    std::optional<bool> forEachLayerPair(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming,
                                         const std::string& undoName,
                                         const std::function<bool(const trackedit::ClipKey&, const trackedit::ClipKey&)>& op);
    bool m_inLayerPairs = false;   ///< inside forEachLayerPair: edit just this pair
    void* m_namesFor = nullptr;   ///< the au3 project whose saved names were read
};
}
