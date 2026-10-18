/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  GlideMix

  PlaybackSpeedCurve.cpp

**********************************************************************/
#include "PlaybackSpeedCurve.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace {
std::mutex& LiveMutex()
{
    static std::mutex mutex;
    return mutex;
}

struct LiveState {
    const BoundedEnvelope* token = nullptr;
    std::shared_ptr<const PlaybackSpeedCurve> curve;
};

LiveState& State()
{
    static LiveState state;
    return state;
}

// Real seconds across `duration` of track time while speed goes linearly
// from v0 to v1 (Envelope::IntegrateInverseInterpolated, linear case).
double InverseIntegral(double v0, double v1, double duration)
{
    const double l = std::log(v0 / v1);
    if (std::fabs(l) < 1.0e-5) {
        return 2.0 / (v0 + v1) * duration;
    }
    return l / (v0 - v1) * duration;
}

// Track time, as a fraction of `duration`, where `area` real seconds are
// reached (Envelope::SolveIntegrateInverseInterpolated, linear case).
double SolveInverseIntegral(double v0, double v1, double duration, double area)
{
    const double a = area / duration;
    double res;
    if (std::fabs(v1 - v0) < 1.0e-5) {
        res = a * (v0 + v1) * 0.5;
    } else {
        res = v0 * std::expm1(a * (v1 - v0)) / (v1 - v0);
    }
    return std::clamp(res, 0.0, 1.0) * duration;
}
} // namespace

double PlaybackSpeedCurve::SpeedAt(double t) const
{
    if (knots.empty()) {
        return 1.0;
    }
    if (t <= knots.front().first) {
        return knots.front().second;
    }
    if (t >= knots.back().first) {
        return knots.back().second;
    }
    const auto it = std::upper_bound(knots.begin(), knots.end(), t,
                                     [](double time, const std::pair<double, double>& k) { return time < k.first; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double dur = b.first - a.first;
    return dur > 0.0 ? a.second + (b.second - a.second) * (t - a.first) / dur : b.second;
}

double PlaybackSpeedCurve::IntegralOfInverse(double t0, double t1) const
{
    if (t0 == t1) {
        return 0.0;
    }
    if (t1 < t0) {
        return -IntegralOfInverse(t1, t0);
    }
    if (knots.empty()) {
        return t1 - t0;
    }
    double total = 0.0;
    double t = t0;
    double v = SpeedAt(t);
    auto it = std::upper_bound(knots.begin(), knots.end(), t,
                               [](double time, const std::pair<double, double>& k) { return time < k.first; });
    for (; it != knots.end() && it->first < t1; ++it) {
        total += InverseIntegral(v, it->second, it->first - t);
        t = it->first;
        v = it->second;
    }
    return total + InverseIntegral(v, SpeedAt(t1), t1 - t);
}

double PlaybackSpeedCurve::SolveIntegralOfInverse(double t0, double area) const
{
    if (area == 0.0) {
        return t0;
    }
    if (knots.empty()) {
        return t0 + area;
    }
    double t = t0;
    double v = SpeedAt(t);
    if (area > 0.0) {
        auto it = std::upper_bound(knots.begin(), knots.end(), t,
                                   [](double time, const std::pair<double, double>& k) { return time < k.first; });
        for (; it != knots.end(); ++it) {
            const double added = InverseIntegral(v, it->second, it->first - t);
            if (added >= area) {
                return t + SolveInverseIntegral(v, it->second, it->first - t, area);
            }
            area -= added;
            t = it->first;
            v = it->second;
        }
        return t + area * v;
    }
    // Backwards: walk to earlier knots, speed going from v(t) towards them.
    area = -area;
    auto it = std::lower_bound(knots.begin(), knots.end(), t,
                               [](const std::pair<double, double>& k, double time) { return k.first < time; });
    while (it != knots.begin()) {
        --it;
        const double added = InverseIntegral(v, it->second, t - it->first);
        if (added >= area) {
            return t - SolveInverseIntegral(v, it->second, t - it->first, area);
        }
        area -= added;
        t = it->first;
        v = it->second;
    }
    return t - area * v;
}

void PlaybackSpeedCurve::SetLive(const BoundedEnvelope* token, std::shared_ptr<const PlaybackSpeedCurve> curve)
{
    std::lock_guard<std::mutex> lock(LiveMutex());
    State().token = token;
    State().curve = std::move(curve);
}

std::shared_ptr<const PlaybackSpeedCurve> PlaybackSpeedCurve::LiveFor(const BoundedEnvelope* envelope)
{
    if (!envelope) {
        return {};
    }
    // Held only to copy a pointer; audio threads never wait long.
    std::lock_guard<std::mutex> lock(LiveMutex());
    return State().token == envelope ? State().curve : nullptr;
}

namespace {
std::atomic<double>& ProjectTempoValue()
{
    static std::atomic<double> tempo { 0.0 };
    return tempo;
}
}

void PlaybackSpeedCurve::SetProjectTempo(double bpm)
{
    ProjectTempoValue().store(bpm, std::memory_order_relaxed);
}

double PlaybackSpeedCurve::ProjectTempo()
{
    return ProjectTempoValue().load(std::memory_order_relaxed);
}

namespace {
std::atomic<double>& BeatOriginValue()
{
    static std::atomic<double> origin { 0.0 };
    return origin;
}
}

void PlaybackSpeedCurve::SetBeatOrigin(double time)
{
    BeatOriginValue().store(time, std::memory_order_relaxed);
}

double PlaybackSpeedCurve::BeatOrigin()
{
    return BeatOriginValue().load(std::memory_order_relaxed);
}

std::shared_ptr<const PlaybackSpeedCurve> PlaybackSpeedCurve::Live()
{
    std::lock_guard<std::mutex> lock(LiveMutex());
    return State().curve;
}
