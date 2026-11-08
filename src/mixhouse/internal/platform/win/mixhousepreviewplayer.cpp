/*
 * GlideMix
 */
#include "../../mixhousepreviewplayer.h"

// Windows: Media Foundation's Media Engine plays the file directly (it
// decodes mp3, aac/m4a, alac, flac and wav and does its own output stream),
// independent of the project's playback, like the macOS AVAudioEngine player.
// It plays on the system's default output: the Media Engine cannot be pointed
// at a device by name.

#include <windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime   // winbase.h's GetTickCount alias hides IMFMediaEngine::GetCurrentTime
#endif
#include <mfapi.h>
#include <mfmediaengine.h>
#include <wrl/client.h>

#include <QDir>
#include <QFile>
#include <QString>

#include "mixhouse/mixhouseaudiosource.h"
#include "mixhouse/mixhousenativepath.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace {
// The Media Engine requires a callback; its events are not needed here
// (load() waits for the metadata, isPlaying() asks the engine).
class Notify : public IMFMediaEngineNotify
{
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
            *ppv = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_refs; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG refs = --m_refs;
        if (refs == 0) {
            delete this;
        }
        return refs;
    }
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR, DWORD) override
    {
        if (event == MF_MEDIA_ENGINE_EVENT_ERROR) {
            m_error = true;
        }
        return S_OK;
    }
    std::atomic<bool> m_error { false };

private:
    std::atomic<ULONG> m_refs { 1 };
};
}

namespace au::mixhouse {
struct MixHousePreviewPlayer::Impl {
    bool mfStarted = false;
    ComPtr<IMFMediaEngine> engine;
    Notify* notify = nullptr;
    double duration = 0.0;
    QString tempWav;   // a format the Media Engine can't play, decoded by GlideMix
    double lastPosition = 0.0;
    bool loaded = false;
    bool playing = false;

    bool ensureEngine()
    {
        if (engine) {
            return true;
        }
        if (!mfStarted) {
            if (FAILED(MFStartup(MF_VERSION))) {
                return false;
            }
            mfStarted = true;
        }
        ComPtr<IMFMediaEngineClassFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory)))) {
            return false;
        }
        ComPtr<IMFAttributes> attributes;
        if (FAILED(MFCreateAttributes(&attributes, 1))) {
            return false;
        }
        notify = new Notify();
        attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify);
        if (FAILED(factory->CreateInstance(MF_MEDIA_ENGINE_AUDIOONLY, attributes.Get(), &engine))) {
            engine.Reset();
            return false;
        }
        return true;
    }
};

MixHousePreviewPlayer::MixHousePreviewPlayer()
    : m_impl(std::make_unique<Impl>())
{
}

MixHousePreviewPlayer::~MixHousePreviewPlayer()
{
    Impl& d = *m_impl;
    if (d.engine) {
        d.engine->Shutdown();
        d.engine.Reset();
    }
    if (d.notify) {
        d.notify->Release();
    }
    if (d.mfStarted) {
        MFShutdown();
    }
    if (!d.tempWav.isEmpty()) {
        QFile::remove(d.tempWav);
    }
}

namespace {
// Opens `path` in the engine and waits for its metadata, as AVAudioFile opens
// synchronously, so playFrom() knows the length. The file goes in as a byte
// stream: SetSource() with a URL failed on names with characters like "·".
double openInEngine(IMFMediaEngine* engine, Notify* notify, const QString& path)
{
    ComPtr<IMFMediaEngineEx> engineEx;
    ComPtr<IMFByteStream> stream;
    if (FAILED(engine->QueryInterface(IID_PPV_ARGS(&engineEx)))
        || FAILED(MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE,
                               reinterpret_cast<const wchar_t*>(path.utf16()), &stream))) {
        return 0.0;
    }
    notify->m_error = false;
    // The name only tells the engine the format (by its extension).
    BSTR name = SysAllocString(reinterpret_cast<const wchar_t*>(path.utf16()));
    const HRESULT hr = engineEx->SetSourceFromByteStream(stream.Get(), name);
    SysFreeString(name);
    if (FAILED(hr)) {
        return 0.0;
    }
    for (int i = 0; i < 400; ++i) {
        if (notify->m_error) {
            return 0.0;
        }
        if (engine->GetReadyState() >= MF_MEDIA_ENGINE_READY_HAVE_METADATA) {
            const double duration = engine->GetDuration();
            return std::isfinite(duration) && duration > 0.0 ? duration : 0.0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return 0.0;
}

// OGG, Opus, AIFF...: GlideMix's own decoder writes a temporary WAV the
// engine can play.
bool decodeToWav(const QString& path, const QString& wav)
{
    const au::mixhouse::AudioInfo probe = au::mixhouse::probeAudioFile(path);
    if (!probe.valid || probe.sampleRate <= 0 || probe.channels <= 0) {
        return false;
    }
    SF_INFO info {};
    info.samplerate = probe.sampleRate;
    info.channels = probe.channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* out = au::mixhouse::openSndfile(wav, SFM_WRITE, &info);
    if (!out) {
        return false;
    }
    const au::mixhouse::AudioInfo decoded = au::mixhouse::decodeAudioFile(path, [&](const float* samples, long long frames) {
        sf_writef_float(out, samples, frames);
        return true;
    });
    sf_close(out);
    return decoded.valid;
}
}

bool MixHousePreviewPlayer::load(const std::string& utf8Path, const std::string&)
{
    Impl& d = *m_impl;
    stop();
    d.loaded = false;
    if (!d.ensureEngine()) {
        return false;
    }
    const QString path = QString::fromStdString(utf8Path);
    double duration = openInEngine(d.engine.Get(), d.notify, path);
    if (duration <= 0.0) {
        // Its own name each time: the engine may still hold the last one open.
        const QString wav = QDir::temp().filePath(QStringLiteral("glidemix-preview-%1.wav").arg(qHash(path), 0, 16));
        if (wav == d.tempWav || decodeToWav(path, wav)) {
            duration = openInEngine(d.engine.Get(), d.notify, wav);
        }
        if (duration > 0.0 && wav != d.tempWav) {
            if (!d.tempWav.isEmpty()) {
                QFile::remove(d.tempWav);
            }
            d.tempWav = wav;
        }
    }
    if (duration <= 0.0) {
        return false;
    }
    d.duration = duration;
    d.lastPosition = 0.0;
    d.loaded = true;
    return true;
}

void MixHousePreviewPlayer::playFrom(double fraction)
{
    Impl& d = *m_impl;
    if (!d.loaded) {
        return;
    }
    const double at = std::clamp(fraction, 0.0, 1.0) * d.duration;
    d.engine->SetCurrentTime(std::min(at, std::max(0.0, d.duration - 0.05)));
    if (FAILED(d.engine->Play())) {
        d.playing = false;
        return;
    }
    d.lastPosition = at / d.duration;
    d.playing = true;
}

void MixHousePreviewPlayer::stop()
{
    Impl& d = *m_impl;
    if (d.playing) {
        d.lastPosition = position();
    }
    d.playing = false;
    if (d.engine && d.loaded) {
        d.engine->Pause();
    }
}

bool MixHousePreviewPlayer::isPlaying()
{
    Impl& d = *m_impl;
    if (d.playing && d.engine && (d.engine->IsEnded() || d.notify->m_error)) {
        d.lastPosition = 1.0;
        d.playing = false;
    }
    return d.playing;
}

double MixHousePreviewPlayer::position()
{
    Impl& d = *m_impl;
    if (!d.loaded || !d.playing || d.duration <= 0.0) {
        return d.lastPosition;
    }
    return std::clamp(d.engine->GetCurrentTime() / d.duration, 0.0, 1.0);
}
void MixHousePreviewPlayer::setRate(double rate)
{
    // The Media Engine keeps the pitch while it changes the speed.
    if (m_impl->engine) {
        // The engine goes back to its default rate when playback starts, so set both.
        m_impl->engine->SetDefaultPlaybackRate(std::clamp(rate, 0.5, 2.0));
        m_impl->engine->SetPlaybackRate(std::clamp(rate, 0.5, 2.0));
    }
}
void MixHousePreviewPlayer::setVolume(double volume)
{
    if (m_impl->engine) {
        m_impl->engine->SetVolume(std::clamp(volume, 0.0, 1.0));
    }
}
}
