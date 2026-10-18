/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ParameterAutomationSnapshot.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {
bool Normalized(double value)
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

double ValueAt(const ParameterAutomationSnapshot::Region& region, double projectTime) noexcept
{
    const auto& points = region.points;
    if (points.empty()) { return region.defaultValue; }
    const double relative = projectTime - region.offset;
    const auto after = std::upper_bound(points.begin(), points.end(), relative,
        [](double time, const auto& point) { return time < point.time; });
    if (after == points.begin()) { return points.front().value; }
    if (after == points.end()) { return points.back().value; }
    const auto& before = *(after - 1);
    if (region.stepped) { return before.value; }
    // upper_bound selects the right limit of an equal-time pair, so this
    // denominator is positive, including immediately after a discontinuity.
    const double fraction = (relative - before.time) / (after->time - before.time);
    return before.value + fraction * (after->value - before.value);
}
}

ParameterAutomationSnapshot::ParameterAutomationSnapshot(std::vector<Region> regions)
{
    for (const auto& region : regions) {
        if (region.parameterId.empty() || !std::isfinite(region.start)
            || !std::isfinite(region.end) || region.end <= region.start
            || !std::isfinite(region.offset) || region.offset > region.start
            || !Normalized(region.defaultValue)) {
            throw std::invalid_argument("Invalid plugin automation region");
        }
        for (size_t i = 0; i < region.points.size(); ++i) {
            const auto& point = region.points[i];
            if (!std::isfinite(point.time) || point.time < 0.0 || !Normalized(point.value)
                || (i && point.time < region.points[i - 1].time)
                || (i > 1 && point.time == region.points[i - 2].time)) {
                throw std::invalid_argument("Invalid plugin automation point");
            }
        }
    }
    std::stable_sort(regions.begin(), regions.end(), [](const auto& a, const auto& b) {
        return a.parameterId < b.parameterId || (a.parameterId == b.parameterId && a.start < b.start);
    });
    for (auto& region : regions) {
        if (mParameters.empty() || mParameters.back().id != region.parameterId) {
            mParameters.push_back({region.parameterId, {}});
        }
        mParameters.back().regions.push_back(std::move(region));
    }
}

ParameterAutomationSnapshot::Value ParameterAutomationSnapshot::Evaluate(
    size_t parameter, double projectTime) const noexcept
{
    if (!std::isfinite(projectTime)) { return {Status::InvalidTime, 0.0}; }
    if (parameter >= mParameters.size()) { return {}; }
    const Region* active = nullptr;
    for (const auto& region : mParameters[parameter].regions) {
        if (region.start > projectTime) { break; }
        if (projectTime < region.end) {
            if (active) { return {Status::Conflict, 0.0}; }
            active = &region;
        }
    }
    return active ? Value{Status::Active, ValueAt(*active, projectTime)} : Value{};
}

void ParameterAutomationSnapshot::EvaluateBlock(size_t parameter, const double* projectTimes,
                                                size_t count, Value* output) const noexcept
{
    for (size_t i = 0; i < count; ++i) { output[i] = Evaluate(parameter, projectTimes[i]); }
}

void ParameterAutomationSnapshot::EvaluateUniformBlock(size_t parameter, double start,
    double secondsPerSample, size_t count, Value* output) const noexcept
{
    for (size_t i = 0; i < count; ++i) {
        output[i] = Evaluate(parameter, start + static_cast<double>(i) * secondsPerSample);
    }
}
