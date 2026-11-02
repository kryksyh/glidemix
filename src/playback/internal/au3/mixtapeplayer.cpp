/*
 * GlideMix: the player on the new playback engine (stage 1).
 */
#include "mixtapeplayer.h"

#include <thread>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <utility>

#include "framework/global/log.h"
#include "framework/global/settings.h"

#include "au3-audio-devices/AudioIOBase.h"
#include "au3-audio-io/ProjectAudioIO.h"
#include "au3-mixtape-engine/MixtapeDemo.h"
#include "au3-mixtape-engine/MixtapeArrangement.h"
#include "au3-mixtape-engine/MixtapeClipVoice.h"
#include "au3-mixtape-engine/MixtapeEffects.h"
#include "au3-mixtape-engine/MixtapeRenderedSequence.h"
#include "au3-mixtape-engine/MixtapeRenderer.h"
#include "au3-import-export/ExportPluginHelpers.h"
#include "au3-mixtape-engine/MixtapeTransport.h"
#include "au3-mixer/PlaybackSpeedCurve.h"
#include "au3-numeric-formats/ProjectTimeSignature.h"
#include "au3-project/Project.h"
#include "au3-project-rate/ProjectRate.h"
#include "au3-track/Track.h"
#include "au3-wave-track/WaveTrack.h"

#include "au3wrap/au3types.h"

#include "au3player.h"

using namespace au::playback;

namespace {
std::atomic<bool> s_enginePlaying{ false };
}

MixtapePlayer::MixtapePlayer(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx)
    , m_regions(std::make_shared<Au3Player>(ctx))
    , m_transport(std::make_unique<mixtape::Transport>())
{
    m_status.val = PlaybackStatus::Stopped;
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.callOnTimeout([this]() { updatePlaybackPosition(); });
    // Au3Player owns the region; a seek there moves our position too.
    m_regions->playbackPositionChanged().onReceive(this, [this](muse::secs_t pos) {
        if (m_status.val == PlaybackStatus::Stopped) {
            m_position.set(pos);
        }
    });
    // GlideMix: exports render the mix the way playback does (master limiter,
    // tempo line, plugin automation), not with Audacity's own mixer.
    ExportPluginHelpers::SetDemoQuery([]() { return mixtape::DemoMode(); });
    ExportPluginHelpers::SetMixRenderHook([this](const AudacityProject& constProject, double t0, double t1, double rate,
                                                 bool tracksAsked) -> std::shared_ptr<const WideSampleSequence> {
        // The demo version always exports through the engine (its limits
        // live there); a licensed one lets a selection mix the tracks.
        if (tracksAsked && !mixtape::DemoMode()) {
            return nullptr;
        }
        auto& project = const_cast<AudacityProject&>(constProject);
        const auto arrangement = mixtape::SnapshotArrangement(TrackList::Get(project), ++m_generation);
        if (!arrangement || t1 <= t0) {
            return nullptr;
        }
        auto effects = std::make_shared<mixtape::EffectsHost>(project.shared_from_this(), rate, 2u,
                                                              mixtape::kRenderBlockFrames);
        std::map<int64_t, uint64_t> signatures;
        prepareEffects(effects.get(), signatures, arrangement);
        effects->MeasureLatencies(*arrangement);
        auto renderer = std::make_unique<mixtape::Renderer>(rate, 2);
        renderer->SetArrangement(arrangement);
        renderer->SetEffects(effects.get());
        renderer->UseLiveSpeedCurve(true);
        renderer->SetOffline(true);
        double seconds = mixtape::RenderedSequence::OutputSeconds(t0, t1);
        if (mixtape::DemoMode() && seconds > mixtape::kDemoExportLimitSec) {
            LOGI() << "Export: demo version, the first " << mixtape::kDemoExportLimitSec << " s only";
            seconds = mixtape::kDemoExportLimitSec;
        }
        LOGI() << "Export: rendering " << t0 << "-" << t1 << " s of the mix (" << seconds << " s out) at " << rate;
        auto sequence = std::make_shared<mixtape::RenderedSequence>(std::move(renderer), effects, t0, seconds, rate);
        sequence->SetOnFinished([](const mixtape::LoudnessReport& report) {
            LOGI() << "Export loudness: " << report.integratedLufs << " LUFS, true peak " << report.truePeakDb
                   << " dBTP, sample peak " << report.samplePeakDb << " dBFS over " << report.seconds << " s";
            ExportPluginHelpers::SetLastExportLoudness(ExportPluginHelpers::Loudness {
                report.integratedLufs, report.truePeakDb, report.samplePeakDb, report.seconds });
        });
        return sequence;
    });
}

MixtapePlayer::~MixtapePlayer()
{
    ExportPluginHelpers::SetMixRenderHook(nullptr);
    ExportPluginHelpers::SetDemoQuery(nullptr);
    stopPerformance();
    // Runs while the app's services are being torn down on quit: stopTransport
    // must not reach any of them.
    stopTransport();
}

namespace {
// GlideMix: where the headphone preview plays while performing.
const muse::Settings::Key kHeadphonesDeviceKey("playback", "mixtape/headphonesDevice");
}

bool MixtapePlayer::enabled()
{
    // GlideMix's engine is the player. It used to be switched on by a
    // marker file that only the development Mac had, so an installed copy
    // played through Audacity's stock player: no Perform, no GlideMix EQ or
    // keylock. MIXTAPE_ENGINE=0 (or the file use-old-engine in
    // ~/Library/Application Support/Glidemix) still selects the old one.
    static const bool on = [] {
        if (const char* value = std::getenv("MIXTAPE_ENGINE"); value && *value) {
            return *value != '0';
        }
        const char* home = std::getenv("HOME");
        return !(home && std::filesystem::exists(std::filesystem::path(home)
                                                 / "Library/Application Support/Glidemix/use-old-engine"));
    }();
    // The demo version's limits live in this engine: no way around them.
    return on || mixtape::DemoMode();
}

bool MixtapePlayer::engineIsPlaying()
{
    return s_enginePlaying.load();
}

void MixtapePlayer::setStatus(PlaybackStatus status)
{
    if (m_status.val != status) {
        m_status.set(status);
    }
}

void MixtapePlayer::prepareEffects(mixtape::EffectsHost* host, std::map<int64_t, uint64_t>& signatures,
                                   const std::shared_ptr<const mixtape::Arrangement>& arrangement)
{
    if (!host || !arrangement) {
        return;
    }
    for (const auto& track : arrangement->tracks) {
        if (!track.track) {
            continue;
        }
        host->EnsureTrack(*track.track);   // no-op once the track has processors
        auto& signature = signatures[track.trackId];
        if (signature != track.automationSignature) {
            if (signature != 0) {
                host->RefreshAutomation(*track.track);
            }
            signature = track.automationSignature;
        }
    }
}

void MixtapePlayer::publishArrangement()
{
    const auto project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    auto* au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    auto arrangement = mixtape::SnapshotArrangement(TrackList::Get(*au3Project), ++m_generation);
    // Processors before the render thread can see a new track. Only one of
    // the two transports has effects at a time (the audience one when
    // performing).
    prepareEffects(m_effects.get(), m_automationSignatures, arrangement);
    prepareEffects(m_performEffects.get(), m_performSignatures, arrangement);
    if (m_effects) {
        m_effects->MeasureLatencies(*arrangement);
    } else if (m_performEffects) {
        m_performEffects->MeasureLatencies(*arrangement);
    }
    const std::shared_ptr<const mixtape::Arrangement> shared = std::move(arrangement);
    if (m_transport->IsActive()) {
        m_transport->SetArrangement(shared);
    }
    if (m_perform && m_perform->IsActive()) {
        m_perform->SetArrangement(shared);
    }
}

void MixtapePlayer::stopTransport()
{
    m_timer.stop();
    m_transport->Stop();   // the render thread is gone before the effects host
    if (const auto meter = std::exchange(m_meter, nullptr)) {
        meter->stop();
    }
    m_effects.reset();
    m_automationSignatures.clear();
    s_enginePlaying = m_performActive;
}

bool MixtapePlayer::startAt(double startTime, double endTime)
{
    const auto project = globalContext()->currentProject();
    if (!project) {
        return false;
    }
    auto* au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    mixtape::Transport::Options options;
    options.sampleRate = ProjectRate::Get(*au3Project).GetRate();
    options.channels = 2;
    // Rendered ahead of the device: more on small machines (4 threads or
    // fewer), where a busy moment otherwise reached the speakers as a crackle.
    options.readAheadSeconds = std::thread::hardware_concurrency() > 0 && std::thread::hardware_concurrency() <= 4 ? 0.25 : 0.08;
    options.hostName = AudioIOHost.Read().ToStdString();
#ifdef Q_OS_WIN
    // Windows: the buffer Preferences sets (100 ms by default). macOS keeps
    // the device's lowest, which plays cleanly there.
    options.latencySeconds = AudioIOLatencyDuration.Read() / 1000.0;
#endif
    options.deviceName = AudioIOPlaybackDevice.Read().ToStdString();
    options.endTime = endTime;
    options.meter = ProjectAudioIO::Get(*au3Project).GetPlaybackMeter();
    // Headphones while performing: a separate device when one is chosen,
    // else outputs 3-4 when the interface has them.
    options.channelOffset = 0;
    if (m_performActive) {
        const std::string headphones = headphonesDevice();
        if (headphones.empty()) {
            options.channelOffset = 2;
        } else {
            options.deviceName = headphones;
        }
    }
    options.overlay = metronomeOverlay(options.sampleRate, options.channels);
    // Play is always the preview: the headphones while the audience is being
    // performed to, and the only thing anyone hears when it is not. It takes
    // the cue level; Perform takes the audience's.
    options.isCue = true;
    stopTransport();
    // While performing, the effects belong to the audience and the headphone
    // preview plays the tracks dry (see the m_effects check below). It keeps
    // its own meter, though: Perform feeds a separate sender, so the two mixes
    // are read independently -- the cue on the side bar, Perform on the header.
    if (options.meter) {
        options.meter->start(options.sampleRate);
        m_meter = options.meter;
    }
    if (!m_performActive) {
        m_effects = std::make_unique<mixtape::EffectsHost>(au3Project->shared_from_this(), options.sampleRate,
                                                           static_cast<unsigned>(options.channels),
                                                           mixtape::kRenderBlockFrames);
    }
    const auto arrangement = mixtape::SnapshotArrangement(TrackList::Get(*au3Project), ++m_generation);
    prepareEffects(m_effects.get(), m_automationSignatures, arrangement);
    if (m_effects) {
        m_effects->MeasureLatencies(*arrangement);
    }
    if (!m_transport->Start(options, arrangement, std::max(0.0, startTime), m_effects.get())) {
        LOGE() << "Mixtape engine could not start: " << m_transport->LastError();
        stopTransport();
        return false;
    }
    s_enginePlaying = true;
    m_reachedEnd.val = false;
    m_timer.start();
    LOGI() << "Mixtape engine playing from " << startTime << " s to " << endTime << " s on \""
           << (options.deviceName.empty() ? std::string("default") : options.deviceName) << "\" outputs "
           << (m_transport->ChannelOffset() + 1) << "-" << (m_transport->ChannelOffset() + 2)
           << (m_performActive ? " (headphones, dry)" : "");
    {
        const auto curve = PlaybackSpeedCurve::Live();
        std::string knots;
        if (curve) {
            for (const auto& [t, speed] : curve->knots) {
                knots += " " + std::to_string(t) + "s:x" + std::to_string(speed);
            }
        }
        LOGI() << "Mixtape engine: project tempo " << ProjectTimeSignature::Get(*au3Project).GetTempo()
               << ", keylock " << mixtape::KeylockEnabled() << ", speed curve" << (curve ? knots : std::string(" none"));
        for (const auto& track : arrangement->tracks) {
            for (const auto& clip : track.clips) {
                LOGI() << "Mixtape engine: track " << track.trackId << " clip " << clip.playStart << "-" << clip.playEnd
                       << " s, stretch " << clip.stretchRatio << ", rate " << clip.sampleRate;
            }
        }
    }
    return true;
}

void MixtapePlayer::play(std::optional<muse::secs_t> startTime)
{
    if (m_status.val == PlaybackStatus::Paused) {
        resume();
        return;
    }
    const auto project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    auto* au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    const double tracksEnd = TrackList::Get(*au3Project).GetEndTime();
    const PlaybackRegion region = m_regions->playbackRegion();
    const double start = startTime ? startTime->to_double() : region.start.to_double();
    if (startAt(start, tracksEnd)) {
        m_followTracksEnd = true;
        const PlaybackRegion loop = m_regions->loopRegion();
        m_transport->SetLoop(m_regions->isLoopRegionActive(), loop.start.to_double(), loop.end.to_double());
        setStatus(PlaybackStatus::Running);
    }
}

void MixtapePlayer::playRange(const PlaybackRegion& range)
{
    if (range.isValid() && startAt(range.start.to_double(), range.end.to_double())) {
        m_followTracksEnd = false;
        setStatus(PlaybackStatus::Running);
    }
}

muse::Ret MixtapePlayer::playTracks(TrackList&, double startTime, double endTime, const PlayTracksOptions&)
{
    if (!startAt(startTime, endTime)) {
        return muse::make_ret(muse::Ret::Code::InternalError);
    }
    m_followTracksEnd = false;
    setStatus(PlaybackStatus::Running);
    return muse::make_ok();
}

void MixtapePlayer::seek(const muse::secs_t newPosition, bool applyIfPlaying)
{
    const double pos = std::max(0.0, newPosition.to_double());
    m_regions->seek(pos, false);
    if (applyIfPlaying && m_status.val != PlaybackStatus::Stopped) {
        // GlideMix: after a jump, play on to the end of the mix -- a range
        // being played (a selection) would otherwise stop at its old end.
        // (0 = no end until the next position update sets the mix's end.)
        m_followTracksEnd = true;
        m_transport->SetEndTime(0.0);
        m_transport->Seek(pos);
    }
    m_position.set(pos);
}

void MixtapePlayer::rewind()
{
    seek(0.0);
}

void MixtapePlayer::stop()
{
    if (m_status.val == PlaybackStatus::Stopped) {
        return;
    }
    stopTransport();
    setStatus(PlaybackStatus::Stopped);
}

void MixtapePlayer::pause()
{
    if (m_status.val != PlaybackStatus::Running) {
        return;
    }
    m_transport->SetPaused(true);
    setStatus(PlaybackStatus::Paused);
}

void MixtapePlayer::resume()
{
    if (m_status.val != PlaybackStatus::Paused) {
        return;
    }
    m_transport->SetPaused(false);
    setStatus(PlaybackStatus::Running);
}

bool MixtapePlayer::isRunning() const
{
    return m_status.val == PlaybackStatus::Running;
}

PlaybackStatus MixtapePlayer::playbackStatus() const
{
    return m_status.val;
}

muse::async::Channel<PlaybackStatus> MixtapePlayer::playbackStatusChanged() const
{
    return m_status.ch;
}

muse::ValNt<bool> MixtapePlayer::reachedEnd() const
{
    return m_reachedEnd;
}

void MixtapePlayer::updatePlaybackPosition()
{
    if (!globalContext()->currentProject()) {
        stopTransport();
        return;
    }
    if (!m_transport->IsActive()) {
        return;
    }
    // Edits land in the arrangement the render thread picks up next block.
    publishArrangement();
    if (m_followTracksEnd) {
        auto* au3Project = reinterpret_cast<au::au3::Au3Project*>(globalContext()->currentProject()->au3ProjectPtr());
        m_transport->SetEndTime(TrackList::Get(*au3Project).GetEndTime());
    }
    const PlaybackRegion loop = m_regions->loopRegion();
    const bool looping = m_regions->isLoopRegionActive();
    m_transport->SetLoop(looping, loop.start.to_double(), loop.end.to_double());
    m_position.set(m_transport->Position());
    if (!looping && m_transport->ReachedEnd()) {
        stop();
        m_reachedEnd.set(true);
    }
}

muse::secs_t MixtapePlayer::playbackPosition() const
{
    return m_position.val;
}

muse::async::Channel<muse::secs_t> MixtapePlayer::playbackPositionChanged() const
{
    return m_position.ch;
}

// Regions: Au3Player's bookkeeping.
PlaybackRegion MixtapePlayer::playbackRegion() const { return m_regions->playbackRegion(); }
void MixtapePlayer::setPlaybackRegion(const PlaybackRegion& region) { m_regions->setPlaybackRegion(region); }
muse::async::Notification MixtapePlayer::loopRegionChanged() const { return m_regions->loopRegionChanged(); }
PlaybackRegion MixtapePlayer::loopRegion() const { return m_regions->loopRegion(); }
void MixtapePlayer::loopEditingBegin() { m_regions->loopEditingBegin(); }
void MixtapePlayer::loopEditingEnd() { m_regions->loopEditingEnd(); }
void MixtapePlayer::setLoopRegion(const PlaybackRegion& region) { m_regions->setLoopRegion(region); }
void MixtapePlayer::setLoopRegionStart(const muse::secs_t time) { m_regions->setLoopRegionStart(time); }
void MixtapePlayer::setLoopRegionEnd(const muse::secs_t time) { m_regions->setLoopRegionEnd(time); }
void MixtapePlayer::clearLoopRegion() { m_regions->clearLoopRegion(); }
bool MixtapePlayer::isLoopRegionClear() const { return m_regions->isLoopRegionClear(); }
void MixtapePlayer::setLoopRegionActive(const bool active) { m_regions->setLoopRegionActive(active); }
bool MixtapePlayer::isLoopRegionActive() const { return m_regions->isLoopRegionActive(); }

std::function<void(float* const*, size_t, double, double)> MixtapePlayer::metronomeOverlay(double sampleRate, size_t channels)
{
    // The metronome on the project's beats (the toolbar's metronome and its
    // level), placed like Au3MetronomeSequence: each click starts on a beat of
    // the timeline and plays at real-time speed whatever the tempo lane does.
    const std::shared_ptr<IMetronome> clicks = metronome.get();
    const auto project = globalContext()->currentProject();
    if (!clicks || !project) {
        return {};
    }
    auto* au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    clicks->prepare(static_cast<int>(std::lround(sampleRate)));
    const ProjectTimeSignature& signature = ProjectTimeSignature::Get(*au3Project);
    const double rate = sampleRate;
    return [clicks, &signature, rate, channels](float* const* out, size_t frames, double t0, double t1) {
        const IMetronome::Clicks* set = clicks->clicks();
        const double tempo = signature.GetTempo();
        const int upper = signature.GetUpperTimeSignature();
        const int lower = signature.GetLowerTimeSignature();
        if (!set || !set->enabled || set->beat.empty() || tempo <= 0.0 || upper <= 0 || lower <= 0 || frames == 0) {
            return;
        }
        const double beatSec = (60.0 / tempo) * (4.0 / lower);
        const double speed = std::max(1e-6, (t1 - t0) / (frames / rate));   // timeline seconds per real second
        for (size_t i = 0; i < frames; ++i) {
            const double t = t0 + (t1 - t0) * (static_cast<double>(i) / frames);
            if (t < 0.0) {
                continue;
            }
            const auto beat = static_cast<int64_t>(std::floor(t / beatSec + 1e-9));
            const double offset = (t - beat * beatSec) / speed * rate;
            const std::vector<float>& click = beat % upper == 0 ? set->accent : set->beat;
            const auto index = static_cast<size_t>(offset);
            if (index + 1 >= click.size()) {
                continue;
            }
            const float frac = static_cast<float>(offset - index);
            const float value = (click[index] + (click[index + 1] - click[index]) * frac) * set->gain;
            for (size_t c = 0; c < channels; ++c) {
                out[c][i] = std::clamp(out[c][i] + value, -1.0f, 1.0f);
            }
        }
    };
}

// ---- performance (MixMeister Perform)

void MixtapePlayer::togglePerform()
{
    if (!m_performActive) {
        startPerformance();
    } else if (m_performPaused) {
        m_perform->SetPaused(false);
        m_performPaused = false;
        m_performState.notify();
    } else {
        m_perform->SetPaused(true);
        m_performPaused = true;
        m_performState.notify();
    }
}

bool MixtapePlayer::startPerformance()
{
    const auto project = globalContext()->currentProject();
    if (!project) {
        return false;
    }
    auto* au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    // The audience gets the effects and the meters: a preview that has them
    // stops first (it restarts dry on the headphones).
    if (m_status.val != PlaybackStatus::Stopped) {
        stop();
    }
    if (!m_perform) {
        m_perform = std::make_unique<mixtape::Transport>();
        m_performTimer.setInterval(16);
        m_performTimer.setTimerType(Qt::PreciseTimer);
        m_performTimer.callOnTimeout([this]() { updatePerformance(); });
    }
    mixtape::Transport::Options options;
    options.sampleRate = ProjectRate::Get(*au3Project).GetRate();
    options.channels = 2;
    // Rendered ahead of the device: more on small machines (4 threads or
    // fewer), where a busy moment otherwise reached the speakers as a crackle.
    options.readAheadSeconds = std::thread::hardware_concurrency() > 0 && std::thread::hardware_concurrency() <= 4 ? 0.25 : 0.08;
    options.hostName = AudioIOHost.Read().ToStdString();
#ifdef Q_OS_WIN
    // Windows: the buffer Preferences sets (100 ms by default). macOS keeps
    // the device's lowest, which plays cleanly there.
    options.latencySeconds = AudioIOLatencyDuration.Read() / 1000.0;
#endif
    options.deviceName = AudioIOPlaybackDevice.Read().ToStdString();
    options.endTime = TrackList::Get(*au3Project).GetEndTime();
    options.channelOffset = 0;   // the audience: outputs 1-2
    //! Its own meter, not the playback one: Play (the headphone cue) can run at
    //! the same time as Perform, and sharing one sender would sum both mixes
    //! into a single reading. Perform feeds the top bar, the cue the side bar.
    options.meter = ProjectAudioIO::Get(*au3Project).GetPerformMeter();
    if (options.meter) {
        options.meter->start(options.sampleRate);
        m_performMeter = options.meter;
    }
    m_performEffects = std::make_unique<mixtape::EffectsHost>(au3Project->shared_from_this(), options.sampleRate,
                                                              static_cast<unsigned>(options.channels),
                                                              mixtape::kRenderBlockFrames);
    const auto arrangement = mixtape::SnapshotArrangement(TrackList::Get(*au3Project), ++m_generation);
    prepareEffects(m_performEffects.get(), m_performSignatures, arrangement);
    m_performEffects->MeasureLatencies(*arrangement);
    const double start = std::max(0.0, m_performPosition.val.to_double());
    if (!m_perform->Start(options, arrangement, start, m_performEffects.get())) {
        LOGE() << "Mixtape performance could not start: " << m_perform->LastError();
        stopPerformance();
        return false;
    }
    m_performActive = true;
    m_performUnderrunsLogged = m_perform->Underruns();
    m_performStatTicks = 0;
    m_performPaused = false;
    s_enginePlaying = true;
    m_performTimer.start();
    LOGI() << "Mixtape performance from " << start << " s on outputs " << (m_perform->ChannelOffset() + 1) << "-"
           << (m_perform->ChannelOffset() + 2);
    m_performState.notify();
    return true;
}

void MixtapePlayer::stopPerformance()
{
    m_performTimer.stop();
    if (m_perform) {
        m_perform->Stop();
    }
    if (const auto meter = std::exchange(m_performMeter, nullptr)) {
        meter->stop();
    }
    m_performEffects.reset();
    m_performSignatures.clear();
    const bool changed = m_performActive;
    m_performActive = false;
    m_performPaused = false;
    s_enginePlaying = m_transport && m_transport->IsActive();
    if (changed) {
        m_performState.notify();
    }
}

void MixtapePlayer::resetPerformance()
{
    stopPerformance();
    m_performPosition.set(0.0);
    // The headphone preview goes back to outputs 1-2 on its next play.
    m_performState.notify();
}

std::vector<std::string> MixtapePlayer::outputDevices() const
{
    return mixtape::Transport::OutputDevices();
}

std::string MixtapePlayer::headphonesDevice() const
{
    return muse::settings()->value(kHeadphonesDeviceKey).toString();
}

void MixtapePlayer::setHeadphonesDevice(const std::string& device)
{
    muse::settings()->setSharedValue(kHeadphonesDeviceKey, muse::Val(device));
    LOGI() << "Mixtape headphones: " << (device.empty() ? std::string("outputs 3-4") : device);
    m_performState.notify();
}

void MixtapePlayer::setPerformancePosition(double position)
{
    position = std::max(0.0, position);
    if (m_performActive && !m_performPaused) {
        return;   // the audience is listening
    }
    if (m_performActive) {
        m_perform->Seek(position);
    }
    m_performPosition.set(position);
}

void MixtapePlayer::updatePerformance()
{
    if (!m_perform || !m_perform->IsActive()) {
        return;
    }
    const auto project = globalContext()->currentProject();
    if (!project) {
        stopPerformance();
        return;
    }
    // Keep the arrangement fresh when the preview isn't doing it already.
    if (!m_transport->IsActive()) {
        publishArrangement();
    }
    auto* au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    m_perform->SetEndTime(TrackList::Get(*au3Project).GetEndTime());
    // GlideMix: dropouts, with what the render thread was doing, every ~2 s.
    if (++m_performStatTicks >= 125) {
        m_performStatTicks = 0;
        const uint64_t underruns = m_perform->Underruns();
        const double slowest = m_perform->TakeSlowestRenderMs();
        if (underruns != m_performUnderrunsLogged) {
            LOGW() << "Perform dropouts: " << (underruns - m_performUnderrunsLogged) << " at " << m_performPosition.val.to_double()
                   << " s; slowest block " << slowest << " ms (a block is "
                   << 1000.0 * mixtape::kRenderBlockFrames / ProjectRate::Get(*au3Project).GetRate()
                   << " ms); buffered " << m_perform->BufferedMs() << " ms";
            m_performUnderrunsLogged = underruns;
        }
    }
    m_performPosition.set(m_perform->Position());
    if (m_perform->ReachedEnd()) {
        stopPerformance();
    }
}
