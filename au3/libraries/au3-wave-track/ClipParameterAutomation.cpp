/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ClipParameterAutomation.h"

#include <QUuid>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
constexpr size_t MaxCurves = 4096;
constexpr size_t MaxPointsPerCurve = 1000000;

bool IsNormalized(double value)
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}
}

clipautomation::Curve ClipParameterAutomation::Curve::Bezier() const
{
    if (!bezier.segments.empty()) { return bezier; }
    clipautomation::Curve result;
    clipautomation::Point previous{0, envelope.GetValue(envelope.GetOffset())};
    for (size_t i = 0; i < envelope.GetNumberOfPoints(); ++i) {
        const auto& p = envelope[i];
        clipautomation::Point next{p.GetT(), p.GetVal()};
        // GlideMix: a level held flat turns into an S-curve once a point on
        // it is moved (new ramps ease like the tempo line's glides).
        result.segments.push_back({previous, next, 0, previous.value == next.value});
        previous = next;
    }
    const double length = std::max(previous.timeOffset, envelope.GetTrackLen());
    result.segments.push_back({previous, {length, previous.value}, 0, true});
    return result;
}

void ClipParameterAutomation::Curve::SetBezier(clipautomation::Curve data)
{
    const auto points = data.Linearize(); // validate and allocate before mutation
    auto cache = envelope;
    cache.Clear();
    for (const auto& p : points) { cache.Insert(p.timeOffset, p.value); }
    envelope = std::move(cache);
    bezier = std::move(data);
}

ClipParameterAutomation::Curve* ClipParameterAutomation::FindCurve(
    const std::string& effectInstanceId, const std::string& parameterId)
{
    const auto it = std::find_if(mCurves.begin(), mCurves.end(), [&](const auto& curve) {
        return curve.effectInstanceId == effectInstanceId && curve.parameterId == parameterId;
    });
    return it == mCurves.end() ? nullptr : &*it;
}

ClipParameterAutomation::Curve* ClipParameterAutomation::AddCurve(
    const std::string& effectInstanceId, const std::string& pluginId,
    const std::string& parameterId, double defaultValue)
{
    const QUuid uuid(QString::fromStdString(effectInstanceId));
    const auto canonicalId = uuid.toString(QUuid::WithoutBraces).toStdString();
    if (mLoadError || uuid.isNull() || pluginId.empty() || pluginId.size() > 65536
        || parameterId.empty() || parameterId.size() > 1024
        || !IsNormalized(defaultValue) || mCurves.size() >= MaxCurves
        || FindCurve(canonicalId, parameterId)) {
        return nullptr;
    }
    auto& curve = mCurves.emplace_back();
    curve.effectInstanceId = canonicalId;
    curve.pluginId = pluginId;
    curve.parameterId = parameterId;
    curve.envelope.Flatten(defaultValue);
    curve.envelope.SetOffset(mOffset);
    curve.envelope.SetTrackLen(mLength);
    return &curve;
}

void ClipParameterAutomation::SetOffset(double offset)
{
    mOffset = offset;
    ForEachEnvelope([&](auto& envelope) { envelope.SetOffset(offset); });
}

void ClipParameterAutomation::SetTrackLen(double length, double sampleDuration)
{
    mLength = length;
    ForEachEnvelope([&](auto& envelope) {
        if (envelope.GetTrackLen() != length) { envelope.SetTrackLen(length, sampleDuration); }
    });
}

void ClipParameterAutomation::RescaleTimesBy(double ratio)
{
    mLength *= ratio;
    for (auto& curve : mCurves) { curve.bezier.Stretch(ratio); }
    ForEachEnvelope([&](auto& envelope) { envelope.RescaleTimesBy(ratio); });
}

void ClipParameterAutomation::RescaleTimes(double length)
{
    if (mLength > 0) {
        for (auto& curve : mCurves) { curve.bezier.Stretch(length / mLength); }
    }
    mLength = length;
    ForEachEnvelope([&](auto& envelope) { envelope.RescaleTimes(length); });
}

void ClipParameterAutomation::InsertSpace(double time, double length)
{
    for (auto& curve : mCurves) {
        if (curve.bezier.segments.empty()) { continue; }
        const double local = std::clamp(time - mOffset, 0.0, mLength);
        const double value = curve.bezier.Evaluate(local);
        clipautomation::Curve hold;
        hold.segments.push_back({{0, value}, {length, value}, 0});
        curve.bezier.Splice(local, local, hold, length);
    }
    mLength += length;
    ForEachEnvelope([&](auto& envelope) { envelope.InsertSpace(time, length); });
}

void ClipParameterAutomation::CollapseRegion(double start, double end, double sampleDuration)
{
    const auto first = std::clamp(start - mOffset, 0.0, mLength);
    const auto last = std::clamp(end - mOffset, 0.0, mLength);
    for (auto& curve : mCurves) {
        if (!curve.bezier.segments.empty() && last > first) {
            curve.bezier.Splice(first, last, {}, 0);
        }
    }
    mLength -= std::max(0.0, last - first);
    ForEachEnvelope([&](auto& envelope) { envelope.CollapseRegion(start, end, sampleDuration); });
}

bool ClipParameterAutomation::CanPaste(const ClipParameterAutomation& source) const noexcept
{
    if (mLoadError || source.mLoadError) { return false; }
    auto count = mCurves.size();
    for (const auto& sourceCurve : source.mCurves) {
        const auto it = std::find_if(mCurves.begin(), mCurves.end(), [&](const auto& curve) {
            return curve.effectInstanceId == sourceCurve.effectInstanceId && curve.parameterId == sourceCurve.parameterId;
        });
        if (it == mCurves.end()) { ++count; }
        else if (it->pluginId != sourceCurve.pluginId) { return false; }
    }
    return count <= MaxCurves;
}

void ClipParameterAutomation::Paste(double time, const ClipParameterAutomation& source, double sampleDuration)
{
    // Work on a copy: duplicate bindings, allocation failure, or a bad source
    // must not leave a partially merged collection.
    auto result = *this;
    if (!CanPaste(source)) {
        throw std::runtime_error("Cannot paste unreadable or conflicting plugin automation data");
    }
    for (const auto& sourceCurve : source.mCurves) {
        auto* destination = result.FindCurve(sourceCurve.effectInstanceId, sourceCurve.parameterId);
        if (!destination) {
            destination = result.AddCurve(sourceCurve.effectInstanceId, sourceCurve.pluginId,
                                          sourceCurve.parameterId, sourceCurve.envelope.GetDefaultValue());
            if (destination) {
                destination->enabled = sourceCurve.enabled;
                destination->stepped = sourceCurve.stepped;
            }
        }
        if (!destination || destination->pluginId != sourceCurve.pluginId) {
            throw std::runtime_error("Conflicting plugin automation binding");
        }
    }
    for (auto& destination : result.mCurves) {
        const auto it = std::find_if(source.mCurves.begin(), source.mCurves.end(), [&](const auto& curve) {
            return curve.effectInstanceId == destination.effectInstanceId && curve.parameterId == destination.parameterId;
        });
        if (it == source.mCurves.end()) {
            if (!destination.bezier.segments.empty()) {
                const double local = std::clamp(time - mOffset, 0.0, mLength);
                const double value = destination.bezier.Evaluate(local);
                clipautomation::Curve hold;
                hold.segments.push_back({{0, value}, {source.mLength, value}, 0});
                destination.bezier.Splice(local, local, hold, source.mLength);
            }
            destination.envelope.InsertSpace(time, source.mLength);
        } else {
            if (!destination.bezier.segments.empty() || !it->bezier.segments.empty()) {
                auto shape = destination.Bezier();
                const double local = std::clamp(time - mOffset, 0.0, mLength);
                shape.Splice(local, local, it->Bezier(), source.mLength);
                destination.bezier = std::move(shape);
            }
            destination.envelope.PasteEnvelope(time, &it->envelope, sampleDuration);
        }
    }
    result.mLength += source.mLength;
    *this = std::move(result);
}

bool ClipParameterAutomation::HandleXMLTag(const std::string_view& tag, const AttributesList& attrs)
{
    const auto fail = [&] { mLoadError = true; return false; };
    if (tag == XMLTag) {
        mCurves.clear();
        mReadingCurve.reset();
        mLoadError = false;
        int version = -1;
        for (const auto& [name, value] : attrs) {
            if (name == "version" && !value.TryGet(version)) {
                return fail();
            }
        }
        return version == 1 || version == SchemaVersion || fail();
    }
    if (mLoadError) {
        return false;
    }
    if (tag == "curve" && !mReadingCurve) {
        std::string instance, plugin, parameter;
        double defaultValue = -1.0;
        bool enabled = true;
        bool stepped = false;
        for (const auto& [name, value] : attrs) {
            if (name == "instance") { instance = value.ToString(); }
            else if (name == "plugin") { plugin = value.ToString(); }
            else if (name == "parameter") { parameter = value.ToString(); }
            else if (name == "default" && !value.TryGet(defaultValue)) { return fail(); }
            else if (name == "enabled" && !value.TryGet(enabled)) { return fail(); }
            else if (name == "stepped" && !value.TryGet(stepped)) { return fail(); }
        }
        auto* curve = AddCurve(instance, plugin, parameter, defaultValue);
        if (!curve) { return fail(); }
        curve->enabled = enabled;
        curve->stepped = stepped;
        mReadingCurve = mCurves.size() - 1;
        return true;
    }
    if (tag == "point" && mReadingCurve) {
        if (!mCurves[*mReadingCurve].bezier.segments.empty()) { return fail(); }
        double time = -1.0, normalized = -1.0;
        for (const auto& [name, value] : attrs) {
            if (name == "t" && !value.TryGet(time)) { return fail(); }
            if (name == "v" && !value.TryGet(normalized)) { return fail(); }
        }
        auto& envelope = mCurves[*mReadingCurve].envelope;
        const auto count = envelope.GetNumberOfPoints();
        if (!std::isfinite(time) || time < 0.0 || !IsNormalized(normalized)
            || count >= MaxPointsPerCurve
            || (count && time < envelope[count - 1].GetT())) {
            return fail();
        }
        // Two equal-time points represent the limits at a discontinuity.
        // GlideMix: a third one there (written by earlier builds) takes the
        // second's place, instead of failing the whole clip's automation --
        // that made every later save throw, and the app quit.
        if (count > 1 && time == envelope[count - 2].GetT()) {
            envelope.Delete(static_cast<int>(count) - 1);
        }
        envelope.Insert(time, normalized);
        return true;
    }
    if (tag == "segment" && mReadingCurve) {
        auto& curve = mCurves[*mReadingCurve];
        if (curve.envelope.GetNumberOfPoints() || curve.bezier.segments.size() >= MaxPointsPerCurve) { return fail(); }
        double t0 = -1, t1 = -1, v0 = -1, v1 = -1, c = 2;
        bool eased = false;
        for (const auto& [name, value] : attrs) {
            if (name == "t0" && !value.TryGet(t0)) { return fail(); }
            if (name == "t1" && !value.TryGet(t1)) { return fail(); }
            if (name == "v0" && !value.TryGet(v0)) { return fail(); }
            if (name == "v1" && !value.TryGet(v1)) { return fail(); }
            if (name == "c" && !value.TryGet(c)) { return fail(); }
            if (name == "e" && !value.TryGet(eased)) { return fail(); }
        }
        curve.bezier.segments.push_back({{t0, v0}, {t1, v1}, static_cast<float>(c), eased});
        // Validate only the new join; avoid quadratic-time loading.
        clipautomation::Curve check;
        if (curve.bezier.segments.size() > 1) { check.segments.push_back(curve.bezier.segments[curve.bezier.segments.size() - 2]); }
        check.segments.push_back(curve.bezier.segments.back());
        return check.Valid() || fail();
    }
    return fail();
}

XMLTagHandler* ClipParameterAutomation::HandleXMLChild(const std::string_view& tag)
{
    if ((!mReadingCurve && tag == "curve") || (mReadingCurve && (tag == "point" || tag == "segment"))) {
        return this;
    }
    mLoadError = true;
    return nullptr;
}

void ClipParameterAutomation::HandleXMLEndTag(const std::string_view& tag)
{
    if (tag == "curve") {
        if (mReadingCurve && !mCurves[*mReadingCurve].bezier.segments.empty() && !mLoadError) {
            try { mCurves[*mReadingCurve].SetBezier(mCurves[*mReadingCurve].bezier); }
            catch (...) { mLoadError = true; }
        }
        mReadingCurve.reset();
    }
}

void ClipParameterAutomation::WriteXML(XMLWriter& writer) const
{
    if (mLoadError) {
        throw std::runtime_error("Cannot save malformed or unsupported plugin automation data");
    }
    if (Empty()) { return; }
    writer.StartTag(XMLTag);
    writer.WriteAttr("version", SchemaVersion);
    for (const auto& curve : mCurves) {
        writer.StartTag("curve");
        writer.WriteAttr("instance", wxString::FromUTF8(curve.effectInstanceId));
        writer.WriteAttr("plugin", wxString::FromUTF8(curve.pluginId));
        writer.WriteAttr("parameter", wxString::FromUTF8(curve.parameterId));
        writer.WriteAttr("default", curve.envelope.GetDefaultValue(), 17);
        writer.WriteAttr("enabled", curve.enabled);
        if (curve.stepped) {
            writer.WriteAttr("stepped", true);
        }
        if (!curve.bezier.segments.empty()) {
            if (!curve.bezier.Valid()) { throw std::runtime_error("Invalid Bezier automation"); }
            for (const auto& s : curve.bezier.segments) {
                writer.StartTag("segment");
                writer.WriteAttr("t0", s.startPoint.timeOffset, 17);
                writer.WriteAttr("v0", s.startPoint.value, 17);
                writer.WriteAttr("t1", s.endPoint.timeOffset, 17);
                writer.WriteAttr("v1", s.endPoint.value, 17);
                writer.WriteAttr("c", static_cast<double>(s.curvature), 17);
                if (s.eased) {
                    writer.WriteAttr("e", true);
                }
                writer.EndTag("segment");
            }
        } else for (size_t i = 0; i < curve.envelope.GetNumberOfPoints(); ++i) {
            const auto& point = curve.envelope[i];
            writer.StartTag("point");
            writer.WriteAttr("t", point.GetT(), 17);
            writer.WriteAttr("v", point.GetVal(), 17);
            writer.EndTag("point");
        }
        writer.EndTag("curve");
    }
    writer.EndTag(XMLTag);
}
