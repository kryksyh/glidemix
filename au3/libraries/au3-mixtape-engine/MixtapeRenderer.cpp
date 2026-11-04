/*
 * GlideMix -- playback engine
 */
#include "MixtapeRenderer.h"
#include "MixtapeDemo.h"
#include "MixtapeLoudness.h"

#include <thread>
#include <condition_variable>
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <cmath>

#include "MixtapeClipVoice.h"
#include "MixtapeEffects.h"

#include "au3-mixer/PlaybackSpeedCurve.h"

#include "au3-wave-track/WaveTrack.h"

namespace mixtape {
namespace {
std::atomic<float> s_audienceGain{ 1.0f };
std::atomic<float> s_cueGain{ 1.0f };

//! Guarded because it is replaced from the GUI thread while the render thread
//! reads it; the render thread takes a copy of the shared_ptr, so a tap being
//! removed cannot be destroyed underneath a block that is using it.
std::mutex s_tapMutex;
std::shared_ptr<Renderer::OutputTap> s_outputTap;

std::shared_ptr<Renderer::OutputTap> outputTap()
{
    std::lock_guard<std::mutex> lock(s_tapMutex);
    return s_outputTap;
}
}

void Renderer::SetOutputTap(OutputTap tap)
{
    std::lock_guard<std::mutex> lock(s_tapMutex);
    s_outputTap = tap ? std::make_shared<OutputTap>(std::move(tap)) : nullptr;
}

void Renderer::SetAudienceGain(float gain)
{
    s_audienceGain.store(gain);
}

void Renderer::SetCueGain(float gain)
{
    s_cueGain.store(gain);
}

float Renderer::AudienceGain()
{
    return s_audienceGain.load();
}

float Renderer::CueGain()
{
    return s_cueGain.load();
}

namespace {
// -1.05 dBTP: room for MP3 and AAC encoders, and a hair under -1 so a meter
// (ours, ffmpeg's) reads -1.0 or less.
constexpr float kLimitCeiling = 0.886f;
constexpr double kLimitReleaseSec = 0.15;
constexpr double kLimitLookaheadSec = 0.0015;
// The true-peak estimate is the loudness meter's own (8x), over
// the kTpTaps frames ending kTpHalf - 1 frames after the judged one.
constexpr int kTpTaps = TruePeakFilter::kTaps;
constexpr int kTpHalf = kTpTaps / 2;
}

Renderer::Renderer(double outputRate, size_t outputChannels)
    : mOutputRate{outputRate}
    , mOutputChannels{outputChannels}
    , mLimitCeiling{kLimitCeiling}
{
    Limiter& l = mLimiter;
    l.release = static_cast<float>(std::exp(-1.0 / (kLimitReleaseSec * outputRate)));
    l.lookahead = std::max<size_t>(8, static_cast<size_t>(std::lround(kLimitLookaheadSec * outputRate)));
    // A frame is judged kTpHalf frames after it comes in (the true-peak
    // filter needs the ones after it), and its gain is ready lookahead - 1
    // frames after that.
    l.delay = static_cast<size_t>(kTpHalf) + l.lookahead - 1;
    l.input.assign(outputChannels, std::vector<float>(l.delay + kTpHalf + 1, 0.0f));
    l.required.assign(l.lookahead, 1.0f);
    l.box.assign(l.lookahead, 1.0f);
    l.boxSum = static_cast<double>(l.lookahead);
    l.minQueue.reserve(l.lookahead + 1);
}

// The largest magnitude around input frame `position` (a ring index): the
// sample, and the meter's eight points between it and its neighbours.
float Renderer::TruePeakAt(size_t position) const
{
    const Limiter& l = mLimiter;
    const size_t size = l.input[0].size();
    const auto& phases = TruePeakFilter::Phases();
    float peak = 0.0f;
    for (size_t c = 0; c < mOutputChannels; ++c) {
        const auto& ring = l.input[c];
        peak = std::max(peak, std::abs(ring[position]));
        // Quiet stretches can't reach the ceiling between samples: skip the
        // filter there (most of a mix), it is the costly part.
        float around = 0.0f;
        for (int k = 0; k < kTpTaps; ++k) {
            around = std::max(around, std::abs(ring[(position + size + static_cast<size_t>(k) - static_cast<size_t>(kTpHalf)) % size]));
        }
        if (around < 0.5f * mLimitCeiling) {
            continue;
        }
        for (const auto& phase : phases) {
            float sum = 0.0f;
            for (int k = 0; k < kTpTaps; ++k) {
                // Frames position - kTpHalf .. position + kTpHalf - 1, oldest first.
                const size_t at = (position + size + static_cast<size_t>(k) - static_cast<size_t>(kTpHalf)) % size;
                sum += phase[static_cast<size_t>(k)] * ring[at];
            }
            peak = std::max(peak, std::abs(sum));
        }
    }
    return peak;
}

void Renderer::Limit(float* const* out, size_t frames)
{
    Limiter& l = mLimiter;
    const size_t size = l.input[0].size();
    for (size_t i = 0; i < frames; ++i) {
        for (size_t c = 0; c < mOutputChannels; ++c) {
            l.input[c][l.write] = out[c][i];
        }
        // The frame kTpHalf back now has the frames after it: its true peak,
        // and the gain it needs.
        const size_t judged = (l.write + size - kTpHalf) % size;
        const float peak = TruePeakAt(judged);
        const float needed = peak > mLimitCeiling ? mLimitCeiling / peak : 1.0f;

        // The smallest gain needed over the look-ahead window...
        const size_t n = l.index++;
        auto& q = l.minQueue;
        while (q.size() > l.minHead && q.back().second >= needed) {
            q.pop_back();
        }
        q.emplace_back(n, needed);
        while (q[l.minHead].first + l.lookahead <= n) {
            ++l.minHead;
        }
        if (l.minHead > l.lookahead) {   // keep the vector small
            q.erase(q.begin(), q.begin() + static_cast<std::ptrdiff_t>(l.minHead));
            l.minHead = 0;
        }
        const float windowMin = q[l.minHead].second;
        // ...averaged over the same window: the gain glides down over 1.5 ms
        // and is at or below what each frame needs when that frame leaves.
        const size_t slot = n % l.lookahead;
        l.boxSum += static_cast<double>(windowMin) - l.box[slot];
        l.box[slot] = windowMin;
        const float target = static_cast<float>(std::min(1.0, l.boxSum / static_cast<double>(l.lookahead)));
        // Down as the look-ahead says; back up slowly.
        l.gain = target < l.gain ? target : target - (target - l.gain) * l.release;

        const size_t leaving = (l.write + size - l.delay) % size;
        for (size_t c = 0; c < mOutputChannels; ++c) {
            out[c][i] = l.input[c][leaving] * l.gain;
        }
        l.write = (l.write + 1) % size;
    }
}

//! GlideMix: exporting has no deadline and no clip depends on another until
//! the mix, so each clip's voice renders ahead on a core of its own, in timeline
//! order, instead of waiting for the playhead to reach it. A thread only exists
//! while clips are left to render. The render thread takes finished audio out
//! block by block, so effects, latency, faders and the limiter run as always
//! and the file comes out sample for sample the same.
struct Renderer::OfflineVoices
{
    //! What may wait in memory for the mix; a clip the mix needs now always starts.
    static constexpr size_t kMemoryBudget = 1536u * 1024u * 1024u;

    struct Clip {
        const ClipSnapshot* clip = nullptr;
        size_t firstBlock = 0;
        size_t lastBlock = 0;
        size_t bytes = 0;
        bool claimed = false;
        std::atomic<size_t> blocksDone{ 0 };
        std::vector<std::vector<float> > planes;
    };

    OfflineVoices(std::shared_ptr<const Arrangement> arrangement_, std::shared_ptr<const PlaybackSpeedCurve> curve_,
                  double rate_, size_t channels_, size_t frames_, double start)
        : arrangement(std::move(arrangement_)), curve(std::move(curve_)), rate(rate_), channels(channels_), frames(frames_)
    {
        double end = start;
        for (const TrackSnapshot& track : arrangement->tracks) {
            for (const ClipSnapshot& clip : track.clips) {
                end = std::max(end, clip.playEnd);
            }
        }
        for (double t = start; t < end;) {
            const double next = curve ? curve->SolveIntegralOfInverse(t, frames / rate) : t + frames / rate;
            if (!(next > t)) {
                break;
            }
            spans.push_back({ t, next });
            t = next;
        }
        for (const TrackSnapshot& track : arrangement->tracks) {
            for (const ClipSnapshot& snapshot : track.clips) {
                auto entry = std::make_unique<Clip>();
                entry->clip = &snapshot;
                bool any = false;
                for (size_t b = 0; b < spans.size(); ++b) {
                    if (snapshot.playEnd > spans[b].first && snapshot.playStart < spans[b].second) {
                        entry->firstBlock = any ? entry->firstBlock : b;
                        entry->lastBlock = b;
                        any = true;
                    }
                }
                if (any) {
                    entry->bytes = (entry->lastBlock - entry->firstBlock + 1) * frames * channels * sizeof(float);
                    byClip[&snapshot] = entry.get();
                    clips.push_back(std::move(entry));
                }
            }
        }
        std::stable_sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) { return a->firstBlock < b->firstBlock; });
        const size_t cores = std::max<size_t>(std::thread::hardware_concurrency(), 2);
        const size_t threads = std::min(cores - 1, clips.size());
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] { Work(); });
        }
    }

    ~OfflineVoices()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        wake.notify_all();
        for (auto& worker : workers) {
            worker.join();
        }
    }

    void Work()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stop) {
            const auto next = std::find_if(clips.begin(), clips.end(), [](const auto& c) { return !c->claimed; });
            if (next == clips.end()) {
                return;
            }
            Clip& entry = **next;
            if (memoryUsed > 0 && memoryUsed + entry.bytes > kMemoryBudget && entry.firstBlock > mixBlock) {
                wake.wait(lock);
                continue;
            }
            entry.claimed = true;
            memoryUsed += entry.bytes;
            lock.unlock();
            RenderClip(entry);
            lock.lock();
        }
    }

    void RenderClip(Clip& entry)
    {
        const size_t blocks = entry.lastBlock - entry.firstBlock + 1;
        entry.planes.assign(channels, std::vector<float>(blocks * frames, 0.0f));
        ClipVoice voice(rate, channels);
        voice.SetPreferResample(true);
        {
            // Rubber Band plans its FFTs when created: one at a time.
            static std::mutex creating;
            std::lock_guard<std::mutex> lock(creating);
            voice.Assign(*entry.clip, curve != nullptr);
        }
        std::vector<float*> ptrs(channels);
        const float unity[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        for (size_t b = 0; b < blocks; ++b) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (stop) {
                    return;
                }
            }
            const auto [t0, t1] = spans[entry.firstBlock + b];
            const double span = t1 - t0;
            const auto frameAt = [&](double t) {
                return span > 0 ? std::clamp<int64_t>(std::llround((t - t0) / span * frames), 0, static_cast<int64_t>(frames)) : 0;
            };
            const int64_t firstFrame = frameAt(std::max(t0, entry.clip->playStart));
            const int64_t endFrame = frameAt(std::min(t1, entry.clip->playEnd));
            if (endFrame > firstFrame) {
                for (size_t o = 0; o < channels; ++o) {
                    ptrs[o] = entry.planes[o].data() + b * frames + firstFrame;
                }
                voice.RenderAdd(ptrs.data(), static_cast<size_t>(endFrame - firstFrame),
                                t0 + span * firstFrame / frames, t0 + span * endFrame / frames, unity, curve.get());
            }
            entry.blocksDone.store(b + 1, std::memory_order_release);
            done.notify_all();
        }
    }

    //! Render thread: adds this clip's part of block `block` to `buffers`.
    void Take(const ClipSnapshot* clip, size_t block, std::vector<std::vector<float> >& buffers)
    {
        const auto found = byClip.find(clip);
        if (found == byClip.end() || block < found->second->firstBlock || block > found->second->lastBlock) {
            return;
        }
        Clip& entry = *found->second;
        const size_t index = block - entry.firstBlock;
        if (entry.blocksDone.load(std::memory_order_acquire) <= index) {
            std::unique_lock<std::mutex> lock(mutex);
            mixBlock = block;
            wake.notify_all();
            done.wait(lock, [&] { return entry.blocksDone.load(std::memory_order_acquire) > index; });
        }
        for (size_t o = 0; o < channels; ++o) {
            const float* src = entry.planes[o].data() + index * frames;
            float* dst = buffers[o].data();
            for (size_t i = 0; i < frames; ++i) {
                dst[i] += src[i];
            }
        }
        if (block == entry.lastBlock) {
            entry.planes.clear();
            entry.planes.shrink_to_fit();
            {
                std::lock_guard<std::mutex> lock(mutex);
                memoryUsed -= entry.bytes;
            }
            wake.notify_all();
        }
    }

    void BlockStarted(size_t block)
    {
        std::lock_guard<std::mutex> lock(mutex);
        mixBlock = block;
    }

    const std::shared_ptr<const Arrangement> arrangement;
    const std::shared_ptr<const PlaybackSpeedCurve> curve;
    const double rate;
    const size_t channels;
    const size_t frames;
    std::vector<std::pair<double, double> > spans;   //!< each block's timeline span
    std::vector<std::unique_ptr<Clip> > clips;       //!< earliest first
    std::map<const ClipSnapshot*, Clip*> byClip;
    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable wake;   //!< memory freed, or the mix waits for a clip
    std::condition_variable done;   //!< a block finished
    size_t memoryUsed = 0;
    size_t mixBlock = 0;
    size_t nextBlock = 0;           //!< render thread only
    bool stop = false;
};

Renderer::~Renderer() = default;

void Renderer::SetLimitCeiling(float ceiling)
{
    mLimitCeiling = ceiling;
}

void Renderer::SetArrangement(std::shared_ptr<const Arrangement> arrangement)
{
    std::atomic_store_explicit(&mPending, std::move(arrangement), std::memory_order_release);
}

std::shared_ptr<const Arrangement> Renderer::CurrentArrangement() const
{
    return std::atomic_load_explicit(&mPending, std::memory_order_acquire);
}

std::pair<double, double> Renderer::Render(float* const* out, size_t frames)
{
    for (size_t o = 0; o < mOutputChannels; ++o) {
        std::fill_n(out[o], frames, 0.0f);
    }
    mCurrent = std::atomic_load_explicit(&mPending, std::memory_order_acquire);
    // The timeline span this block plays: the tempo curve's speed decides
    // how far the playhead gets in frames / rate seconds of real time.
    auto curve = mUseLiveCurve ? PlaybackSpeedCurve::Live() : mFixedCurve;
    if (curve && std::all_of(curve->knots.begin(), curve->knots.end(),
                             [](const auto& knot) { return std::abs(knot.second - 1.0) < 1e-9; })) {
        curve.reset();   // a flat line at the project tempo: clips stay bit-exact
    }
    const double t0 = mTime;
    const double t1 = curve ? curve->SolveIntegralOfInverse(t0, frames / mOutputRate) : t0 + frames / mOutputRate;
    const double span = t1 - t0;
    // Frame index where timeline time t falls in this block (linear within the block).
    const auto frameAt = [&](double t) {
        return span > 0 ? std::clamp<int64_t>(std::llround((t - t0) / span * frames), 0, static_cast<int64_t>(frames)) : 0;
    };
    const bool alwaysStretch = curve != nullptr;

    std::map<VoiceKey, bool> alive;
    if (mEffects) {
        mEffects->BeginBlock();
    }
    if (mCurrent) {
        const auto& tracks = mCurrent->tracks;
        if (mTrackBuffers.size() < tracks.size()) {
            mTrackBuffers.resize(tracks.size());
        }
        if (mOffline && !mOfflineVoices) {
            mOfflineVoices = std::make_unique<OfflineVoices>(mCurrent, curve, mOutputRate, mOutputChannels, frames, t0);
        }
        // Stage 1 (render thread): which clips play in this block; voices are
        // created or re-assigned here so the map isn't touched in parallel.
        mJobs.clear();
        for (size_t ti = 0; ti < tracks.size(); ++ti) {
            const TrackSnapshot& track = tracks[ti];
            auto& buffers = mTrackBuffers[ti];
            if (buffers.size() != mOutputChannels || buffers.front().size() < frames) {
                buffers.assign(mOutputChannels, std::vector<float>(std::max<size_t>(frames, 4096)));
            }
            TrackJob job;
            job.track = ti;
            for (const ClipSnapshot& clip : track.clips) {
                if (clip.playEnd <= t0 || clip.playStart >= t1) {
                    continue;
                }
                if (mOfflineVoices) {
                    job.voices.push_back({ nullptr, &clip });   // already rendering ahead
                    continue;
                }
                const VoiceKey key{ track.trackId, clip.clipId };
                alive[key] = true;
                auto& voice = mVoices[key];
                if (!voice) {
                    voice = std::make_unique<ClipVoice>(mOutputRate, mOutputChannels);
                    voice->Assign(clip, alwaysStretch);
                } else if (!voice->Clip().SameAudio(clip) || voice->AlwaysStretch() != alwaysStretch) {
                    voice->Assign(clip, alwaysStretch);
                } else {
                    // Same audio, so the voice plays on; its volume and bass lines may have been edited.
                    voice->UpdateLines(clip);
                }
                job.voices.push_back({ voice.get(), &clip });
            }
            mJobs.push_back(std::move(job));
        }

        // Stage 2 (in parallel across tracks): the clip voices, the costly part.
        const auto renderTrack = [&](size_t j) {
            const TrackJob& job = mJobs[j];
            auto& buffers = mTrackBuffers[job.track];
            for (size_t o = 0; o < mOutputChannels; ++o) {
                std::fill_n(buffers[o].data(), frames, 0.0f);
            }
            std::vector<float*> ptrs(mOutputChannels);
            const float unity[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
            for (const auto& [voice, clip] : job.voices) {
                // The part of this block inside the clip, in whole frames.
                const int64_t firstFrame = frameAt(std::max(t0, clip->playStart));
                const int64_t endFrame = frameAt(std::min(t1, clip->playEnd));
                if (endFrame <= firstFrame) {
                    continue;
                }
                for (size_t o = 0; o < mOutputChannels; ++o) {
                    ptrs[o] = buffers[o].data() + firstFrame;
                }
                voice->RenderAdd(ptrs.data(), static_cast<size_t>(endFrame - firstFrame),
                                 t0 + span * firstFrame / frames, t0 + span * endFrame / frames, unity, curve.get());
            }
        };
        if (mOfflineVoices) {
            const size_t block = mOfflineVoices->nextBlock++;
            mOfflineVoices->BlockStarted(block);
            for (const TrackJob& job : mJobs) {
                auto& buffers = mTrackBuffers[job.track];
                for (size_t o = 0; o < mOutputChannels; ++o) {
                    std::fill_n(buffers[o].data(), frames, 0.0f);
                }
                for (const auto& voice : job.voices) {
                    mOfflineVoices->Take(voice.second, block, buffers);
                }
            }
        }
        // Worth the hand-off only for tracks running the stretcher: waking the
        // pool every 256 frames cost more than resampled or untouched clips
        // themselves (four threads half busy on a 2-core PC for 3 tracks).
        size_t busy = 0;
        for (const TrackJob& job : mOfflineVoices ? std::vector<TrackJob> {} : mJobs) {
            busy += std::any_of(job.voices.begin(), job.voices.end(),
                                [](const auto& v) { return v.first->Stretches(); }) ? 1 : 0;
        }
        if (mOfflineVoices) {
            // Stage 2 ran on the voices' own threads.
        } else if (busy > 1) {
            mPool.Run(mJobs.size(), renderTrack);
        } else {
            for (size_t j = 0; j < mJobs.size(); ++j) {
                renderTrack(j);
            }
        }

        // Stage 3 (render thread, in track order): effects, latency
        // compensation, faders, the mix.
        size_t maxLatency = 0;
        for (const TrackSnapshot& track : tracks) {
            maxLatency = std::max(maxLatency, track.latencyFrames);
        }
        // The master effects' latency and the limiter's look-ahead come on top:
        // the playhead and exports shift by all of it, so nothing starts late.
        mOutputDelay = maxLatency + mCurrent->masterLatencyFrames + mLimiter.delay;
        mTrackPtrs.resize(mOutputChannels);
        for (const TrackJob& job : mJobs) {
            const TrackSnapshot& track = tracks[job.track];
            auto& buffers = mTrackBuffers[job.track];
            const bool audible = !track.mute && (!mCurrent->anySolo || track.solo);
            const bool anyClip = !job.voices.empty();
            for (size_t o = 0; o < mOutputChannels; ++o) {
                mTrackPtrs[o] = buffers[o].data();
            }
            // Effects run every block, clip or not, so tails ring on.
            const bool effects = mEffects && track.track;
            if (effects) {
                mEffects->Process(track.track.get(), mTrackPtrs.data(), frames, t0, t1);
            }
            // Effects delay their track by their latency: hold every other
            // track back to match, so beats line up across tracks.
            const size_t hold = maxLatency - std::min(maxLatency, track.latencyFrames);
            DelayLine& delay = mDelays[track.trackId];
            if (hold == 0) {
                delay.frames = 0;
            } else {
                if (delay.frames != hold || delay.buffer.size() != mOutputChannels) {
                    delay.frames = hold;
                    delay.position = 0;
                    delay.buffer.assign(mOutputChannels, std::vector<float>(hold, 0.0f));
                }
                for (size_t o = 0; o < mOutputChannels; ++o) {
                    float* data = buffers[o].data();
                    std::vector<float>& line = delay.buffer[o];
                    size_t pos = delay.position;
                    for (size_t i = 0; i < frames; ++i) {
                        std::swap(data[i], line[pos]);
                        pos = pos + 1 == hold ? 0 : pos + 1;
                    }
                }
                delay.position = (delay.position + frames) % hold;
            }
            if (!anyClip && !effects && hold == 0) {
                if (mTrackSink) {
                    mTrackSink(track.trackId, mTrackPtrs.data(), frames);
                }
                continue;
            }
            for (size_t o = 0; o < mOutputChannels; ++o) {
                const float gain = audible ? track.ChannelGain(o) : 0.0f;
                float* src = buffers[o].data();
                float* dst = out[o];
                for (size_t i = 0; i < frames; ++i) {
                    src[i] *= gain;
                    dst[i] += src[i];
                }
            }
            if (mTrackSink) {
                mTrackSink(track.trackId, mTrackPtrs.data(), frames);
            }
        }
    }
    if (mEffects) {
        mEffects->Process(nullptr, out, frames, t0, t1);   // master effects
        mEffects->EndBlock();
    }
    if (mOverlay) {
        // The tracks come out late by the latency compensation: so does the click.
        const double shift = frames > 0 ? mOutputDelay.load() * span / frames : 0.0;
        mOverlay(out, frames, t0 - shift, t1 - shift);
    }
    // Last of all, the level of this output: the audience's, or the
    // headphones' while performing. The click rides with it, as it does on a
    // DJ mixer's master.
    const float gain = mOffline ? 1.0f : mIsCue ? CueGain() : AudienceGain();
    if (gain != 1.0f) {
        for (size_t o = 0; o < mOutputChannels; ++o) {
            float* dst = out[o];
            for (size_t i = 0; i < frames; ++i) {
                dst[i] *= gain;
            }
        }
    }
    Limit(out, frames);
    // The demo version's voice tag: last, so Play, Perform, broadcast and export
    // all carry them.
    if (DemoMode()) {
        ApplyDemoVoice(out, mOutputChannels, frames, mDemoFrame, mOutputRate);
    }
    mDemoFrame += static_cast<int64_t>(frames);
    // What the listener hears is what goes out, level and all.
    if (const auto tap = mOffline ? nullptr : outputTap()) {
        (*tap)(mIsCue, out, mOutputChannels, frames, mOutputRate);
    }
    // Voices of clips that left the block are dropped; one that comes back
    // later starts again with a seek.
    for (auto it = mVoices.begin(); it != mVoices.end();) {
        it = alive.count(it->first) ? std::next(it) : mVoices.erase(it);
    }
    mTime = t1;
    return { t0, t1 };
}
}

namespace mixtape {
std::pair<double, double> Renderer::RenderLooped(float* const* out, size_t frames)
{
    const double start = mLoopStart.load();
    const double end = mLoopEnd.load();
    if (!mLoopActive.load() || mTime >= end + 1.0 || mTime < start - 1e-9) {
        // Not looping, or the playhead is outside the loop (a seek put it
        // there): play on normally.
        if (mLoopActive.load() && mTime >= end && mTime < end + 1.0) {
            mTime = start;
        }
        return Render(out, frames);
    }
    if (mTime >= end) {
        mTime = start;
    }
    const double t0 = mTime;
    // Render in pieces, wrapping to the loop start whenever the end is passed.
    size_t done = 0;
    std::vector<float*> ptrs(mOutputChannels);
    while (done < frames) {
        const size_t n = frames - done;
        for (size_t o = 0; o < mOutputChannels; ++o) {
            ptrs[o] = out[o] + done;
        }
        const auto [a, b] = Render(ptrs.data(), n);
        if (b <= end) {
            done += n;
            continue;
        }
        // Overshot the loop end: keep only the frames before it and redo the rest from the start.
        const size_t keep = std::clamp<size_t>(static_cast<size_t>((end - a) / (b - a) * n), 1, n);
        for (size_t o = 0; o < mOutputChannels; ++o) {
            std::fill(out[o] + done + keep, out[o] + frames, 0.0f);
        }
        done += keep;
        mTime = start;
    }
    return { t0, mTime };
}
}
