/*
 * GlideMix
 */
#include "../../mixhousepreviewplayer.h"

// Linux: no system media player to hand the file to (AVAudioEngine on macOS,
// the Media Engine on Windows), so the song is decoded by the analysis
// decoder (mp3, libsndfile formats, m4a/aac through FFmpeg) on its own thread
// into memory, and played from there on its own PortAudio output stream --
// independent of the project's playback, like the other platforms.

#include <QString>

#include <portaudio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include "mixhouse/mixhouseaudiosource.h"
#include "log.h"

namespace au::mixhouse {
namespace {
constexpr int kChannels = 2;   // everything is played as stereo
constexpr unsigned long kFramesPerBuffer = 1024;
}

struct MixHousePreviewPlayer::Impl {
    bool paReady = false;
    PaStream* stream = nullptr;
    double streamRate = 0.0;

    // The song, 16-bit stereo, written by the decoder thread up to
    // `decoded` and read by the audio callback below it. Sized once in load(),
    // so the callback never sees it move.
    std::vector<int16_t> samples;
    long long capacity = 0;              ///< frames the buffer holds
    std::atomic<long long> decoded { 0 };
    std::atomic<bool> decodeDone { false };
    std::atomic<bool> cancel { false };
    std::thread decoder;
    int fileRate = 0;

    std::atomic<double> pos { 0.0 };     ///< frames into the song (fractional when resampling)
    std::atomic<bool> playing { false };

    long long totalFrames() const
    {
        return decodeDone ? decoded.load() : capacity;
    }

    void stopDecoder()
    {
        cancel = true;
        if (decoder.joinable()) {
            decoder.join();
        }
        cancel = false;
    }

    void closeStream()
    {
        if (stream) {
            Pa_StopStream(stream);
            Pa_CloseStream(stream);
            stream = nullptr;
        }
    }

    static int callback(const void*, void* output, unsigned long frames, const PaStreamCallbackTimeInfo*,
                        PaStreamCallbackFlags, void* user)
    {
        auto* self = static_cast<Impl*>(user);
        auto* out = static_cast<float*>(output);
        std::fill(out, out + frames * kChannels, 0.0f);
        if (!self->playing) {
            return paContinue;
        }
        const double step = self->streamRate > 0.0 ? self->fileRate / self->streamRate : 1.0;
        const long long ready = self->decoded.load();
        const long long total = self->totalFrames();
        double p = self->pos.load();
        for (unsigned long i = 0; i < frames; ++i) {
            const long long i0 = static_cast<long long>(p);
            if (i0 >= total) {
                self->playing = false;   // the end of the song
                break;
            }
            if (i0 + 1 >= ready) {
                break;                   // not decoded yet: silence until it is
            }
            const float u = static_cast<float>(p - static_cast<double>(i0));
            for (int ch = 0; ch < kChannels; ++ch) {
                const float a = self->samples[static_cast<size_t>(i0 * kChannels + ch)] / 32768.0f;
                const float b = self->samples[static_cast<size_t>((i0 + 1) * kChannels + ch)] / 32768.0f;
                out[i * kChannels + ch] = a + (b - a) * u;
            }
            p += step;
        }
        self->pos = p;
        return paContinue;
    }

    bool openStream(const std::string& deviceName)
    {
        PaDeviceIndex device = Pa_GetDefaultOutputDevice();
        for (PaDeviceIndex i = 0; !deviceName.empty() && i < Pa_GetDeviceCount(); ++i) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (info && info->maxOutputChannels >= kChannels && deviceName == info->name) {
                device = i;
                break;
            }
        }
        if (device == paNoDevice) {
            LOGW() << "Library preview: no audio output device";
            return false;
        }
        const PaDeviceInfo* info = Pa_GetDeviceInfo(device);
        PaStreamParameters params {};
        params.device = device;
        params.channelCount = kChannels;
        params.sampleFormat = paFloat32;
        params.suggestedLatency = info ? info->defaultHighOutputLatency : 0.1;
        // The song's own rate if the device takes it, else the device's (resampled above).
        for (const double rate : { static_cast<double>(fileRate), info ? info->defaultSampleRate : 44100.0 }) {
            if (rate > 0.0 && Pa_OpenStream(&stream, nullptr, &params, rate, kFramesPerBuffer, paClipOff,
                                            &Impl::callback, this) == paNoError) {
                streamRate = rate;
                return true;
            }
            stream = nullptr;
        }
        LOGW() << "Library preview: could not open the output stream";
        return false;
    }
};

MixHousePreviewPlayer::MixHousePreviewPlayer()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->paReady = Pa_Initialize() == paNoError;
}

MixHousePreviewPlayer::~MixHousePreviewPlayer()
{
    m_impl->playing = false;
    m_impl->closeStream();
    m_impl->stopDecoder();
    if (m_impl->paReady) {
        Pa_Terminate();
    }
}

bool MixHousePreviewPlayer::load(const std::string& utf8Path, const std::string& outputDeviceName)
{
    Impl& d = *m_impl;
    d.playing = false;
    d.closeStream();
    d.stopDecoder();
    if (!d.paReady) {
        return false;
    }
    const QString path = QString::fromStdString(utf8Path);
    const AudioInfo probe = probeAudioFile(path);
    if (!probe.valid || probe.sampleRate <= 0 || probe.channels <= 0 || probe.frames <= 0) {
        return false;
    }
    d.fileRate = probe.sampleRate;
    // A little room in case the decoder finds more frames than the header said.
    d.capacity = probe.frames + probe.sampleRate;
    d.samples.assign(static_cast<size_t>(d.capacity) * kChannels, 0);
    d.decoded = 0;
    d.decodeDone = false;
    d.pos = 0.0;
    if (!d.openStream(outputDeviceName)) {
        return false;
    }
    const int channels = probe.channels;
    d.decoder = std::thread([&d, path, channels]() {
        long long frame = 0;
        decodeAudioFile(path, [&](const float* in, long long frames) {
            for (long long i = 0; i < frames && frame < d.capacity; ++i, ++frame) {
                const float left = in[i * channels];
                const float right = channels > 1 ? in[i * channels + 1] : left;
                d.samples[static_cast<size_t>(frame * kChannels)]
                    = static_cast<int16_t>(std::lround(std::clamp(left, -1.0f, 1.0f) * 32767.0f));
                d.samples[static_cast<size_t>(frame * kChannels + 1)]
                    = static_cast<int16_t>(std::lround(std::clamp(right, -1.0f, 1.0f) * 32767.0f));
            }
            d.decoded = frame;
            return !d.cancel && frame < d.capacity;
        });
        d.decodeDone = true;
    });
    return true;
}

void MixHousePreviewPlayer::playFrom(double fraction)
{
    Impl& d = *m_impl;
    if (!d.stream) {
        return;
    }
    d.pos = std::clamp(fraction, 0.0, 1.0) * static_cast<double>(d.totalFrames());
    d.playing = true;
    if (Pa_IsStreamActive(d.stream) != 1) {
        Pa_StartStream(d.stream);
    }
}

void MixHousePreviewPlayer::stop()
{
    Impl& d = *m_impl;
    d.playing = false;
    if (d.stream && Pa_IsStreamActive(d.stream) == 1) {
        Pa_StopStream(d.stream);
    }
}

bool MixHousePreviewPlayer::isPlaying()
{
    return m_impl->playing;
}

double MixHousePreviewPlayer::position()
{
    const long long total = m_impl->totalFrames();
    return total > 0 ? std::clamp(m_impl->pos.load() / static_cast<double>(total), 0.0, 1.0) : 0.0;
}
}
