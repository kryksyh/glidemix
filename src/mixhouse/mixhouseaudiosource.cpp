/*
 * MixHouse DJ 2 -- audio decoding for analysis.
 */
#include "mixhouseaudiosource.h"

#include <QFileInfo>

#include <cmath>
#include <mutex>

#include <sndfile.h>
#include <mpg123.h>

#ifdef Q_OS_MACOS
#include <AudioToolbox/ExtendedAudioFile.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

#ifdef Q_OS_LINUX
#include "mod-ffmpeg/lib-ffmpeg-support/FFmpegFunctions.h"
#endif

#include "mixhouse/mixhousenativepath.h"
#include "log.h"

using namespace au::mixhouse;

namespace {
constexpr long long kChunkFrames = 8192;

bool isMp3(const QString& path)
{
    return QFileInfo(path).suffix().compare(QStringLiteral("mp3"), Qt::CaseInsensitive) == 0;
}

// --- libsndfile: wav, aiff, flac, ogg, opus -------------------------------
AudioInfo decodeSndfile(const QString& path,
                        const std::function<bool(const float*, long long)>* onSamples)
{
    AudioInfo info;
    SF_INFO sf{};
    SNDFILE* f = openSndfile(path, SFM_READ, &sf);
    if (!f) {
        info.error = QString::fromUtf8(sf_strerror(nullptr));
        return info;
    }
    info.valid = true;
    info.sampleRate = sf.samplerate;
    info.channels = sf.channels;
    info.frames = sf.frames;
    info.durationSec = (sf.samplerate > 0) ? double(sf.frames) / sf.samplerate : 0.0;

    if (onSamples && sf.channels > 0) {
        std::vector<float> buf(static_cast<size_t>(kChunkFrames) * sf.channels);
        sf_count_t got = 0;
        while ((got = sf_readf_float(f, buf.data(), kChunkFrames)) > 0) {
            if (!(*onSamples)(buf.data(), got)) {
                break;   // analyzer asked to stop
            }
        }
    }
    sf_close(f);
    return info;
}

// --- mpg123: mp3 ----------------------------------------------------------
AudioInfo decodeMpg123(const QString& path,
                       const std::function<bool(const float*, long long)>* onSamples)
{
    AudioInfo info;
    // Real, confirmed crash this fixes -- found via two separate real .ips
    // crash reports (SIGBUS/EXC_BAD_ACCESS inside libmpg123's own
    // INT123_frame_init_par, called from mpg123_new()), both from
    // MixHouseAnalyzerWorker's background pool, the first genuinely
    // concurrent caller of this function this app has ever had (harmless
    // before that: analysis only ever ran on one thread at a time). A plain
    // `static bool inited` guard around mpg123_init() was the first fix
    // tried here -- necessary (it WAS a real, separate race) but NOT
    // sufficient: replacing it with std::call_once did not stop the crash,
    // proven by a second crash at the exact same site after that fix was
    // already live. mpg123_new() itself races on a SECOND, separate piece
    // of internal state -- a lazily-computed CPU-feature/decoder-dispatch
    // table that mpg123_init() does NOT populate, only mpg123_new() does,
    // on demand, the first several times it's called -- a documented real
    // limitation of this library for multi-threaded callers, not something
    // fixable from this app's side except by serializing the calls that
    // touch it. The decode loop below (the actual per-track work, and the
    // whole reason for running this on N worker threads at all) stays
    // completely unlocked and parallel; only handle creation/destruction is
    // serialized, which costs nothing measurable next to a real decode
    // pass.
    static std::once_flag mpg123InitOnce;
    std::call_once(mpg123InitOnce, []() { mpg123_init(); });
    static std::mutex mpg123HandleMutex;

    int err = MPG123_OK;
    mpg123_handle* h = nullptr;
    {
        std::lock_guard<std::mutex> lock(mpg123HandleMutex);
        h = mpg123_new(nullptr, &err);
    }
    if (!h) {
        info.error = QStringLiteral("mpg123_new failed");
        return info;
    }
    // Every mpg123_delete() below goes through this, same mutex as
    // mpg123_new() above -- cheap insurance in case the internal race
    // mpg123_new() has (see this function's own comment above) turns out to
    // touch teardown too; no crash has actually been observed here, but the
    // lock costs nothing next to a real decode pass either way.
    auto deleteHandle = [&]() {
        std::lock_guard<std::mutex> lock(mpg123HandleMutex);
        mpg123_delete(h);
    };
    // mpg123_open() + mpg123_format() (pinning the format AFTER opening) looks
    // right and "succeeds", but per mpg123.h's own doc comment on
    // mpg123_format(): "It only has effect on the next natural change of
    // output format, when libmpg123 will consult its format table again" --
    // for a normal file with no internal format change, that never happens,
    // so the pin silently never took effect. Measured on real files: every
    // sample this used to hand the analyzer was raw SIGNED_16 PCM
    // bit-reinterpreted as FLOAT_32 (garbage values, and exactly half as many
    // "frames" as the true decode, since 2 bytes/sample were being counted as
    // 4). mpg123_open_fixed() is the API's own documented fix: it sets up the
    // format table BEFORE opening, so the negotiated format is guaranteed
    // correct from the very first frame. It also upmixes mono to stereo,
    // which is fine -- decodeAudioFile's downmix step below averages
    // however many channels come back, mono-doubled-to-stereo included.
    if (mpg123_open_fixed(h, path.toUtf8().constData(), MPG123_STEREO, MPG123_ENC_FLOAT_32) != MPG123_OK) {
        info.error = QString::fromUtf8(mpg123_strerror(h));
        deleteHandle();
        return info;
    }

    long rate = 0;
    int channels = 0, encoding = 0;
    if (mpg123_getformat(h, &rate, &channels, &encoding) != MPG123_OK) {
        info.error = QStringLiteral("mpg123_getformat: %1")
                         .arg(QString::fromUtf8(mpg123_strerror(h)));
        mpg123_close(h); deleteHandle();
        return info;
    }

    info.valid = true;
    info.sampleRate = static_cast<int>(rate);
    info.channels = channels;
    const off_t len = mpg123_length(h);          // frames, or MPG123_ERR
    info.frames = (len > 0) ? static_cast<long long>(len) : 0;
    info.durationSec = (rate > 0 && info.frames > 0) ? double(info.frames) / rate : 0.0;

    if (onSamples && channels > 0) {
        std::vector<float> buf(static_cast<size_t>(kChunkFrames) * channels);
        size_t done = 0;
        int r = MPG123_OK;
        while ((r = mpg123_read(h, reinterpret_cast<unsigned char*>(buf.data()),
                                buf.size() * sizeof(float), &done)) == MPG123_OK
               || r == MPG123_NEW_FORMAT) {
            if (r == MPG123_NEW_FORMAT) {
                continue;
            }
            const long long frames = static_cast<long long>(done / sizeof(float)) / channels;
            if (frames <= 0) {
                continue;
            }
            if (!(*onSamples)(buf.data(), frames)) {
                break;
            }
        }
    }
    mpg123_close(h);
    deleteHandle();
    return info;
}

#ifdef Q_OS_MACOS
// --- macOS AudioToolbox: m4a/aac/alac and anything else libsndfile cannot
// open. Part of the system, so nothing to install or ship.
AudioInfo decodeExtAudioFile(const QString& path,
                             const std::function<bool(const float*, long long)>* onSamples)
{
    AudioInfo info;
    const QByteArray utf8 = path.toUtf8();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(utf8.constData()),
                                                           utf8.size(), false);
    if (!url) {
        info.error = QStringLiteral("bad path");
        return info;
    }
    ExtAudioFileRef file = nullptr;
    OSStatus status = ExtAudioFileOpenURL(url, &file);
    CFRelease(url);
    if (status != noErr || !file) {
        info.error = QStringLiteral("AudioToolbox cannot open the file (%1)").arg(status);
        return info;
    }

    AudioStreamBasicDescription fileFormat {};
    UInt32 size = sizeof(fileFormat);
    SInt64 fileFrames = 0;
    UInt32 framesSize = sizeof(fileFrames);
    if (ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &size, &fileFormat) != noErr
        || fileFormat.mChannelsPerFrame == 0 || fileFormat.mSampleRate <= 0) {
        ExtAudioFileDispose(file);
        info.error = QStringLiteral("AudioToolbox cannot read the format");
        return info;
    }
    ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &framesSize, &fileFrames);

    const UInt32 channels = fileFormat.mChannelsPerFrame;
    info.valid = true;
    info.sampleRate = static_cast<int>(fileFormat.mSampleRate);
    info.channels = static_cast<int>(channels);
    info.frames = fileFrames;
    info.durationSec = double(fileFrames) / fileFormat.mSampleRate;

    if (onSamples) {
        // Interleaved 32-bit float at the file's own rate: what the analyzers take.
        AudioStreamBasicDescription client {};
        client.mSampleRate = fileFormat.mSampleRate;
        client.mFormatID = kAudioFormatLinearPCM;
        client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        client.mBitsPerChannel = 32;
        client.mChannelsPerFrame = channels;
        client.mFramesPerPacket = 1;
        client.mBytesPerFrame = 4 * channels;
        client.mBytesPerPacket = 4 * channels;
        if (ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client) != noErr) {
            ExtAudioFileDispose(file);
            info.valid = false;
            info.error = QStringLiteral("AudioToolbox cannot convert the file");
            return info;
        }
        std::vector<float> buf(static_cast<size_t>(kChunkFrames) * channels);
        for (;;) {
            AudioBufferList list {};
            list.mNumberBuffers = 1;
            list.mBuffers[0].mNumberChannels = channels;
            list.mBuffers[0].mDataByteSize = static_cast<UInt32>(buf.size() * sizeof(float));
            list.mBuffers[0].mData = buf.data();
            UInt32 frames = static_cast<UInt32>(kChunkFrames);
            if (ExtAudioFileRead(file, &frames, &list) != noErr || frames == 0) {
                break;
            }
            if (!(*onSamples)(buf.data(), frames)) {
                break;
            }
        }
    }
    ExtAudioFileDispose(file);
    return info;
}
#endif

#ifdef Q_OS_WIN
// --- Windows Media Foundation: m4a/aac/alac (and mp3, flac, wav) -- the
// Windows counterpart of AudioToolbox above, part of the system.
AudioInfo decodeMediaFoundation(const QString& path,
                                const std::function<bool(const float*, long long)>* onSamples)
{
    using Microsoft::WRL::ComPtr;
    AudioInfo info;
    // Analysis threads have no COM yet; the main thread already has it
    // (RPC_E_CHANGED_MODE), which is fine as well.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    static std::once_flag mfStartOnce;
    std::call_once(mfStartOnce, []() { MFStartup(MF_VERSION, MFSTARTUP_LITE); });
    struct ComScope {
        HRESULT hr;
        ~ComScope() { if (SUCCEEDED(hr)) { CoUninitialize(); } }
    } comScope { com };

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(reinterpret_cast<const wchar_t*>(path.utf16()), nullptr, &reader);
    if (FAILED(hr)) {
        info.error = QStringLiteral("Media Foundation cannot open the file (0x%1)").arg(quint32(hr), 8, 16, QLatin1Char('0'));
        return info;
    }
    const DWORD audio = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(audio, TRUE);

    // Interleaved 32-bit float at the file's own rate and channels.
    ComPtr<IMFMediaType> wanted;
    MFCreateMediaType(&wanted);
    wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    ComPtr<IMFMediaType> type;
    if (FAILED(reader->SetCurrentMediaType(audio, nullptr, wanted.Get()))
        || FAILED(reader->GetCurrentMediaType(audio, &type))) {
        info.error = QStringLiteral("Media Foundation cannot decode the audio");
        return info;
    }
    const UINT32 channels = MFGetAttributeUINT32(type.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
    const UINT32 rate = MFGetAttributeUINT32(type.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
    if (channels == 0 || rate == 0) {
        info.error = QStringLiteral("Media Foundation cannot read the format");
        return info;
    }
    PROPVARIANT duration;
    PropVariantInit(&duration);
    double seconds = 0.0;
    if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &duration))
        && duration.vt == VT_UI8) {
        seconds = double(duration.uhVal.QuadPart) / 1e7;   // 100 ns units
    }
    PropVariantClear(&duration);

    info.valid = true;
    info.sampleRate = static_cast<int>(rate);
    info.channels = static_cast<int>(channels);
    info.durationSec = seconds;
    info.frames = static_cast<long long>(std::llround(seconds * rate));

    if (onSamples) {
        for (;;) {
            DWORD flags = 0;
            ComPtr<IMFSample> sample;
            if (FAILED(reader->ReadSample(audio, 0, nullptr, &flags, nullptr, &sample))
                || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                break;
            }
            if (!sample) {
                continue;
            }
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
                break;
            }
            BYTE* data = nullptr;
            DWORD bytes = 0;
            if (FAILED(buffer->Lock(&data, nullptr, &bytes))) {
                break;
            }
            const long long frames = static_cast<long long>(bytes / sizeof(float)) / channels;
            const bool more = frames <= 0 || (*onSamples)(reinterpret_cast<const float*>(data), frames);
            buffer->Unlock();
            if (!more) {
                break;
            }
        }
    }
    return info;
}
#endif

#ifdef Q_OS_LINUX
// --- FFmpeg: m4a/aac/alac and anything else libsndfile cannot read -- the
// Linux counterpart of AudioToolbox / Media Foundation. Linux has no system
// decoder for these; the importer already uses the FFmpeg the user has
// installed (loaded at run time), so the analysis does too.
AudioInfo decodeFFmpeg(const QString& path,
                       const std::function<bool(const float*, long long)>* onSamples)
{
    AudioInfo info;
    const auto ffmpeg = FFmpegFunctions::Load();
    if (!ffmpeg) {
        info.error = QStringLiteral("FFmpeg is not installed");
        return info;
    }
    auto format = ffmpeg->CreateAVFormatContext();
    if (!format || format->OpenInputContext(wxString::FromUTF8(path.toUtf8().constData()), nullptr,
                                            AVDictionaryWrapper(*ffmpeg)) != AVIOContextWrapper::OpenResult::Success) {
        info.error = QStringLiteral("Format not recognised.");
        return info;
    }
    // The first audio stream FFmpeg can decode.
    std::unique_ptr<AVCodecContextWrapper> codec;
    int streamIndex = -1;
    for (unsigned int i = 0; i < format->GetStreamsCount() && !codec; ++i) {
        const AVStreamWrapper* stream = format->GetStream(static_cast<int>(i));
        if (!stream || !stream->IsAudio() || !ffmpeg->CreateDecoder(stream->GetAVCodecID())) {
            continue;
        }
        auto context = stream->GetAVCodecContext();
        if (context && context->Open(context->GetCodec()) >= 0 && context->GetChannels() > 0) {
            codec = std::move(context);
            streamIndex = stream->GetIndex();
        }
    }
    if (!codec) {
        info.error = QStringLiteral("No audio stream FFmpeg can decode.");
        return info;
    }
    info.valid = true;
    info.sampleRate = codec->GetSampleRate();
    info.channels = codec->GetChannels();
    const int64_t duration = format->GetDuration();   // AV_TIME_BASE units (microseconds)
    if (duration > 0 && info.sampleRate > 0) {
        info.durationSec = static_cast<double>(duration) / 1e6;
        info.frames = static_cast<long long>(std::llround(info.durationSec * info.sampleRate));
    }
    if (!onSamples) {
        return info;
    }

    const auto deliver = [&](const std::vector<float>& data) {
        const long long frames = static_cast<long long>(data.size()) / info.channels;
        return frames <= 0 || (*onSamples)(data.data(), frames);
    };
    long long decoded = 0;
    bool more = true;
    for (std::unique_ptr<AVPacketWrapper> packet; more && (packet = format->ReadNextPacket()) != nullptr;) {
        if (packet->GetStreamIndex() != streamIndex) {
            continue;
        }
        const std::vector<float> data = codec->DecodeAudioPacketFloat(packet.get());
        decoded += static_cast<long long>(data.size()) / info.channels;
        more = deliver(data);
    }
    if (more) {   // what the decoder still holds
        auto empty = ffmpeg->CreateAVPacketWrapper();
        const std::vector<float> data = codec->DecodeAudioPacketFloat(empty.get());
        decoded += static_cast<long long>(data.size()) / info.channels;
        deliver(data);
    }
    if (decoded > 0) {
        info.frames = decoded;
        info.durationSec = static_cast<double>(decoded) / info.sampleRate;
    }
    return info;
}
#endif

AudioInfo decode(const QString& path, const std::function<bool(const float*, long long)>* onSamples)
{
    AudioInfo info = isMp3(path) ? decodeMpg123(path, onSamples) : decodeSndfile(path, onSamples);
#ifdef Q_OS_MACOS
    if (!info.valid && !isMp3(path)) {
        info = decodeExtAudioFile(path, onSamples);
    }
#endif
#ifdef Q_OS_WIN
    if (!info.valid) {
        info = decodeMediaFoundation(path, onSamples);
    }
#endif
#ifdef Q_OS_LINUX
    if (!info.valid && !isMp3(path)) {
        info = decodeFFmpeg(path, onSamples);
    }
#endif
    return info;
}
}  // namespace

AudioInfo au::mixhouse::decodeAudioFile(const QString& path,
        const std::function<bool(const float*, long long)>& onSamples)
{
    return decode(path, &onSamples);
}

AudioInfo au::mixhouse::probeAudioFile(const QString& path)
{
    return decode(path, nullptr);
}
