/*
 * GlideMix
 */
#pragma once

#include <vector>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

namespace au::mixhouse {
/// The master tempo line: BPM over project time, edited in the TEMPO lane.
/// Ported from the parked MixHouse DJ app's tempo automation
/// (ArrangementWindow::m_tempoPoints / buildTempoTable / secToBeat /
/// beatToSec), but in absolute project seconds instead of fractions of the
/// arrangement length, so points stay put when the project grows.
///
/// Before the first point the tempo is the first point's; after the last it
/// holds the last point's. A line with one BPM everywhere IS the project tempo:
/// committing a flat line sets it, and a project tempo change moves a flat
/// line with it.
class IMixHouseTempoMap : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IMixHouseTempoMap)

public:
    virtual ~IMixHouseTempoMap() = default;

    static constexpr double kMinBpm = 50.0;    // parked kTempoMin
    static constexpr double kMaxBpm = 300.0;   // parked kTempoMax

    /// How the segment to the next point runs.
    enum class Shape {
        Curve,    ///< bent by `curve` (0 is a straight ramp)
        SCurve,   ///< eases out of the old tempo and into the new one
    };

    struct Point {
        double time = 0.0;   ///< project seconds
        double bpm = 120.0;
        double curve = 0.0;  ///< shape of the segment to the NEXT point, -1..1
        Shape shape = Shape::Curve;
    };
    using Points = std::vector<Point>;

    virtual Points points() const = 0;

    /// `completed` false while a drag is in progress (repaint only), true on
    /// release (and then a flat line becomes the project tempo).
    virtual void setPoints(const Points& points, bool completed) = 0;

    /// Flattens the line to the project tempo (the lane's Reset button).
    virtual void reset() = 0;

    virtual double bpmAt(double time) const = 0;
    virtual bool isConstant() const = 0;

    /// Beats elapsed from project time 0 to `time`, integrating the line, and
    /// its exact inverse.
    virtual double secToBeat(double time) const = 0;
    virtual double beatToSec(double beat) const = 0;

    /// MixMeister's tempo that follows the tracks: each track plays at its own
    /// BPM, and where the next one starts over it the line glides from the
    /// outgoing BPM to the incoming one across the overlap (Beatmix). While on,
    /// the line is rebuilt whenever tracks move, are added or removed.
    virtual bool followsTracks() const = 0;
    virtual void setFollowsTracks(bool follow) = 0;

    /// Tracks this far from the project tempo (8%) play at their own tempo
    /// when the line follows the tracks; nearer ones stay on the project tempo.
    static constexpr double kFarTempo = 0.08;

    /// A track was placed with this stretch ratio (raw tempo -> project tempo).
    /// A far one turns "Follow tracks" on, so it plays at its own tempo and the
    /// line glides into it -- unless the line was drawn by hand.
    virtual void trackPlaced(double stretchRatio) = 0;

    /// MixMeister's Master BPM: moves the whole mix faster or slower by
    /// `deltaBpm` at the playhead's tempo, keeping the line's shape.
    virtual void nudgeMasterBpm(double deltaBpm, double atTime) = 0;

    virtual muse::async::Notification tempoMapChanged() const = 0;

    /// The parked app's segment shape: bpm(t) = bpm0 + (bpm1 - bpm0) * t^k with
    /// k = 2^(-2 * curve); curve 0 is a straight ramp.
    static double curveExponent(double curve);
    static double curveShape(double t, double curve);

    /// 0..1 along the segment that starts at `from`, at fraction `t` of its
    /// length, and the integral of that from 0 to `t` (for counting beats).
    static double segmentShape(double t, const Point& from);
    static double segmentShapeIntegral(double t, const Point& from);
};
}
