/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "TrackParameterAutomation.h"
#include "WaveTrack.h"
#include "WaveClip.h"

#include <algorithm>
#include <stdexcept>

ParameterAutomationSnapshot BuildTrackParameterAutomation(
    const WaveTrack& track, const std::string& effectInstanceId, const std::string& pluginId)
{
    std::vector<ParameterAutomationSnapshot::Region> regions;
    for (const auto& clip : track.Intervals()) {
        const auto& data = clip->GetParameterAutomation();
        if (data.HasLoadError()) {
            throw std::runtime_error("Cannot prepare unreadable plugin automation data");
        }
        if (clip->GetPlayEndTime() <= clip->GetPlayStartTime()) { continue; }
        for (const auto& curve : data.Curves()) {
            if (!curve.enabled || curve.effectInstanceId != effectInstanceId || curve.pluginId != pluginId) { continue; }
            auto& region = regions.emplace_back();
            region.parameterId = curve.parameterId;
            region.start = clip->GetPlayStartTime();
            region.end = clip->GetPlayEndTime();
            // Play start is snapped to a sample; the sequence offset isn't. A clip
            // placed between samples (bar alignment does this) can snap below its
            // own offset, which the snapshot rejects. The sub-sample gap is inaudible.
            region.offset = std::min(clip->GetSequenceStartTime(), region.start);
            region.defaultValue = curve.envelope.GetDefaultValue();
            region.stepped = curve.stepped;
            if (curve.stepped && !curve.bezier.segments.empty()) {
                // Steps: only the points themselves, no curve in between.
                for (const auto& segment : curve.bezier.segments) {
                    region.points.push_back({segment.startPoint.timeOffset, segment.startPoint.value});
                }
                const auto& last = curve.bezier.segments.back().endPoint;
                region.points.push_back({last.timeOffset, last.value});
                continue;
            }
            if (!curve.bezier.segments.empty()) {
                const auto ramps = curve.bezier.Linearize();
                region.points.reserve(ramps.size());
                for (const auto& point : ramps) { region.points.push_back({point.timeOffset, point.value}); }
                continue;
            }
            region.points.reserve(curve.envelope.GetNumberOfPoints());
            for (size_t i = 0; i < curve.envelope.GetNumberOfPoints(); ++i) {
                const auto& point = curve.envelope[i];
                region.points.push_back({point.GetT(), point.GetVal()});
            }
        }
    }
    return ParameterAutomationSnapshot(std::move(regions));
}
