#include "mixhousenowplayingmodel.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/resource.h>
#endif
#ifdef Q_OS_MAC
#include <libproc.h>
#include <unistd.h>
#endif

using namespace au::mixhouse;

namespace {
// Bytes this process has read from disk so far; -1 where unknown.
double processDiskReadBytes()
{
#if defined(Q_OS_WIN)
    IO_COUNTERS io {};
    if (!GetProcessIoCounters(GetCurrentProcess(), &io)) {
        return -1.0;
    }
    return double(io.ReadTransferCount);
#elif defined(Q_OS_MAC)
    rusage_info_v2 info {};
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&info)) != 0) {
        return -1.0;
    }
    return double(info.ri_diskio_bytesread);
#else
    return -1.0;
#endif
}

// Processor time this process has used so far, in seconds.
double processCpuSeconds()
{
#ifdef Q_OS_WIN
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return -1.0;
    }
    auto secs = [](const FILETIME& f) {
        ULARGE_INTEGER v;
        v.LowPart = f.dwLowDateTime;
        v.HighPart = f.dwHighDateTime;
        return double(v.QuadPart) / 1e7;   // 100 ns units
    };
    return secs(kernel) + secs(user);
#else
    rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1.0;
    }
    auto secs = [](const timeval& t) { return double(t.tv_sec) + double(t.tv_usec) / 1e6; };
    return secs(usage.ru_utime) + secs(usage.ru_stime);
#endif
}
}

MixHouseNowPlayingModel::MixHouseNowPlayingModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(200);   // a few updates a second while playing
    QObject::connect(&m_updateTimer, &QTimer::timeout, this, [this]() { update(); });
    m_cpuTimer.setInterval(1000);
    QObject::connect(&m_cpuTimer, &QTimer::timeout, this, [this]() { sampleCpu(); });
}

void MixHouseNowPlayingModel::componentComplete()
{
    if (globalContext()) {
        if (const auto state = globalContext()->playbackState()) {
            m_playing = state->isPlaying();
            state->playbackStatusChanged().onReceive(this, [this](playback::PlaybackStatus) {
                const auto st = globalContext()->playbackState();
                const bool playing = st && st->isPlaying();
                if (playing != m_playing) {
                    m_playing = playing;
                    emit playingChanged();
                }
            });
            state->playbackPositionChanged().onReceive(this, [this](muse::secs_t) {
                if (!m_updateTimer.isActive()) {
                    m_updateTimer.start();
                }
            });
        }
        globalContext()->currentTrackeditProjectChanged().onNotify(this, [this]() {
            m_updateTimer.start();
        });
    }
    if (projectHistory()) {
        projectHistory()->historyChanged().onReceive(this, [this](trackedit::HistoryEvent) {
            m_updateTimer.start();
        });
    }
    m_cpuClock.start();
    m_lastCpuSecs = processCpuSeconds();
    m_cpuTimer.start();
    update();
}

MixHouseNowPlayingModel::SongInfo MixHouseNowPlayingModel::songFor(const QString& path)
{
    const auto it = m_songs.constFind(path);
    if (it != m_songs.constEnd()) {
        return it.value();
    }
    SongInfo info;
    if (library()) {
        IMixHouseLibrary::TrackQuery q;
        q.location = path;
        const MixHouseTrackList found = library()->tracks(q);
        if (!found.empty()) {
            info.keyId = found.front().keyId;
            info.energy = int(std::lround(found.front().energy));
        }
    }
    m_songs.insert(path, info);
    return info;
}

void MixHouseNowPlayingModel::update()
{
    QString keyText;
    QColor keyColor;
    int energy = 0;
    int songIndex = 0;
    int songCount = 0;

    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    const auto state = globalContext() ? globalContext()->playbackState() : nullptr;
    if (project && state) {
        const double pos = state->playbackPosition().to_double();
        // Every song in the mix by start time; the one under the playhead is
        // the latest to start (the incoming one in a transition).
        std::vector<trackedit::Clip> clips;
        for (const trackedit::TrackId trackId : project->trackIdList()) {
            for (const trackedit::Clip& clip : project->clipList(trackId)) {
                clips.push_back(clip);
            }
        }
        std::sort(clips.begin(), clips.end(), [](const trackedit::Clip& a, const trackedit::Clip& b) {
            return a.startTime < b.startTime;
        });
        songCount = int(clips.size());
        int best = -1;
        for (int i = 0; i < songCount; ++i) {
            if (clips[i].startTime <= pos && pos < clips[i].endTime) {
                best = i;
            }
        }
        if (best >= 0) {
            songIndex = best + 1;
            int keyId = 0;
            IMixHouseBeatGrids::ClipFile file;
            if (beatGrids() && beatGrids()->clipFile(clips[best].key, file)) {
                const SongInfo info = songFor(file.path);
                keyId = info.keyId;
                energy = info.energy;
            }
            // A transposed clip sounds in another key: show that one.
            if (keyMatch()) {
                const int sounding = keyMatch()->keyInfo(clips[best].key).effectiveKeyId;
                if (sounding > 0) {
                    keyId = sounding;
                }
            }
            if (keyId > 0) {
                const MixHouseKeyNotation notation = libraryConfiguration() ? libraryConfiguration()->keyNotation()
                                                     : MixHouseKeyNotation {};
                keyText = IMixHouseKeyMatch::keyText(keyId, notation);
                keyColor = IMixHouseKeyMatch::keyColor(keyId);
            }
        }
    }

    if (keyText != m_keyText || keyColor != m_keyColor || energy != m_energy
        || songIndex != m_songIndex || songCount != m_songCount) {
        m_keyText = keyText;
        m_keyColor = keyColor;
        m_energy = energy;
        m_songIndex = songIndex;
        m_songCount = songCount;
        emit songChanged();
    }
}

void MixHouseNowPlayingModel::sampleCpu()
{
    const double now = processCpuSeconds();
    const double wall = m_cpuClock.restart() / 1000.0;

    const double diskBytes = processDiskReadBytes();
    if (diskBytes >= 0.0 && m_lastDiskBytes >= 0.0 && wall > 0.0) {
        const double mbps = std::max(0.0, (diskBytes - m_lastDiskBytes) / wall / (1024.0 * 1024.0));
        if (std::abs(mbps - m_diskMBps) > 0.05) {
            m_diskMBps = mbps;
            emit cpuChanged();
        }
    }
    m_lastDiskBytes = diskBytes;
    if (now < 0.0 || m_lastCpuSecs < 0.0 || wall <= 0.0) {
        m_lastCpuSecs = now;
        return;
    }
    // Share of the whole processor (all cores), like a DJ app's CPU meter.
    const int cores = std::max(1, QThread::idealThreadCount());
    const int percent = std::clamp(int(std::lround((now - m_lastCpuSecs) / wall / cores * 100.0)), 0, 100);
    m_lastCpuSecs = now;
    if (percent != m_cpuPercent) {
        m_cpuPercent = percent;
        emit cpuChanged();
    }
}
