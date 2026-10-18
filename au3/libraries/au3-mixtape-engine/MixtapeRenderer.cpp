/*
 * GlideMix -- playback engine
 */
#include "MixtapeRenderer.h"
#include "MixtapeDemo.h"

#include <algorithm>
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
constexpr float kLimitCeiling = 0.891f;     // -1 dBFS
constexpr double kLimitReleaseSec = 0.15;
}

Renderer::Renderer(double outputRate, size_t outputChannels)
    : mOutputRate{outputRate}
    , mOutputChannels{outputChannels}
{
    mLimiter.release = static_cast<float>(std::exp(-1.0 / (kLimitReleaseSec * outputRate)));
}

void Renderer::Limit(float* const* out, size_t frames)
{
    Limiter& l = mLimiter;
    for (size_t i = 0; i < frames; ++i) {
        float peak = 0.0f;
        for (size_t c = 0; c < mOutputChannels; ++c) {
            peak = std::max(peak, std::abs(out[c][i]));
        }
        const float needed = peak * l.gain > kLimitCeiling ? kLimitCeiling / peak : 1.0f;
        // Down at once when a peak would pass the ceiling; back up slowly.
        l.gain = needed < l.gain ? needed : 1.0f - (1.0f - l.gain) * l.release;
        if (l.gain < 1.0f) {
            for (size_t c = 0; c < mOutputChannels; ++c) {
                out[c][i] *= l.gain;
            }
        }
    }
}

Renderer::~Renderer() = default;

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
        // Worth the hand-off only for tracks running the stretcher: waking the
        // pool every 256 frames cost more than resampled or untouched clips
        // themselves (four threads half busy on a 2-core PC for 3 tracks).
        size_t busy = 0;
        for (const TrackJob& job : mJobs) {
            busy += std::any_of(job.voices.begin(), job.voices.end(),
                                [](const auto& v) { return v.first->Stretches(); }) ? 1 : 0;
        }
        if (busy > 1) {
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
        mOutputDelay = maxLatency;
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
    // The demo version's gaps: last, so Play, Perform, broadcast and export
    // all carry them.
    if (DemoMode()) {
        ApplyDemoGaps(out, mOutputChannels, frames, mDemoFrame, mOutputRate, mOffline, mDemoNoise);
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
