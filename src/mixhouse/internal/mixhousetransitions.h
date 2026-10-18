/*
 * GlideMix
 */
#pragma once

#include <map>
#include <string>
#include <utility>

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

private:
    struct GainPoint {
        double time = 0.0;
        double value = 1.0;
        float curvature = 0.0f;
    };

    double gainAt(const trackedit::ClipKey& clip, double time) const;
    //! applyCrossfade, starting the fade at `fadeStart` when it lies inside the overlap.
    bool crossfadeFrom(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, double fadeStart);
    bool findZone(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, Zone& zone) const;
public:
    bool laneLines(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, Lane lane,
                   LaneLine& out, LaneLine& in) const override;
    bool setLanePoints(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, Lane lane,
                       bool outgoingSide, const std::vector<std::pair<double, double> >& points) override;
    bool addEffect(const trackedit::ClipKey& outgoing, const trackedit::ClipKey& incoming, const std::string& kind) override;
private:
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

    muse::async::Notification m_zonesChanged;
    bool m_effectsOnly = false;   //! writePreset adds effect lines only (addEffect)
    std::map<std::pair<trackedit::ClipKey, trackedit::ClipKey>, std::string> m_applied;
};
}
