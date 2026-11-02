/*
 * GlideMix
 */
#include "mixhousestems.h"
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
    return QDir::homePath() + QStringLiteral("/Library/Application Support/Glidemix/Stems");
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
        const QString python = dir + QStringLiteral("/.venv/bin/python");
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
    const int bars = high ? 4 : 1;   // htdemucs_ft is a bag of four models, one progress bar each
    const MixHouseStemsDevice deviceSetting = configuration() ? configuration()->stemsDevice() : MixHouseStemsDevice::Automatic;

    std::vector<std::shared_ptr<WaveClip> > clips;
    for (const auto& clip : track->Intervals()) {
        clips.push_back(clip);
    }

    au::au3::ProgressDialog progress(iocContext(), muse::trc("mixhouse", "Separating stems"));
    // Per clip: its four stems, as planes per channel.
    std::vector<std::vector<std::vector<std::vector<float> > > > stems(clips.size());

    for (size_t c = 0; c < clips.size(); ++c) {
        const WaveClip& clip = *clips[c];
        const QString temp = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                             + QStringLiteral("/glidemix-stem-source-%1.wav").arg(QCoreApplication::applicationPid());
        sf_count_t frames = 0;
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(QByteArray::number(clip.GetRate()) + "/" + QByteArray::number(qulonglong(clip.NChannels())));
        if (!writeClipSource(clip, temp, &frames, &hash)) {
            interactive()->errorSync(muse::trc("mixhouse", "Stem separation failed"),
                                     muse::trc("mixhouse", "Could not prepare the audio."));
            return;
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

        const QString clipLabel = muse::qtrc("mixhouse", "Separating %1 of %2").arg(c + 1).arg(clips.size());
        if (!haveAll()) {
            QDir().mkpath(songDir);
            const auto run = [&](const QString& device) -> bool {
                QProcess process;
                process.setProcessChannelMode(QProcess::MergedChannels);
                QStringList args = launch->prefixArgs;
                args << QStringLiteral("-n") << model << QStringLiteral("-d") << device << QStringLiteral("--float32")
                     << QStringLiteral("-o") << songDir << QStringLiteral("--filename") << QStringLiteral("{stem}.{ext}") << temp;
                // The model lives in the engine's own folder (installed there),
                // so Uninstall removes it too.
                QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                env.insert(QStringLiteral("TORCH_HOME"), helpersEngineDir() + QStringLiteral("/models"));
                env.insert(QStringLiteral("HF_HOME"), helpersEngineDir() + QStringLiteral("/models"));
                env.insert(QStringLiteral("HF_HUB_DISABLE_TELEMETRY"), QStringLiteral("1"));
                process.setProcessEnvironment(env);
                process.start(launch->program, args);
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
                    if (progress.Poll(c * 100ull * bars + done, clips.size() * 100ull * bars,
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
                ok = run(QStringLiteral("mps"));
                if (!ok && !progress.Cancelled() && deviceSetting == MixHouseStemsDevice::Automatic) {
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
                return;
            }
        }
        QFile::remove(temp);

        stems[c].resize(std::size(kStems));
        for (size_t s = 0; s < std::size(kStems); ++s) {
            const QString path = stemDir + QLatin1Char('/') + QLatin1String(kStems[s].file) + QStringLiteral(".wav");
            if (!readStem(path, clip.NChannels(), frames, &stems[c][s])) {
                interactive()->errorSync(muse::trc("mixhouse", "Stem separation failed"),
                                         muse::trc("mixhouse", "Could not read the separated stems."));
                return;
            }
        }
        // Touch the song so the cache keeps recently used songs longest.
        QFile(songDir).setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
        trimCache(songDir);
    }

    // Each stem track is a copy of the original -- same clips, positions,
    // trims, tempo stretch and beat grid -- with the stem's audio written
    // over each clip's samples.
    auto& tracks = TrackList::Get(*au3Project);
    auto next = tracks.Find(track);
    ++next;
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
"$TMP/uv" venv --quiet --python 3.12 "$TMP/.venv"
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
}

QString MixHouseStems::helpersEngineDir()
{
    return QDir::homePath() + QStringLiteral("/Library/Application Support/Glidemix/Helpers/glidemix-stems");
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
    m_installer->start(QStringLiteral("/bin/bash"), { QStringLiteral("-c"), QString::fromUtf8(kInstallScript),
                                                      QStringLiteral("glidemix-stems-install"), helpersEngineDir() });
}

void MixHouseStems::cancelInstall()
{
    if (m_installer) {
        m_installer->kill();   // finished() tidies up
    }
}

bool MixHouseStems::engineInHelpers() const
{
    return QFileInfo(helpersEngineDir() + QStringLiteral("/.venv/bin/python")).isExecutable();
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
