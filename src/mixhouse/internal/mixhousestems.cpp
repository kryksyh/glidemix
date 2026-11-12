/*
 * GlideMix
 */
#include "mixhousestems.h"
#include "mixhouse/glidemixpaths.h"
#include "mixhouse/mixhousenativepath.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <sndfile.h>

#include "au3-wave-track/WaveClip.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/internal/domconverter.h"
#include "au3wrap/internal/progressdialog.h"
#include "au3wrap/internal/trackcolor.h"
#include "project/iaudacityproject.h"
#include "log.h"

using namespace au::mixhouse;

namespace {
//! Demucs's stems, in the order the tracks are added.
struct StemInfo {
    const char* file;
    const char* title;
    int colorIndex;   ///< the clip palette (projectsceneconfiguration.cpp), DJ.Studio's stem colours
};
constexpr StemInfo kStems[] = {
    { "drums", "Drums", 16 },    // blue
    { "bass", "Bass", 1 },       // red
    { "vocals", "Vocals", 9 },   // green
    { "other", "Other", 4 },     // yellow
};

//! A clip's whole source (not just the visible part), interleaved float, as
//! GlideMix decoded it -- so the stems line up with it sample for sample.
bool writeClipSource(const WaveClip& clip, const QString& path, sf_count_t* frames, QCryptographicHash* hash)
{
    const size_t channels = clip.NChannels();
    const sampleCount total = clip.GetSequenceSamplesCount() / static_cast<long long>(std::max<size_t>(1, channels));   // per channel
    const sampleCount trimLeft = clip.TimeToSamples(clip.GetTrimLeft());
    SF_INFO info {};
    info.samplerate = clip.GetRate();
    info.channels = static_cast<int>(channels);
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* file = au::mixhouse::openSndfile(path, SFM_WRITE, &info);
    if (!file) {
        return false;
    }
    constexpr size_t kBlock = 65536;
    std::vector<std::vector<float> > planes(channels, std::vector<float>(kBlock));
    std::vector<float> interleaved(kBlock * channels);
    for (sampleCount pos = 0; pos < total; pos += kBlock) {
        const size_t len = static_cast<size_t>(std::min<sampleCount>(kBlock, total - pos).as_long_long());
        for (size_t ch = 0; ch < channels; ++ch) {
            clip.GetSamples(ch, reinterpret_cast<samplePtr>(planes[ch].data()), floatSample, pos - trimLeft, len, false);
        }
        for (size_t i = 0; i < len; ++i) {
            for (size_t ch = 0; ch < channels; ++ch) {
                interleaved[i * channels + ch] = planes[ch][i];
            }
        }
        sf_writef_float(file, interleaved.data(), static_cast<sf_count_t>(len));
        // The cache key: the samples only -- the file itself carries a time
        // stamp (libsndfile's PEAK chunk), so it differs every time.
        hash->addData(QByteArrayView(reinterpret_cast<const char*>(interleaved.data()),
                                     static_cast<qsizetype>(len * channels * sizeof(float))));
    }
    sf_close(file);
    *frames = total.as_long_long();
    return true;
}

//! One stem file as `channels` planes of exactly `frames` samples, resampled
//! (linearly) if Demucs's 44.1 kHz differs from the clip's rate.
bool readStem(const QString& path, size_t channels, sf_count_t frames, std::vector<std::vector<float> >* out)
{
    SF_INFO info {};
    SNDFILE* file = au::mixhouse::openSndfile(path, SFM_READ, &info);
    if (!file || info.channels < 1) {
        return false;
    }
    std::vector<float> interleaved(static_cast<size_t>(info.frames) * info.channels);
    sf_readf_float(file, interleaved.data(), info.frames);
    sf_close(file);

    out->assign(channels, std::vector<float>(static_cast<size_t>(frames), 0.0f));
    const double step = frames > 1 && info.frames > 1 ? double(info.frames - 1) / double(frames - 1) : 1.0;
    for (sf_count_t i = 0; i < frames; ++i) {
        const double src = info.frames == frames ? double(i) : i * step;
        const sf_count_t i0 = std::min<sf_count_t>(static_cast<sf_count_t>(src), info.frames - 1);
        const sf_count_t i1 = std::min<sf_count_t>(i0 + 1, info.frames - 1);
        const float frac = static_cast<float>(src - double(i0));
        for (size_t ch = 0; ch < channels; ++ch) {
            // A mono clip gets the mix of Demucs's two channels.
            auto sampleAt = [&](sf_count_t f) {
                if (channels == 1 && info.channels >= 2) {
                    return 0.5f * (interleaved[f * info.channels] + interleaved[f * info.channels + 1]);
                }
                const int c = std::min<int>(static_cast<int>(ch), info.channels - 1);
                return interleaved[f * info.channels + c];
            };
            (*out)[ch][static_cast<size_t>(i)] = sampleAt(i0) + (sampleAt(i1) - sampleAt(i0)) * frac;
        }
    }
    return true;
}

qint64 folderSize(const QString& dir)
{
    qint64 size = 0;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        size += it.fileInfo().size();
    }
    return size;
}
}

MixHouseStems::MixHouseStems(const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx)
{
}

QString MixHouseStems::cacheRoot()
{
    return paths::glidemixDataPath(QStringLiteral("Stems"));
}

namespace {
//! A virtual environment's Python: bin/python, Scripts\python.exe on Windows.
QString venvPython(const QString& dir)
{
#ifdef Q_OS_WIN
    return dir + QStringLiteral("/.venv/Scripts/python.exe");
#else
    return dir + QStringLiteral("/.venv/bin/python");
#endif
}
}

//! Looked for like the key analyzer: $GLIDEMIX_STEMS_PYTHON, an installed
//! add-on (Application Support/Glidemix/Helpers/glidemix-stems), then a
//! developer checkout (~/Developer/glidemix-stems, development builds only),
//! each a Python with Demucs.
std::optional<MixHouseStems::Launch> MixHouseStems::findDemucs()
{
    const auto executable = [](const QString& path) {
        const QFileInfo info(path);
        return info.isFile() && info.isExecutable();
    };
    const QStringList demucs = { QStringLiteral("-m"), QStringLiteral("demucs") };
    const QString env = qEnvironmentVariable("GLIDEMIX_STEMS_PYTHON");
    if (!env.isEmpty() && executable(env)) {
        return Launch { env, demucs };
    }
    QStringList dirs { helpersEngineDir() };
#ifdef GLIDEMIX_DEV_HELPERS
    dirs << QDir::homePath() + QStringLiteral("/Developer/glidemix-stems");
#endif
    for (const QString& dir : dirs) {
        const QString python = venvPython(dir);
        if (executable(python)) {
            return Launch { python, demucs };
        }
    }
    return std::nullopt;
}

bool MixHouseStems::available() const
{
    return findDemucs().has_value();
}

qint64 MixHouseStems::cacheSize() const
{
    return folderSize(cacheRoot());
}

void MixHouseStems::clearCache()
{
    QDir(cacheRoot()).removeRecursively();
}

//! Oldest separated songs go first once the cache is over its limit; the
//! song just separated stays.
void MixHouseStems::trimCache(const QString& keep) const
{
    const qint64 limit = qint64(configuration() ? configuration()->stemsCacheLimitGb() : 10) * 1024 * 1024 * 1024;
    QFileInfoList songs = QDir(cacheRoot()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
    qint64 total = cacheSize();
    for (const QFileInfo& song : songs) {
        if (total <= limit) {
            break;
        }
        if (song.absoluteFilePath() == keep) {
            continue;
        }
        total -= folderSize(song.absoluteFilePath());
        QDir(song.absoluteFilePath()).removeRecursively();
    }
}

bool MixHouseStems::separateClip(const WaveClip& clip, const Launch& launch, au::au3::ProgressDialog& progress, size_t index,
                                 size_t count, std::vector<std::vector<std::vector<float> > >& out)
{
    const bool high = configuration() && configuration()->stemsHighQuality();
    const QString model = high ? QStringLiteral("htdemucs_ft") : QStringLiteral("htdemucs");
    const int bars = high ? 4 : 1;   // htdemucs_ft is a bag of four models, one progress bar each
    const MixHouseStemsDevice deviceSetting = configuration() ? configuration()->stemsDevice() : MixHouseStemsDevice::Automatic;
    const QString temp = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                         + QStringLiteral("/glidemix-stem-source-%1.wav").arg(QCoreApplication::applicationPid());
    sf_count_t frames = 0;
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArray::number(clip.GetRate()) + "/" + QByteArray::number(qulonglong(clip.NChannels())));
    if (!writeClipSource(clip, temp, &frames, &hash)) {
        interactive()->errorSync(muse::trc("mixhouse", "Stem separation failed"),
                                 muse::trc("mixhouse", "Could not prepare the audio."));
        return false;
    }
    // The song's stems are kept by what they were made from: the audio
    // itself, and (in the folder below) the model.
    const QString songDir = cacheRoot() + QLatin1Char('/') + QString::fromLatin1(hash.result().toHex().left(20));
    const QString stemDir = songDir + QLatin1Char('/') + model;
    const auto haveAll = [&]() {
        return std::all_of(std::begin(kStems), std::end(kStems), [&](const StemInfo& s) {
            return QFileInfo::exists(stemDir + QLatin1Char('/') + QLatin1String(s.file) + QStringLiteral(".wav"));
        });
    };

    const QString clipLabel = muse::qtrc("mixhouse", "Separating %1 of %2").arg(index + 1).arg(count);
    if (!haveAll()) {
        QDir().mkpath(songDir);
        const auto run = [&](const QString& device) -> bool {
            QProcess process;
            process.setProcessChannelMode(QProcess::MergedChannels);
            QStringList args = launch.prefixArgs;
            args << QStringLiteral("-n") << model << QStringLiteral("-d") << device << QStringLiteral("--float32")
                 << QStringLiteral("-o") << songDir << QStringLiteral("--filename") << QStringLiteral("{stem}.{ext}") << temp;
            // The model lives in the engine's own folder (installed there),
            // so Uninstall removes it too.
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("TORCH_HOME"), helpersEngineDir() + QStringLiteral("/models"));
            env.insert(QStringLiteral("HF_HOME"), helpersEngineDir() + QStringLiteral("/models"));
            env.insert(QStringLiteral("HF_HUB_DISABLE_TELEMETRY"), QStringLiteral("1"));
            process.setProcessEnvironment(env);
            process.start(launch.program, args);
            if (!process.waitForStarted(15000)) {
                return false;
            }
            static const QRegularExpression percent(QStringLiteral("(\\d+)%\\|"));
            int bar = 0;
            int last = 0;
            QByteArray pending;
            while (process.state() != QProcess::NotRunning) {
                process.waitForReadyRead(100);
                pending += process.readAll();
                auto it = percent.globalMatch(QString::fromUtf8(pending));
                while (it.hasNext()) {
                    const int value = it.next().captured(1).toInt();
                    if (value < last && bar + 1 < bars) {
                        ++bar;   // the next model of the bag started
                    }
                    last = value;
                }
                if (pending.size() > 4096) {
                    pending = pending.right(512);
                }
                const unsigned long long done = static_cast<unsigned long long>(bar * 100 + last);
                if (progress.Poll(index * 100ull * bars + done, count * 100ull * bars,
                                  Verbatim(wxString::FromUTF8(clipLabel.toStdString()))) == ProgressResult::Cancelled) {
                    process.kill();
                    process.waitForFinished(3000);
                    return false;
                }
            }
            return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        };
        bool ok = false;
        if (deviceSetting == MixHouseStemsDevice::Cpu) {
            ok = run(QStringLiteral("cpu"));
        } else {
#ifdef Q_OS_WIN
            // NVIDIA GPUs. The installer's torch is PyPI's, CPU-only on
            // Windows, so Automatic falls back to the CPU until a CUDA
            // build of torch is installed instead.
            ok = run(QStringLiteral("cuda"));
#else
            ok = run(QStringLiteral("mps"));
#endif
            // Any GPU failure falls back to the CPU, also when GPU was
            // chosen: on Windows the installed engine is CPU-only, so an
            // old "GPU" setting would otherwise never separate anything.
            if (!ok && !progress.Cancelled()) {
                LOGW() << "GlideMix stems: GPU separation failed, trying the CPU";
                ok = run(QStringLiteral("cpu"));
            }
        }
        if (!ok || !haveAll()) {
            QFile::remove(temp);
            if (!progress.Cancelled()) {
                interactive()->errorSync(muse::trc("mixhouse", "Stem separation failed"),
                                         muse::trc("mixhouse", "The stems engine could not separate this track."));
            }
            return false;
        }
    }
    QFile::remove(temp);

    out.resize(std::size(kStems));
    for (size_t s = 0; s < std::size(kStems); ++s) {
        const QString path = stemDir + QLatin1Char('/') + QLatin1String(kStems[s].file) + QStringLiteral(".wav");
        if (!readStem(path, clip.NChannels(), frames, &out[s])) {
            interactive()->errorSync(muse::trc("mixhouse", "Stem separation failed"),
                                     muse::trc("mixhouse", "Could not read the separated stems."));
            return false;
        }
    }
    // Touch the song so the cache keeps recently used songs longest.
    QFile(songDir).setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    trimCache(songDir);
    return true;
}

bool MixHouseStems::stemsOfRange(const trackedit::ClipKey& key, double t0, double t1, RangeStems& out)
{
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return false;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    out.track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    out.clip = out.track ? au::au3::DomAccessor::findWaveClip(out.track, key.itemId) : nullptr;
    if (!out.clip) {
        return false;
    }
    const std::optional<Launch> launch = findDemucs();
    if (!launch) {
        interactive()->errorSync(muse::trc("mixhouse", "Stem separation is not installed"),
                                 muse::trc("mixhouse", "Install the stems engine in Preferences > Stems first."));
        return false;
    }
    {
        au::au3::ProgressDialog progress(iocContext(), muse::trc("mixhouse", "Separating stems"));
        if (!separateClip(*out.clip, *launch, progress, 0, 1, out.stems)) {
            return false;
        }
    }
    out.start = std::max(t0, out.clip->GetPlayStartTime());
    out.end = std::min(t1, out.clip->GetPlayEndTime());
    out.offset = out.clip->TimeToSamples(out.start - out.clip->GetPlayStartTime()).as_long_long();
    out.first = out.offset + out.clip->TimeToSamples(out.clip->GetTrimLeft()).as_long_long();
    out.count = out.clip->TimeToSamples(out.end - out.start).as_long_long();
    return out.count > 0 && out.first >= 0 && out.stems.size() == std::size(kStems);
}

namespace {
//! 0 at the range's edges, 1 inside, over ~10 ms: no click where a change starts or ends.
float edgeFade(long long i, long long count, int rate)
{
    const long long fade = std::max<long long>(1, std::min<long long>(rate / 100, count / 4));
    const long long d = std::min(i, count - 1 - i);
    return d >= fade ? 1.0f : float(d) / float(fade);
}
}

bool MixHouseStems::removeStemFromRange(const trackedit::ClipKey& key, double t0, double t1, int stem)
{
    if (stem < 0 || stem >= int(std::size(kStems))) {
        return false;
    }
    RangeStems r;
    if (!stemsOfRange(key, t0, t1, r)) {
        return false;
    }
    const auto prj = globalContext()->currentTrackeditProject();
    const int rate = r.clip->GetRate();
    std::vector<float> buffer(size_t(r.count));
    for (size_t ch = 0; ch < r.clip->NChannels(); ++ch) {
        r.clip->GetSamples(ch, reinterpret_cast<samplePtr>(buffer.data()), floatSample, sampleCount(r.offset), buffer.size(), false);
        const auto& plane = r.stems[size_t(stem)][std::min(ch, r.stems[size_t(stem)].size() - 1)];
        for (long long i = 0; i < r.count; ++i) {
            const size_t at = size_t(r.first + i);
            if (at < plane.size()) {
                buffer[size_t(i)] -= edgeFade(i, r.count, rate) * plane[at];
            }
        }
        r.clip->SetSamples(ch, reinterpret_cast<constSamplePtr>(buffer.data()), floatSample, sampleCount(r.offset),
                           buffer.size(), floatSample);
    }
    if (prj) {
        prj->notifyAboutClipChanged(au::au3::DomConverter::clip(r.track, r.clip.get()));
    }
    const QString word = muse::qtrc("mixhouse", kStems[stem].title);
    if (projectHistory()) {
        const std::string text = muse::qtrc("mixhouse", "Removed %1 from the selection").arg(word.toLower()).toStdString();
        projectHistory()->pushHistoryState(text, text);
    }
    if (activity()) {
        activity()->post(muse::qtrc("mixhouse", "Removed %1 from the selection").arg(word.toLower()));
    }
    LOGI() << "GlideMix stems: removed " << kStems[stem].file << " from " << r.start << ".." << r.end;
    return true;
}

bool MixHouseStems::splitRangeIntoStems(const trackedit::ClipKey& key, double t0, double t1)
{
    RangeStems r;
    if (!stemsOfRange(key, t0, t1, r)) {
        return false;
    }
    const auto project = globalContext()->currentProject();
    const auto prj = globalContext()->currentTrackeditProject();
    if (!project || !prj) {
        return false;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    // Which of the lane's clips it is: the copies keep the same order.
    size_t index = 0;
    for (const auto& c : r.track->Intervals()) {
        if (c == r.clip) {
            break;
        }
        ++index;
    }

    auto& tracks = TrackList::Get(*au3Project);
    auto next = tracks.Find(r.track);
    ++next;
    // Under the song's sample line, which stays right under the song.
    if (next != tracks.end() && (*next)->GetName() == wxString::FromUTF8("Sample line")) {
        ++next;
    }
    const Track* before = next != tracks.end() ? *next : nullptr;
    std::vector<trackedit::TrackId> added;
    for (size_t s = 0; s < std::size(kStems); ++s) {
        auto copy = std::static_pointer_cast<WaveTrack>(r.track->Duplicate());
        TrackList::AssignUniqueId(copy);
        copy->SetName(wxString::FromUTF8(kStems[s].title) + wxString::FromUTF8(" \xE2\x80\x94 ") + r.track->GetName());
        copy->SetMute(false);
        au::au3::TrackColor::Get(copy.get()).SetColorIndex(kStems[s].colorIndex);
        // Only the selected part of this one clip stays on the stem lane.
        std::vector<std::shared_ptr<WaveClip> > others;
        std::shared_ptr<WaveClip> mine;
        size_t i = 0;
        for (const auto& c : copy->Intervals()) {
            (i++ == index ? mine : others.emplace_back()) = c;
        }
        for (const auto& c : others) {
            copy->RemoveInterval(c);
        }
        if (!mine) {
            continue;
        }
        mine->SetColorIndex(0);
        const sampleCount trimLeft = mine->TimeToSamples(mine->GetTrimLeft());
        for (size_t ch = 0; ch < mine->NChannels(); ++ch) {
            const auto& plane = r.stems[s][std::min(ch, r.stems[s].size() - 1)];
            mine->SetSamples(ch, reinterpret_cast<constSamplePtr>(plane.data()), floatSample,
                             sampleCount(0) - trimLeft, plane.size(), floatSample);
        }
        mine->TrimLeftTo(r.start);
        mine->TrimRightTo(r.end);
        tracks.Insert(before, copy, true);
        prj->notifyAboutTrackInserted(au::au3::DomConverter::track(copy.get()),
                                      static_cast<int>(std::distance(tracks.begin(), tracks.Find(copy.get()))));
        for (const auto& clip : prj->clipList(copy->GetId())) {
            prj->notifyAboutClipAdded(clip);
        }
        added.push_back(copy->GetId());
    }

    // The song goes silent there: that part now plays from the stem lanes.
    const int rate = r.clip->GetRate();
    std::vector<float> buffer(size_t(r.count));
    for (size_t ch = 0; ch < r.clip->NChannels(); ++ch) {
        r.clip->GetSamples(ch, reinterpret_cast<samplePtr>(buffer.data()), floatSample, sampleCount(r.offset), buffer.size(), false);
        for (long long i = 0; i < r.count; ++i) {
            buffer[size_t(i)] *= 1.0f - edgeFade(i, r.count, rate);
        }
        r.clip->SetSamples(ch, reinterpret_cast<constSamplePtr>(buffer.data()), floatSample, sampleCount(r.offset),
                           buffer.size(), floatSample);
    }
    prj->notifyAboutClipChanged(au::au3::DomConverter::clip(r.track, r.clip.get()));
    if (projectHistory()) {
        projectHistory()->pushHistoryState(muse::trc("mixhouse", "Split the selection into stems"),
                                           muse::trc("mixhouse", "Split the selection into stems"));
    }
    if (clipColors()) {
        for (const auto& id : added) {
            clipColors()->setColorByKey(id, false);
        }
    }
    if (activity()) {
        activity()->post(muse::qtrc("mixhouse", "Selection split into stems"));
    }
    LOGI() << "GlideMix stems: split " << r.start << ".." << r.end << " into " << added.size() << " stem lanes";
    return !added.empty();
}

QStringList MixHouseStems::saveClipStems(const trackedit::ClipKey& key, double t0, double t1, const QList<int>& which,
                                         const QString& dir, const QString& namePattern, bool keepExisting)
{
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project || which.isEmpty()) {
        return {};
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(key.trackId));
    const auto clip = track ? au::au3::DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return {};
    }
    const std::optional<Launch> launch = findDemucs();
    if (!launch) {
        interactive()->errorSync(muse::trc("mixhouse", "Stem separation is not installed"),
                                 muse::trc("mixhouse", "Install the stems engine in Preferences > Stems first."));
        return {};
    }
    std::vector<std::vector<std::vector<float> > > stems;
    {
        au::au3::ProgressDialog progress(iocContext(), muse::trc("mixhouse", "Separating stems"));
        if (!separateClip(*clip, *launch, progress, 0, 1, stems)) {
            return {};
        }
    }
    // The range in the stems' frames: they start at the clip's whole source,
    // trimmed part included.
    const double start = std::max(t0, clip->GetPlayStartTime());
    const double end = std::min(t1, clip->GetPlayEndTime());
    const long long first = (clip->TimeToSamples(start - clip->GetPlayStartTime())
                             + clip->TimeToSamples(clip->GetTrimLeft())).as_long_long();
    const long long count = clip->TimeToSamples(end - start).as_long_long();
    const int channels = static_cast<int>(clip->NChannels());
    if (count <= 0 || first < 0) {
        return {};
    }
    QDir().mkpath(dir);
    QStringList written;
    for (const int s : which) {
        if (s < 0 || s >= int(std::size(kStems)) || size_t(s) >= stems.size()) {
            continue;
        }
        const auto& planes = stems[size_t(s)];
        const QString base = namePattern.contains(QStringLiteral("%1")) ? namePattern.arg(QLatin1String(kStems[s].file)) : namePattern;
        QString path = dir + QLatin1Char('/') + base + QStringLiteral(".wav");
        for (int n = 2; keepExisting && QFileInfo::exists(path); ++n) {
            path = dir + QLatin1Char('/') + base + QStringLiteral(" %1.wav").arg(n);
        }
        SF_INFO info {};
        info.samplerate = clip->GetRate();
        info.channels = channels;
        info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
        SNDFILE* file = au::mixhouse::openSndfile(path, SFM_WRITE, &info);
        if (!file) {
            continue;
        }
        std::vector<float> interleaved;
        constexpr long long kBlock = 65536;
        for (long long pos = 0; pos < count; pos += kBlock) {
            const long long len = std::min(kBlock, count - pos);
            interleaved.assign(size_t(len) * channels, 0.0f);
            for (long long i = 0; i < len; ++i) {
                for (int ch = 0; ch < channels && size_t(ch) < planes.size(); ++ch) {
                    const size_t at = size_t(first + pos + i);
                    interleaved[size_t(i) * channels + ch] = at < planes[ch].size() ? planes[ch][at] : 0.0f;
                }
            }
            sf_writef_float(file, interleaved.data(), len);
        }
        sf_close(file);
        written << path;
    }
    LOGI() << "GlideMix stems: saved " << written.size() << " stem samples of clip " << key.trackId << "/" << key.itemId;
    return written;
}

void MixHouseStems::separateTrack(const trackedit::TrackId& trackId)
{
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    const auto prj = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !prj) {
        return;
    }
    auto* au3Project = reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(trackId));
    if (!track || track->Intervals().empty()) {
        return;
    }
    const std::optional<Launch> launch = findDemucs();
    if (!launch) {
        interactive()->errorSync(muse::trc("mixhouse", "Stem separation is not installed"),
                                 muse::trc("mixhouse", "GlideMix could not find the stems engine."));
        return;
    }

    const bool high = configuration() && configuration()->stemsHighQuality();
    const QString model = high ? QStringLiteral("htdemucs_ft") : QStringLiteral("htdemucs");

    std::vector<std::shared_ptr<WaveClip> > clips;
    for (const auto& clip : track->Intervals()) {
        clips.push_back(clip);
    }

    au::au3::ProgressDialog progress(iocContext(), muse::trc("mixhouse", "Separating stems"));
    // Per clip: its four stems, as planes per channel.
    std::vector<std::vector<std::vector<std::vector<float> > > > stems(clips.size());

    for (size_t c = 0; c < clips.size(); ++c) {
        if (!separateClip(*clips[c], *launch, progress, c, clips.size(), stems[c])) {
            return;
        }
    }

    // Each stem track is a copy of the original -- same clips, positions,
    // trims, tempo stretch and beat grid -- with the stem's audio written
    // over each clip's samples.
    auto& tracks = TrackList::Get(*au3Project);
    auto next = tracks.Find(track);
    ++next;
    // Under the song's sample line, which stays right under the song.
    if (next != tracks.end() && (*next)->GetName() == wxString::FromUTF8("Sample line")) {
        ++next;
    }
    const Track* before = next != tracks.end() ? *next : nullptr;
    std::vector<trackedit::TrackId> added;
    for (size_t s = 0; s < std::size(kStems); ++s) {
        auto copy = std::static_pointer_cast<WaveTrack>(track->Duplicate());
        TrackList::AssignUniqueId(copy);
        // GlideMix: the stem first ("Drums \u2014 <song>"): the lane header
        // shows the start of the name, and every stem of a song looked alike.
        copy->SetName(wxString::FromUTF8(kStems[s].title) + wxString::FromUTF8(" \xE2\x80\x94 ") + track->GetName());
        copy->SetMute(false);
        au::au3::TrackColor::Get(copy.get()).SetColorIndex(kStems[s].colorIndex);
        size_t c = 0;
        for (const auto& clip : copy->Intervals()) {
            if (c >= stems.size()) {
                break;
            }
            clip->SetColorIndex(0);   // follow the stem track's colour
            const sampleCount trimLeft = clip->TimeToSamples(clip->GetTrimLeft());
            for (size_t ch = 0; ch < clip->NChannels(); ++ch) {
                const auto& plane = stems[c][s][ch];
                clip->SetSamples(ch, reinterpret_cast<constSamplePtr>(plane.data()), floatSample,
                                 sampleCount(0) - trimLeft, plane.size(), floatSample);
            }
            ++c;
        }
        tracks.Insert(before, copy, true);
        prj->notifyAboutTrackInserted(au::au3::DomConverter::track(copy.get()),
                                      static_cast<int>(std::distance(tracks.begin(), tracks.Find(copy.get()))));
        for (const auto& clip : prj->clipList(copy->GetId())) {
            prj->notifyAboutClipAdded(clip);
        }
        added.push_back(copy->GetId());
    }

    track->SetMute(true);
    prj->notifyAboutTrackChanged(au::au3::DomConverter::track(track));
    if (projectHistory()) {
        projectHistory()->pushHistoryState(muse::trc("mixhouse", "Separated stems"), muse::trc("mixhouse", "Separate stems"));
    }
    // Four copies of one song all tinted by the same key say nothing: show
    // each stem track in its own colour.
    if (clipColors()) {
        for (const auto& id : added) {
            clipColors()->setColorByKey(id, false);
        }
    }
    LOGI() << "GlideMix stems: separated track " << trackId << " with " << model.toStdString();
    if (activity()) {
        activity()->post(muse::qtrc("mixhouse", "Stems ready: %1").arg(QString::fromStdString(track->GetName().ToStdString())));
    }
}

namespace {
#ifdef Q_OS_WIN
//! The installer on Windows, run with Windows PowerShell 5.1 (in every
//! Windows 10/11): the bash one below, step for step, with the same STEP
//! lines and checks. The destination comes in $env:GLIDEMIX_STEMS_DEST.
//! Differences: uv's Windows zip, the venv's Python is .venv\Scripts\python.exe,
//! and after the move only pyvenv.cfg needs pointing at the new folder (a
//! Windows venv finds its base Python through pyvenv.cfg's "home"; no symlink).
const char* kInstallScript = R"PS(
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
trap { Write-Output "ERROR $_"; exit 1 }
function Check { if ($LASTEXITCODE -ne 0) { Write-Output "ERROR exit code $LASTEXITCODE"; exit 1 } }
$Dest = $env:GLIDEMIX_STEMS_DEST
if (-not $Dest) { Write-Output "ERROR no destination"; exit 1 }
$Tmp = "$Dest.installing"
if (Test-Path -LiteralPath $Tmp) { Remove-Item -LiteralPath $Tmp -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Tmp | Out-Null
$Arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'aarch64' } else { 'x86_64' }
Write-Output "STEP 1/4 Downloading"
Invoke-WebRequest -UseBasicParsing -Uri "https://github.com/astral-sh/uv/releases/latest/download/uv-$Arch-pc-windows-msvc.zip" -OutFile "$Tmp\uv.zip"
Expand-Archive -LiteralPath "$Tmp\uv.zip" -DestinationPath "$Tmp\uv" -Force
$Uv = (Get-ChildItem -LiteralPath "$Tmp\uv" -Recurse -Filter 'uv.exe' | Select-Object -First 1).FullName
if (-not $Uv) { Write-Output "ERROR uv.exe not found"; exit 1 }
$env:UV_CACHE_DIR = "$Tmp\cache"
$env:UV_PYTHON_INSTALL_DIR = "$Tmp\python"
Write-Output "STEP 2/4 Downloading"
& $Uv venv --quiet --python 3.12 --python-preference only-managed "$Tmp\.venv"; Check
Write-Output "STEP 3/4 Downloading (about 1 GB)"
& $Uv pip install --quiet --python "$Tmp\.venv\Scripts\python.exe" "demucs==4.1.0" "soundfile" "torch"; Check
Write-Output "STEP 4/4 Downloading"
$env:TORCH_HOME = "$Tmp\models"
$env:HF_HOME = "$Tmp\models"
$env:HF_HUB_DISABLE_TELEMETRY = '1'
& "$Tmp\.venv\Scripts\python.exe" -c "from demucs.pretrained import get_model; get_model('htdemucs')" | Out-Null; Check
Remove-Item -LiteralPath "$Tmp\cache", "$Tmp\uv", "$Tmp\uv.zip" -Recurse -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $Dest) { Remove-Item -LiteralPath $Dest -Recurse -Force }
Move-Item -LiteralPath $Tmp -Destination $Dest
# The environment was made in the temporary folder and names it: point it
# at the moved one (written back as UTF-8 without a BOM), then make sure it runs.
$Cfg = "$Dest\.venv\pyvenv.cfg"
[IO.File]::WriteAllText($Cfg, [IO.File]::ReadAllText($Cfg).Replace($Tmp, $Dest))
& "$Dest\.venv\Scripts\python.exe" -c "import demucs, torch" | Out-Null; Check
Write-Output "DONE"
)PS";
#else
//! The installer, run with bash. Each "STEP" line is shown in Preferences --
//! a count only: what is downloaded is not named there (Chris, 2026-10-27).
//! uv (Astral's Python installer) brings its own Python, so nothing on the
//! Mac is touched outside the one folder.
const char* kInstallScript = R"SH(
set -euo pipefail
DEST="$1"
TMP="$DEST.installing"
rm -rf "$TMP"
mkdir -p "$TMP"
case "$(uname -m)" in
    arm64) ARCH=aarch64 ;;
    *) ARCH=x86_64 ;;
esac
echo "STEP 1/4 Downloading"
curl -fsSL "https://github.com/astral-sh/uv/releases/latest/download/uv-$ARCH-apple-darwin.tar.gz" -o "$TMP/uv.tar.gz"
tar -xzf "$TMP/uv.tar.gz" -C "$TMP" --strip-components 1
export UV_CACHE_DIR="$TMP/cache"
export UV_PYTHON_INSTALL_DIR="$TMP/python"
echo "STEP 2/4 Downloading"
"$TMP/uv" venv --quiet --python 3.12 --python-preference only-managed "$TMP/.venv"
echo "STEP 3/4 Downloading (about 1 GB)"
"$TMP/uv" pip install --quiet --python "$TMP/.venv/bin/python" "demucs==4.1.0" "soundfile" "torch"
echo "STEP 4/4 Downloading"
TORCH_HOME="$TMP/models" HF_HOME="$TMP/models" HF_HUB_DISABLE_TELEMETRY=1 "$TMP/.venv/bin/python" -c "from demucs.pretrained import get_model; get_model('htdemucs')" >/dev/null 2>&1
rm -rf "$TMP/cache" "$TMP/uv.tar.gz"
rm -rf "$DEST"
mv "$TMP" "$DEST"
# The environment was made in the temporary folder and names it: point its
# Python at the moved one, then make sure it runs.
PYBIN="$(ls -d "$DEST"/python/cpython-3.12*/bin/python3.12 | head -1)"
ln -sfn "$PYBIN" "$DEST/.venv/bin/python"
sed -i '' "s|$TMP|$DEST|g" "$DEST/.venv/pyvenv.cfg"
"$DEST/.venv/bin/python" -c "import demucs, torch" >/dev/null
echo "DONE"
)SH";
#endif
}

QString MixHouseStems::helpersEngineDir()
{
    return paths::glidemixDataPath(QStringLiteral("Helpers/glidemix-stems"));
}

bool MixHouseStems::installing() const
{
    return m_installer != nullptr;
}

void MixHouseStems::installEngine()
{
    if (m_installer) {
        return;
    }
    QDir().mkpath(QFileInfo(helpersEngineDir()).absolutePath());
    m_installer = new QProcess();
    m_installer->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(m_installer, &QProcess::readyReadStandardOutput, m_installer, [this]() {
        while (m_installer && m_installer->canReadLine()) {
            const QString line = QString::fromUtf8(m_installer->readLine()).trimmed();
            if (line.startsWith(QStringLiteral("STEP "))) {
                m_installProgress.send(line.mid(5));
                if (activity()) {
                    activity()->set(QStringLiteral("stems-install"),
                                    muse::qtrc("mixhouse", "Installing the stems engine: %1").arg(line.mid(5)));
                }
            }
            LOGI() << "GlideMix stems install: " << line;
        }
    });
    QObject::connect(m_installer, &QProcess::finished, m_installer, [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        m_installer->deleteLater();
        m_installer = nullptr;
        if (!ok) {
            QDir(helpersEngineDir() + QStringLiteral(".installing")).removeRecursively();
        }
        m_installProgress.send(ok ? muse::qtrc("mixhouse", "Installed") : muse::qtrc("mixhouse", "The install did not finish"));
        m_installFinished.send(ok);
        if (activity()) {
            activity()->finish(QStringLiteral("stems-install"), ok ? muse::qtrc("mixhouse", "The stems engine is installed")
                                                                  : muse::qtrc("mixhouse", "The stems engine install did not finish"));
        }
    });
    m_installProgress.send(muse::qtrc("mixhouse", "Starting"));
#ifdef Q_OS_WIN
    // -EncodedCommand (base64 of UTF-16LE) sidesteps command-line quoting;
    // the destination goes in through the environment for the same reason.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GLIDEMIX_STEMS_DEST"), QDir::toNativeSeparators(helpersEngineDir()));
    m_installer->setProcessEnvironment(env);
    const QString script = QString::fromUtf8(kInstallScript);
    const QByteArray encoded = QByteArray(reinterpret_cast<const char*>(script.utf16()),
                                          script.size() * qsizetype(sizeof(char16_t))).toBase64();
    QString powershell = qEnvironmentVariable("SystemRoot") + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe");
    if (!QFileInfo::exists(powershell)) {
        powershell = QStringLiteral("powershell.exe");
    }
    m_installer->start(powershell, { QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                                     QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                                     QStringLiteral("-EncodedCommand"), QString::fromLatin1(encoded) });
#else
    m_installer->start(QStringLiteral("/bin/bash"), { QStringLiteral("-c"), QString::fromUtf8(kInstallScript),
                                                      QStringLiteral("glidemix-stems-install"), helpersEngineDir() });
#endif
}

void MixHouseStems::cancelInstall()
{
    if (m_installer) {
        m_installer->kill();   // finished() tidies up
    }
}

bool MixHouseStems::engineInHelpers() const
{
    return QFileInfo(venvPython(helpersEngineDir())).isExecutable();
}

void MixHouseStems::uninstallEngine()
{
    if (!m_installer) {
        QDir(helpersEngineDir()).removeRecursively();
    }
}

muse::async::Channel<QString> MixHouseStems::installProgress() const
{
    return m_installProgress;
}

muse::async::Channel<bool> MixHouseStems::installFinished() const
{
    return m_installFinished;
}
