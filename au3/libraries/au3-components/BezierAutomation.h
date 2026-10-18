/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace clipautomation {
// Strength of the quadratic bulge a curvature of +/-1 produces, as a
// multiple of the plain (pre-2026-09-13) formula. 1.0 is the exact critical
// value: for ANY valid start/end in [0,1] and curvature in [-1,1], the
// resulting curve never leaves [0,1] -- provable by maximizing
// start + u*Delta + k*curvature*|Delta|*u*(1-u) over u in [0,1] for the
// worst case start=0, end=1: the max touches exactly 1.0 only at k=1
// (the maximum solves to (1+k)^2/(4k), which equals 1 only when k=1).
// Chris found k=1's curves ("bends just a little bit") too subtle after
// using this for real automation work. 1.5 was chosen as a deliberate,
// bounded step up: even in that same worst-case point placement, the curve
// can only overshoot the valid range by (k-1)^2/(4k) = 0.25/6 ~= 4.2%, a
// small, rare-to-hit edge case (both endpoints already at the extremes AND
// full curvature) rather than an open-ended "make it stronger" change.
constexpr float kCurvatureStrength = 1.5f;

struct Point {
    double timeOffset{};
    double value{}; // Normalized parameter value, not audio amplitude.
};

struct Segment {
    Point startPoint;
    Point endPoint;
    float curvature{};

    // Time is linear in the Bezier parameter. No inverse polynomial is needed.
    // Positive curvature bulges above the straight line in parameter space.
    // Must apply the SAME kCurvatureStrength as Evaluate() below -- a
    // quadratic Bezier with this control point traces EXACTLY the Evaluate()
    // curve pointwise (verified algebraically), which is what keeps the
    // rendered line and the real evaluated/played value in agreement.
    Point ControlPoint() const noexcept
    {
        return {(startPoint.timeOffset + endPoint.timeOffset) * .5,
                (startPoint.value + endPoint.value) * .5
                    + kCurvatureStrength * curvature * std::abs(endPoint.value - startPoint.value) * .5};
    }

    double Evaluate(double time) const noexcept
    {
        const double duration = endPoint.timeOffset - startPoint.timeOffset;
        if (duration <= 0) { return endPoint.value; } // right limit of a step
        const double u = std::clamp((time - startPoint.timeOffset) / duration, 0.0, 1.0);
        const double value = startPoint.value + u * (endPoint.value - startPoint.value)
            + kCurvatureStrength * curvature * std::abs(endPoint.value - startPoint.value) * u * (1 - u);
        // Defensive backstop for the rare extreme-placement overshoot noted
        // above (this type's own contract is a normalized value, see Point).
        return std::clamp(value, 0.0, 1.0);
    }

    // Exact restriction of a quadratic, retaining its shape after a split/trim.
    Segment Slice(double from, double to) const noexcept
    {
        from = std::clamp(from, startPoint.timeOffset, endPoint.timeOffset);
        to = std::clamp(to, from, endPoint.timeOffset);
        Segment result{{from, Evaluate(from)}, {to, Evaluate(to)}, 0};
        const double duration = endPoint.timeOffset - startPoint.timeOffset;
        const double delta = std::abs(result.endPoint.value - result.startPoint.value);
        if (duration > 0 && delta > 0) {
            const double ratio = (to - from) / duration;
            result.curvature = static_cast<float>(std::clamp(
                curvature * std::abs(endPoint.value - startPoint.value) * ratio * ratio / delta, -1.0, 1.0));
        }
        return result;
    }
};

// Mutable only on the control/UI thread. Publish an independently owned, const
// snapshot to a processor; never mutate a vector being read by that processor.
class Curve {
public:
    std::vector<Segment> segments;

    bool Valid() const noexcept
    {
        const auto pointValid = [](Point p) {
            return std::isfinite(p.timeOffset) && p.timeOffset >= 0
                && std::isfinite(p.value) && p.value >= 0 && p.value <= 1;
        };
        for (size_t i = 0; i < segments.size(); ++i) {
            const auto& s = segments[i];
            if (!pointValid(s.startPoint) || !pointValid(s.endPoint)
                || s.startPoint.timeOffset > s.endPoint.timeOffset
                || !std::isfinite(s.curvature) || std::abs(s.curvature) > 1
                || (i && (s.startPoint.timeOffset != segments[i - 1].endPoint.timeOffset
                          || s.startPoint.value != segments[i - 1].endPoint.value))) {
                return false;
            }
        }
        return true;
    }

    double Evaluate(double time, double fallback = 0) const noexcept
    {
        if (segments.empty() || !std::isfinite(time)) { return fallback; }
        const auto it = std::upper_bound(segments.begin(), segments.end(), time,
            [](double t, const Segment& s) { return t < s.endPoint.timeOffset; });
        return it == segments.end() ? segments.back().endPoint.value : it->Evaluate(time);
    }

    Curve Slice(double from, double to, bool rebase = false) const
    {
        if (!std::isfinite(from) || !std::isfinite(to) || from < 0 || to < from) {
            throw std::invalid_argument("Invalid automation slice");
        }
        Curve result;
        for (const auto& s : segments) {
            if (s.endPoint.timeOffset <= from || s.startPoint.timeOffset >= to) { continue; }
            auto part = s.Slice(from, to);
            if (rebase) {
                part.startPoint.timeOffset -= from;
                part.endPoint.timeOffset -= from;
            }
            result.segments.push_back(part);
        }
        return result;
    }

    void Stretch(double ratio)
    {
        if (!std::isfinite(ratio) || ratio <= 0) { throw std::invalid_argument("Invalid automation stretch"); }
        for (auto& s : segments) {
            s.startPoint.timeOffset *= ratio;
            s.endPoint.timeOffset *= ratio;
        }
    }

    // Replace [from,to] with a source curve rebased at from. An empty source
    // collapses the range; a flat source inserts/holds a gap. UI thread only.
    void Splice(double from, double to, const Curve& source, double sourceLength)
    {
        if (segments.empty() || !source.Valid() || !std::isfinite(sourceLength) || sourceLength < 0
            || !std::isfinite(from) || !std::isfinite(to) || from < 0 || to < from
            || to > segments.back().endPoint.timeOffset) {
            throw std::invalid_argument("Invalid automation splice");
        }
        auto left = Slice(segments.front().startPoint.timeOffset, from);
        auto right = Slice(to, segments.back().endPoint.timeOffset);
        auto append = [&](Segment s) {
            if (!left.segments.empty()) {
                const auto last = left.segments.back().endPoint;
                if (last.timeOffset != s.startPoint.timeOffset || last.value != s.startPoint.value) {
                    left.segments.push_back({last, s.startPoint, 0});
                }
            }
            left.segments.push_back(s);
        };
        for (auto s : source.segments) {
            s.startPoint.timeOffset += from;
            s.endPoint.timeOffset += from;
            append(s);
        }
        const double delta = sourceLength - (to - from);
        for (auto s : right.segments) {
            s.startPoint.timeOffset += delta;
            s.endPoint.timeOffset += delta;
            append(s);
        }
        if (!left.Valid()) { throw std::invalid_argument("Invalid splice result"); }
        segments = std::move(left.segments);
    }

    // Four boundary nodes: left limit, new left value, new right value,
    // right limit. Equal-time pairs are intentional, sample-exact jumps.
    void BoxStep(double from, double to, double value)
    {
        if (segments.empty() || !std::isfinite(value) || value < 0 || value > 1
            || from < segments.front().startPoint.timeOffset
            || to > segments.back().endPoint.timeOffset || !std::isfinite(from)
            || !std::isfinite(to) || from >= to) { throw std::invalid_argument("Invalid automation shelf"); }
        auto left = Slice(segments.front().startPoint.timeOffset, from);
        auto right = Slice(to, segments.back().endPoint.timeOffset);
        const double before = left.segments.empty() ? Evaluate(from) : left.segments.back().endPoint.value;
        const double after = Evaluate(to);
        left.segments.push_back({{from, before}, {from, value}, 0});
        left.segments.push_back({{from, value}, {to, value}, 0});
        left.segments.push_back({{to, value}, {to, after}, 0});
        left.segments.insert(left.segments.end(), right.segments.begin(), right.segments.end());
        segments = std::move(left.segments);
    }

    // Control-thread look-ahead: bounded normalized error for every tiny ramp.
    // Q(u) - its chord has maximum magnitude |c * delta| / (4 * n^2).
    // The audio callback receives only these linear knots, never Bezier math.
    std::vector<Point> Linearize(double tolerance = 1e-5) const
    {
        if (!Valid() || !std::isfinite(tolerance) || tolerance <= 0) {
            throw std::invalid_argument("Invalid automation curve/tolerance");
        }
        std::vector<Point> points;
        if (segments.empty()) { return points; }
        points.push_back(segments.front().startPoint);
        for (const auto& s : segments) {
            const double bound = std::abs(kCurvatureStrength * s.curvature * (s.endPoint.value - s.startPoint.value));
            const auto count = std::max(1.0, std::ceil(std::sqrt(bound / (4 * tolerance))));
            if (count > 65536 || points.size() + count > 1000000) {
                throw std::length_error("Automation ramp budget exceeded");
            }
            for (size_t i = 1; i <= static_cast<size_t>(count); ++i) {
                const double t = s.startPoint.timeOffset
                    + (s.endPoint.timeOffset - s.startPoint.timeOffset) * i / count;
                const Point next{t, s.Evaluate(t)};
                if (points.back().timeOffset == t && points.back().value == next.value) { continue; }
                if (points.size() > 1 && points[points.size() - 2].timeOffset == t) {
                    points.back() = next;
                } else { points.push_back(next); }
            }
        }
        return points;
    }
};
}
