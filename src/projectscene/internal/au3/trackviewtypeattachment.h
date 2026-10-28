/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include "trackedit/dom/track.h"
#include "au3wrap/au3types.h"

#include "au3-track/Track.h"

namespace au::au3 {
class TrackViewTypeAttachment : public TrackAttachment
{
public:
    static TrackViewTypeAttachment& Get(const Au3Track* track);
    static TrackViewTypeAttachment& Get(Au3Track* track);

    TrackViewTypeAttachment(Au3Track& track);
    void Reparent(const std::shared_ptr<Au3Track>& parent) override;
    void CopyTo(Au3Track& track) const override;
    void WriteXMLAttributes(XMLWriter& writer) const override;
    bool HandleXMLAttribute(const std::string_view& attr, const XMLAttributeValueView& valueView) override;

    trackedit::TrackViewType GetTrackViewType() const;
    void SetTrackViewType(const trackedit::TrackViewType&);

    //! GlideMix: a stereo track drawn as one waveform (both channels in one lane).
    bool GetSingleWave() const { return mSingleWave; }
    //! GlideMix: the track's automation lanes, as JSON.
    const std::string& GetAutomationLanes() const { return mAutomationLanes; }
    void SetAutomationLanes(std::string json) { mAutomationLanes = std::move(json); }
    void SetSingleWave(bool single) { mSingleWave = single; }

private:
    std::weak_ptr<Au3Track> mTrack;
    trackedit::TrackViewType mTrackViewType = trackedit::TrackViewType::Undefined;
    // GlideMix: Single wave is the default view (Chris, 2026-10-28): one
    // waveform in half the height, more songs on screen.
    bool mSingleWave = true;
    std::string mAutomationLanes;
};
}
