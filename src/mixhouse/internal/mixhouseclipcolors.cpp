/*
 * GlideMix
 */
#include "mixhouseclipcolors.h"

#include <algorithm>

#include "au3-track/Track.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-xml/XMLWriter.h"
#include "au3wrap/internal/domaccessor.h"
#include "project/iaudacityproject.h"

using namespace au::mixhouse;

namespace {
//! Saved in the project as attributes of the track: mixtape_colorbykey and
//! mixtape_colorintensity. Older projects have neither and get the defaults.
class MixtapeTrackColorSettings final : public TrackAttachment
{
public:
    static MixtapeTrackColorSettings& Get(Track& track);

    explicit MixtapeTrackColorSettings(Track&) {}

    void CopyTo(Track& track) const override
    {
        auto& copy = Get(track);
        copy.colorByKey = colorByKey;
        copy.intensity = intensity;
    }

    void WriteXMLAttributes(XMLWriter& writer) const override
    {
        writer.WriteAttr(wxT("mixtape_colorbykey"), colorByKey);
        writer.WriteAttr(wxT("mixtape_colorintensity"), intensity);
    }

    bool HandleXMLAttribute(const std::string_view& attr, const XMLAttributeValueView& value) override
    {
        if (attr == "mixtape_colorbykey") {
            bool on = true;
            if (value.TryGet(on)) {
                colorByKey = on;
            }
            return true;
        }
        if (attr == "mixtape_colorintensity") {
            int v = IMixHouseClipColors::kFullIntensity;
            if (value.TryGet(v)) {
                intensity = std::clamp(v, 0, IMixHouseClipColors::kFullIntensity);
            }
            return true;
        }
        return false;
    }

    bool colorByKey = true;   // on unless turned off, as the parked app's laneColorByKey()
    int intensity = IMixHouseClipColors::kFullIntensity;
};

const AttachedTrackObjects::RegisteredFactory mixtapeTrackColorSettingsKey{
    [](Track& track) { return std::make_shared<MixtapeTrackColorSettings>(track); }
};

MixtapeTrackColorSettings& MixtapeTrackColorSettings::Get(Track& track)
{
    return track.AttachedTrackObjects::Get<MixtapeTrackColorSettings>(mixtapeTrackColorSettingsKey);
}
}

MixHouseClipColors::MixHouseClipColors(const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx)
{
    m_recordTimer.setSingleShot(true);
    m_recordTimer.setInterval(300);
    QObject::connect(&m_recordTimer, &QTimer::timeout, [this]() {
        if (projectHistory()) {
            projectHistory()->modifyState();
            projectHistory()->markUnsaved();
        }
    });
}

WaveTrack* MixHouseClipColors::waveTrack(const trackedit::TrackId& track) const
{
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return nullptr;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    return au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(track));
}

void MixHouseClipColors::subscribeToHistory()
{
    if (m_subscribed || !projectHistory()) {
        return;
    }
    m_subscribed = true;
    // Undo and redo swap the tracks for their saved copies, settings
    // included: redraw every track's clips.
    projectHistory()->historyChanged().onReceive(this, [this](const auto&) {
        const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
        if (!project) {
            return;
        }
        for (const trackedit::TrackId id : project->trackIdList()) {
            m_changed.send(id);
        }
    });
}

void MixHouseClipColors::recordChange()
{
    m_recordTimer.start();
}

int MixHouseClipColors::intensity(const trackedit::TrackId& track) const
{
    WaveTrack* t = waveTrack(track);
    return t ? MixtapeTrackColorSettings::Get(*t).intensity : kFullIntensity;
}

void MixHouseClipColors::setIntensity(const trackedit::TrackId& track, int value)
{
    const_cast<MixHouseClipColors*>(this)->subscribeToHistory();
    value = std::clamp(value, 0, kFullIntensity);
    WaveTrack* t = waveTrack(track);
    if (!t || MixtapeTrackColorSettings::Get(*t).intensity == value) {
        return;
    }
    MixtapeTrackColorSettings::Get(*t).intensity = value;
    recordChange();
    m_changed.send(track);
}

bool MixHouseClipColors::colorByKey(const trackedit::TrackId& track) const
{
    const_cast<MixHouseClipColors*>(this)->subscribeToHistory();
    WaveTrack* t = waveTrack(track);
    return t ? MixtapeTrackColorSettings::Get(*t).colorByKey : true;
}

void MixHouseClipColors::setColorByKey(const trackedit::TrackId& track, bool on)
{
    WaveTrack* t = waveTrack(track);
    if (!t || MixtapeTrackColorSettings::Get(*t).colorByKey == on) {
        return;
    }
    MixtapeTrackColorSettings::Get(*t).colorByKey = on;
    // Straight away, not coalesced: a colour pick has just pushed its own
    // undo step, and this belongs in that step.
    m_recordTimer.stop();
    if (projectHistory()) {
        projectHistory()->modifyState();
        projectHistory()->markUnsaved();
    }
    m_changed.send(track);
}

QColor MixHouseClipColors::keyColor(const trackedit::ClipKey& clip) const
{
    if (!colorByKey(clip.trackId) || !keyMatch()) {
        return QColor();
    }
    return IMixHouseKeyMatch::keyColor(keyMatch()->keyInfo(clip).effectiveKeyId);
}

muse::async::Channel<au::trackedit::TrackId> MixHouseClipColors::trackColorsChanged() const
{
    return m_changed;
}
