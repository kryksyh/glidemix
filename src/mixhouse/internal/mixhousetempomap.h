/*
 * GlideMix
 */
#pragma once

#include <memory>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/imixhousebeatgrids.h"

#include <QTimer>

#include "mixhouse/imixhousetempomap.h"

class BoundedEnvelope;

namespace au::mixhouse {
class MixHouseTempoMap : public IMixHouseTempoMap, public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::GlobalInject<IMixHouseLibraryConfiguration> configuration;
    muse::ContextInject<IMixHouseBeatGrids> beatGrids { this };

public:
    explicit MixHouseTempoMap(const muse::modularity::ContextPtr& iocCtx);
    ~MixHouseTempoMap() override;

    void init();

    Points points() const override;
    void setPoints(const Points& points, bool completed) override;
    void reset() override;

    double bpmAt(double time) const override;
    bool isConstant() const override;
    double secToBeat(double time) const override;
    double beatToSec(double beat) const override;

    muse::async::Notification tempoMapChanged() const override;

    bool followsTracks() const override { return m_followTracks; }
    void setFollowsTracks(bool follow) override;
    std::vector<Handover> handovers() const override;
    void trackPlaced(double stretchRatio) override;
    void nudgeMasterBpm(double deltaBpm, double atTime) override;

private:
    struct TablePoint {
        double sec = 0.0;
        double beats = 0.0;   // beats elapsed at `sec`
        double bpm = 0.0;
        Point shape;   // the segment from here to the next entry
    };

    void onProjectChanged();
    //! Publishes the line as the live playback warp: speed = BPM / project tempo.
    void rebuildPlaybackWarp();
    const BoundedEnvelope* playbackWarp();
    void followProjectTempo(double tempo);
    double projectTempo() const;
    static Points sanitized(Points points);
    std::vector<TablePoint> table() const;

    Points m_points;
    // Stands for the live curve in streams (PlaybackSpeedCurve::SetLive); its
    // own points are unused, only its speed range bounds the resamplers.
    std::unique_ptr<BoundedEnvelope> m_warpToken;
    std::shared_ptr<void> m_warpHook;
    muse::async::Notification m_changed;
    //! Rebuilds the line from the tracks when they changed since the last build.
    void followTracksNow(bool force);

    bool m_followTracks = false;
    bool m_realTempos = false;   ///< after Reset: no 8% rule, every track at its own BPM
    double m_masterFactor = 1.0;
    QString m_trackSignature;
    QTimer m_followTimer;
};
}
