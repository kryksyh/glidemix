/*
 * GlideMix -- playback engine
 */
#include "MixtapeTransport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "portaudio.h"
#ifdef _WIN32
#include "pa_win_wasapi.h"
#endif

#include "au3-audio-devices/IMeterSender.h"

namespace mixtape {
namespace {
constexpr size_t kRenderBlock = kRenderBlockFrames;
std::atomic<int> s_activeTransports{ 0 };

PaHostApiIndex FindHost(const std::string& host)
{
    for (PaHostApiIndex i = 0; !host.empty() && i < Pa_GetHostApiCount(); ++i) {
        const PaHostApiInfo* info = Pa_GetHostApiInfo(i);
        if (info && host == info->name) {
            return i;
        }
    }
    return -1;
}

//! The named output on the host Audacity plays through (`host`, e.g.
//! "Windows WASAPI"), else on any host; no name: that host's default output.
//! Without the host, "default" was PortAudio's default -- MME on Windows,
//! whatever Preferences said.
PaDeviceIndex FindOutputDevice(const std::string& name, const std::string& host = {})
{
    const PaHostApiIndex hostIndex = FindHost(host);
    if (!name.empty()) {
        const int count = Pa_GetDeviceCount();
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < count; ++i) {
                const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
                if (info && info->maxOutputChannels > 0 && name == info->name
                    && (pass == 1 || hostIndex < 0 || info->hostApi == hostIndex)) {
                    return i;
                }
            }
        }
    }
    if (const PaHostApiInfo* info = hostIndex >= 0 ? Pa_GetHostApiInfo(hostIndex) : nullptr) {
        if (info->defaultOutputDevice != paNoDevice) {
            return info->defaultOutputDevice;
        }
    }
    return Pa_GetDefaultOutputDevice();
}

int PaCallback(const void*, void* output, unsigned long frames, const PaStreamCallbackTimeInfo*,
               PaStreamCallbackFlags, void* user)
{
    return static_cast<Transport*>(user)->OnCallback(static_cast<float*>(output), frames);
}
}

bool AnyTransportActive()
{
    return s_activeTransports.load() > 0;
}

Transport::Transport() = default;

Transport::~Transport()
{
    Stop();
}

bool Transport::Start(const Options& options, std::shared_ptr<const Arrangement> arrangement, double startTime,
                      EffectsHost* effects)
{
    Stop();
    mOptions = options;
    mError.clear();
    mReachedEnd = false;
    mEndTime = options.endTime;
    mPaused = false;
    mUnderruns = 0;
    mStarted = false;

    mRenderer = std::make_unique<Renderer>(options.sampleRate, options.channels);
    mRenderer->SetArrangement(std::move(arrangement));
    mRenderer->SetEffects(effects);
    mRenderer->UseLiveSpeedCurve(options.followTempoCurve);
    mRenderer->SetOverlay(options.overlay);
    mRenderer->SetIsCue(options.isCue);
    mBlockCount = 0;
    mRingFrames = static_cast<size_t>(options.readAheadSeconds * options.sampleRate) + 4 * kRenderBlock;
    mRing.assign(mRingFrames * options.channels, 0.0f);
    mWritten = 0;
    mRead = 0;
    mSeekRequest = -1.0;
    ResetBuffer(startTime);

    if (Pa_Initialize() != paNoError) {
        mError = "PortAudio could not start";
        return false;
    }
    PaStreamParameters out{};
    out.device = FindOutputDevice(options.deviceName, options.hostName);
    if (out.device == paNoDevice) {
        mError = "no audio output device";
        Pa_Terminate();
        return false;
    }
    const PaDeviceInfo* info = Pa_GetDeviceInfo(out.device);
    const size_t available = info ? static_cast<size_t>(std::max(0, info->maxOutputChannels)) : options.channels;
    mChannelOffset = available >= options.channelOffset + options.channels ? options.channelOffset : 0;
    mDeviceChannels = mChannelOffset + options.channels;
    out.channelCount = static_cast<int>(mDeviceChannels);
    out.sampleFormat = paFloat32;
    // The buffer Preferences asks for (100 ms on Windows), not the device's
    // lowest: that crackled on a 2-core machine.
    out.suggestedLatency = std::max(info ? info->defaultLowOutputLatency : 0.01, options.latencySeconds);
#ifdef _WIN32
    // WASAPI shared mode only takes the device's own rate; let Windows convert
    // the project's.
    PaWasapiStreamInfo wasapi {};
    const PaHostApiInfo* host = info ? Pa_GetHostApiInfo(info->hostApi) : nullptr;
    if (host && host->type == paWASAPI) {
        wasapi.size = sizeof(wasapi);
        wasapi.hostApiType = paWASAPI;
        wasapi.version = 1;
        wasapi.flags = paWinWasapiAutoConvert;
        out.hostApiSpecificStreamInfo = &wasapi;
    }
#endif
    PaStream* stream = nullptr;
    const PaError err = Pa_OpenStream(&stream, nullptr, &out, options.sampleRate, paFramesPerBufferUnspecified,
                                      paClipOff, &PaCallback, this);
    if (err != paNoError) {
        mError = Pa_GetErrorText(err);
        Pa_Terminate();
        return false;
    }
    mStream = stream;
    if (const PaStreamInfo* streamInfo = Pa_GetStreamInfo(stream)) {
        mOutputLatency = streamInfo->outputLatency;
    }
    if (mOptions.meter) {
        mRenderer->SetTrackSink([this](int64_t trackId, float* const* buffers, size_t frames) {
            const auto dacTime = mBlockDacTime;
            for (size_t c = 0; c < mOptions.channels; ++c) {
                mOptions.meter->push(static_cast<uint8_t>(c), { buffers[c], frames, 1, dacTime },
                                     IMeterSender::TrackId { trackId });
            }
        });
    }

    mRunning = true;
    mThread = std::thread([this] { RenderLoop(); });
    // Let the render thread fill the read-ahead before the device starts pulling.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (mWritten.load() - mRead.load() < mRingFrames / 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (Pa_StartStream(stream) != paNoError) {
        mError = "audio stream did not start";
        Stop();
        return false;
    }
    mActive = true;
    ++s_activeTransports;
    return true;
}

void Transport::Stop()
{
    mRunning = false;
    if (mThread.joinable()) {
        mThread.join();
    }
    if (mStream) {
        Pa_AbortStream(static_cast<PaStream*>(mStream));
        Pa_CloseStream(static_cast<PaStream*>(mStream));
        mStream = nullptr;
        Pa_Terminate();
    }
    if (mActive.exchange(false)) {
        --s_activeTransports;
    }
    // The renderer holds the arrangement, and with it the project's tracks and
    // sample blocks. Kept after a File > New, the old project's blocks were
    // freed on the next Play, after its database had closed ("Connection to
    // project file is null").
    mRenderer.reset();
}

void Transport::SetPaused(bool paused)
{
    mPaused = paused;
}

void Transport::Seek(double time)
{
    mSeekRequest = std::max(0.0, time);
}

void Transport::SetLoop(bool active, double start, double end)
{
    if (mRenderer) {
        mRenderer->SetLoop(active, start, end);
    }
}

void Transport::SetArrangement(std::shared_ptr<const Arrangement> arrangement)
{
    if (mRenderer) {
        mRenderer->SetArrangement(std::move(arrangement));
    }
}

double Transport::BufferedMs() const
{
    const uint64_t read = mRead.load();
    const uint64_t written = mWritten.load();
    return written > read ? 1000.0 * static_cast<double>(written - read) / mOptions.sampleRate : 0.0;
}

double Transport::Position() const
{
    const uint64_t read = mRead.load();
    const uint64_t count = mBlockCount.load();
    // Newest block first: the one holding the frame the device plays now.
    for (uint64_t k = 0; k < std::min<uint64_t>(count, kBlockTimes); ++k) {
        const BlockTime& block = mBlockTimes[(count - 1 - k) % kBlockTimes];
        const uint64_t start = block.start.load();
        if (start < mResetIndex.load()) {
            break;   // from before the last seek
        }
        if (read >= start) {
            const double fraction = std::min(1.0, static_cast<double>(read - start) / kRenderBlock);
            const double t0 = block.t0.load();
            return t0 + (block.t1.load() - t0) * fraction;
        }
    }
    return mResetTime.load();
}

void Transport::ResetBuffer(double time)
{
    // Render thread (or before it starts): the ring restarts empty at `time`.
    // The callback sees mRead == mWritten and plays silence until new frames
    // arrive.
    mRenderer->Seek(time);
    const uint64_t read = mRead.load();
    mWritten = read;
    mResetTime = time;
    mResetIndex = read;
}

void Transport::RenderLoop()
{
    const size_t channels = mOptions.channels;
    std::vector<std::vector<float> > planar(channels, std::vector<float>(kRenderBlock));
    std::vector<float*> ptrs(channels);
    for (size_t c = 0; c < channels; ++c) {
        ptrs[c] = planar[c].data();
    }
    const size_t target = static_cast<size_t>(mOptions.readAheadSeconds * mOptions.sampleRate);
    // Debug: MIXTAPE_ENGINE_DUMP=/path writes what is rendered (raw float32,
    // interleaved) so playback can be measured without listening.
    FILE* dump = nullptr;
    if (const char* path = std::getenv("MIXTAPE_ENGINE_DUMP"); path && *path) {
        dump = std::fopen(path, "wb");
    }
    // ... or, without restarting: while the file
    // ~/Library/Application Support/Glidemix/dump-audio exists, append
    // to the path written in it (checked every ~0.2 s).
    std::string triggerFile;
    if (const char* home = std::getenv("HOME")) {
        triggerFile = std::string(home) + "/Library/Application Support/Glidemix/dump-audio";
    }
    bool triggerDump = false;
    size_t sinceCheck = 0;
    std::vector<float> interleaved(kRenderBlock * channels);
    while (mRunning.load()) {
        const double seek = mSeekRequest.exchange(-1.0);
        if (seek >= 0.0) {
            ResetBuffer(seek);
        }
        uint64_t written = mWritten.load();
        const uint64_t read = mRead.load();
        if (read > written) {
            // The callback consumed past a reset (it had read stale counters):
            // skip the renderer ahead by the same amount to stay in step.
            mRenderer->Seek(mRenderer->Position() + static_cast<double>(read - written) / mOptions.sampleRate);
            written = read;
            mWritten = written;
            mResetIndex = written;
            mResetTime = mRenderer->Position();
        }
        if (written - read >= target) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        // When this block will reach the speakers: what is already queued plus the device latency.
        const double ahead = static_cast<double>(written - read) / mOptions.sampleRate + mOutputLatency.load();
        mBlockDacTime = std::chrono::steady_clock::now()
                        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(ahead));
        const auto renderStart = std::chrono::steady_clock::now();
        auto [t0, t1] = mRenderer->RenderLooped(ptrs.data(), kRenderBlock);
        {
            const uint64_t us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                          std::chrono::steady_clock::now() - renderStart).count());
            uint64_t slowest = mSlowestRenderUs.load();
            while (us > slowest && !mSlowestRenderUs.compare_exchange_weak(slowest, us)) {}
        }
        if (t1 < t0) {
            t1 = t0 + kRenderBlock / mOptions.sampleRate;   // wrapped at the loop end: good enough for the playhead
        }
        {
            BlockTime& block = mBlockTimes[mBlockCount.load() % kBlockTimes];
            block.t0 = t0;
            block.t1 = t1;
            // The output lags the timeline by the latency compensation.
            block.start = written + mRenderer->OutputDelayFrames();
            mBlockCount.fetch_add(1);
        }
        if (mOptions.meter) {
            for (size_t c = 0; c < channels; ++c) {
                mOptions.meter->push(static_cast<uint8_t>(c), { planar[c].data(), kRenderBlock, 1, mBlockDacTime });
            }
        }
        if (!triggerFile.empty() && ++sinceCheck * kRenderBlock >= mOptions.sampleRate / 5) {
            sinceCheck = 0;
            FILE* trigger = std::fopen(triggerFile.c_str(), "r");
            if (trigger && !dump) {
                char path[1024] = {};
                if (std::fgets(path, sizeof(path) - 1, trigger)) {
                    path[std::strcspn(path, "\r\n")] = 0;
                    dump = std::fopen(path, "ab");
                    triggerDump = dump != nullptr;
                }
            } else if (!trigger && dump && triggerDump) {
                std::fclose(dump);
                dump = nullptr;
                triggerDump = false;
            }
            if (trigger) {
                std::fclose(trigger);
            }
        }
        if (dump) {
            for (size_t i = 0; i < kRenderBlock; ++i) {
                for (size_t c = 0; c < channels; ++c) {
                    interleaved[i * channels + c] = planar[c][i];
                }
            }
            std::fwrite(interleaved.data(), sizeof(float), interleaved.size(), dump);
        }
        for (size_t i = 0; i < kRenderBlock; ++i) {
            const size_t slot = static_cast<size_t>((written + i) % mRingFrames) * channels;
            for (size_t c = 0; c < channels; ++c) {
                mRing[slot + c] = planar[c][i];
            }
        }
        mWritten = written + kRenderBlock;
    }
    if (dump) {
        std::fclose(dump);
    }
}


int Transport::DeviceOutputs(const std::string& deviceName)
{
    if (Pa_Initialize() != paNoError) {
        return 0;
    }
    int outputs = 0;
    const PaDeviceIndex device = FindOutputDevice(deviceName);
    if (const PaDeviceInfo* info = device != paNoDevice ? Pa_GetDeviceInfo(device) : nullptr) {
        outputs = info->maxOutputChannels;
    }
    Pa_Terminate();
    return outputs;
}

std::vector<std::string> Transport::OutputDevices()
{
    std::vector<std::string> names;
    if (Pa_Initialize() != paNoError) {
        return names;
    }
    const int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels >= 2 && info->name
            && std::find(names.begin(), names.end(), info->name) == names.end()) {
            names.emplace_back(info->name);
        }
    }
    Pa_Terminate();
    return names;
}

int Transport::OnCallback(float* deviceOutput, unsigned long frames)
{
    const size_t channels = mOptions.channels;
    const size_t deviceChannels = mDeviceChannels;
    if (deviceChannels != channels) {
        // Render the pair into scratch, then place it on its outputs.
        thread_local std::vector<float> scratch;
        if (scratch.size() < frames * channels) {
            scratch.resize(frames * channels);
        }
        const int result = OnCallbackPair(scratch.data(), frames);
        std::memset(deviceOutput, 0, frames * deviceChannels * sizeof(float));
        for (unsigned long i = 0; i < frames; ++i) {
            for (size_t c = 0; c < channels; ++c) {
                deviceOutput[i * deviceChannels + mChannelOffset + c] = scratch[i * channels + c];
            }
        }
        return result;
    }
    return OnCallbackPair(deviceOutput, frames);
}

int Transport::OnCallbackPair(float* output, unsigned long frames)
{
    const size_t channels = mOptions.channels;
    if (mPaused.load()) {
        std::memset(output, 0, frames * channels * sizeof(float));
        return paContinue;
    }
    uint64_t read = mRead.load();
    const uint64_t written = mWritten.load();
    const size_t available = written > read ? static_cast<size_t>(std::min<uint64_t>(written - read, frames)) : 0;
    for (size_t i = 0; i < available; ++i) {
        const size_t slot = static_cast<size_t>((read + i) % mRingFrames) * channels;
        std::memcpy(output + i * channels, mRing.data() + slot, channels * sizeof(float));
    }
    if (available < frames) {
        if (mStarted.load()) {
            mUnderruns.fetch_add(1);
        }
        std::memset(output + available * channels, 0, (frames - available) * channels * sizeof(float));
    }
    mRead = read + available;
    if (available > 0) {
        mStarted = true;
    }
    if (const double end = mEndTime.load(); end > 0.0 && Position() >= end) {
        mReachedEnd = true;
    }
    return paContinue;
}
}
