/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "au3-mixer/Envelope.h"
#include "au3-components/BezierAutomation.h"

#include <optional>
#include <string>
#include <vector>

namespace clipautomation {
//! Controls built into the Mixtape engine (no plugin involved) ride in the same
//! per-clip curve store as plugin parameters, so they follow the clip through
//! moves, trims, stretches, copies, undo and saving. They are keyed by this
//! reserved instance id, which is a fixed UUID because AddCurve() refuses any
//! id that is not one; it cannot clash with a real effect instance, whose ids
//! are random. Values are normalised 0..1.
inline constexpr const char* kMixtapeBuiltinInstanceId = "b7c3e0a2-4d1f-4c8e-9a55-6f1d2e7a4b10";
inline constexpr const char* kMixtapeBuiltinPluginId = "mixtape.builtin";
//! The bass band of the transition EQ: 1 = untouched, 0 = bass removed.
inline constexpr const char* kMixtapeLowBandParameter = "low";
//! The mids and highs of the transition EQ, the same way.
inline constexpr const char* kMixtapeMidBandParameter = "mid";
inline constexpr const char* kMixtapeHighBandParameter = "high";
}

//! Project data only. Mutate on the main thread and snapshot for audio processing.
//! Never substitute these envelopes for WaveClip's audio gain envelope.
class WAVE_TRACK_API ClipParameterAutomation final : public XMLTagHandler
{
public:
    struct Curve {
        std::string effectInstanceId;
        std::string pluginId;
        std::string parameterId;
        bool enabled{true};
        //! GlideMix: a switch or stepped choice -- holds each point's value until
        //! the next point (Ableton's on/off steps) instead of ramping.
        bool stepped{false};
        // Normalized continuous parameter values, NOT amplitude/dB values.
        // Point times are relative to the untrimmed clip's sequence start,
        // in stretched seconds, just like the existing gain envelope.
        Envelope envelope{false, 0.0, 1.0, 0.0};
        // Canonical curved representation when nonempty. The envelope remains
        // a linear compatibility cache, separate from WaveClip's volume data.
        clipautomation::Curve bezier;
        clipautomation::Curve Bezier() const;
        void SetBezier(clipautomation::Curve data);
    };

    static constexpr auto XMLTag = "parameterautomation";
    static constexpr int SchemaVersion = 2;

    const std::vector<Curve>& Curves() const noexcept { return mCurves; }
    bool Empty() const noexcept { return mCurves.empty(); }
    bool HasLoadError() const noexcept { return mLoadError; }

    //! Bind by persistent instance identity plus parameter, never by chain slot.
    //! Returns null for malformed/duplicate bindings. No plugin loading occurs.
    Curve* AddCurve(const std::string& effectInstanceId, const std::string& pluginId,
                    const std::string& parameterId, double defaultValue);
    Curve* FindCurve(const std::string& effectInstanceId, const std::string& parameterId);

    template<typename Function> void ForEachEnvelope(Function&& function)
    {
        for (auto& curve : mCurves) {
            function(curve.envelope);
        }
    }

    void SetOffset(double offset);
    void SetTrackLen(double length, double sampleDuration);
    void RescaleTimesBy(double ratio);
    void RescaleTimes(double length);
    void InsertSpace(double time, double length);
    void CollapseRegion(double start, double end, double sampleDuration);
    bool CanPaste(const ClipParameterAutomation& source) const noexcept;
    //! Source/destination must already refer to the same track effect chain.
    void Paste(double time, const ClipParameterAutomation& source, double sampleDuration);

    bool HandleXMLTag(const std::string_view& tag, const AttributesList& attrs) override;
    XMLTagHandler* HandleXMLChild(const std::string_view& tag) override;
    void HandleXMLEndTag(const std::string_view& tag) override;
    void WriteXML(XMLWriter& writer) const;

private:
    std::vector<Curve> mCurves;
    double mOffset{};
    double mLength{};
    // Invalid/newer data must never be silently overwritten by an empty list.
    bool mLoadError{};
    std::optional<size_t> mReadingCurve;
};
