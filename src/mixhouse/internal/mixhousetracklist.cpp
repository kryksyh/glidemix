/*
 * GlideMix
 */
#include "mixhousetracklist.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include "au3-mixtape-engine/MixtapeDemo.h"
#include "au3-mixtape-engine/MixtapeRenderedSequence.h"
#include "trackedit/itrackeditproject.h"

#include "log.h"

using namespace au::mixhouse;

namespace {
struct Entry {
    double start = 0.0;    // timeline seconds
    double end = 0.0;
    double at = 0.0;       // seconds into the exported file
    QString artist;
    QString title;
    QString line() const { return artist.isEmpty() ? title : artist + QStringLiteral(" - ") + title; }
};

QString clock(double seconds)
{
    const int s = static_cast<int>(std::floor(std::max(0.0, seconds)));
    const int h = s / 3600;
    const int m = (s / 60) % 60;
    const int sec = s % 60;
    return h > 0 ? QString::asprintf("%d:%02d:%02d", h, m, sec) : QString::asprintf("%02d:%02d", m, sec);
}

// CUE sheets count minutes (no hours) and 1/75 s frames.
QString cueIndex(double seconds)
{
    const long frames = std::lround(std::max(0.0, seconds) * 75.0);
    return QString::asprintf("%02ld:%02ld:%02ld", frames / (75 * 60), (frames / 75) % 60, frames % 75);
}

QString cueText(QString s)
{
    return s.replace(QLatin1Char('"'), QLatin1Char('\''));
}
}

std::string MixHouseTracklist::write(const std::string& audioPath, double t0, double t1) const
{
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || t1 <= t0) {
        return {};
    }
    static const QRegularExpression kNumber(QStringLiteral("^\\s*\\d{1,4}\\s*[-.)_]\\s*"));
    std::vector<Entry> entries;
    for (const auto& track : project->trackList()) {
        if (track.mute) {
            continue;
        }
        for (const auto& clip : project->clipList(track.id)) {
            if (clip.endTime <= t0 || clip.startTime >= t1 || clip.title.empty()) {
                continue;
            }
            Entry e;
            e.start = clip.startTime;
            e.end = clip.endTime;
            // The library's tags when the song has them, else the file name.
            const int trackId = beatGrids() ? beatGrids()->clipLibraryTrackId(clip.key) : -1;
            if (trackId >= 0 && library()) {
                IMixHouseLibrary::TrackQuery query;
                query.trackId = trackId;
                query.limit = 1;
                const auto found = library()->tracks(query);
                if (!found.empty()) {
                    e.artist = found.front().artist.trimmed();
                    e.title = found.front().title.trimmed();
                }
            }
            if (e.title.isEmpty()) {
                e.artist.clear();
                e.title = clip.title.toQString().remove(kNumber).trimmed();
            }
            // Plain text: non-breaking spaces (Lexicon's names) become spaces.
            e.artist = e.artist.replace(QChar(0x00A0), QLatin1Char(' ')).simplified();
            e.title = e.title.replace(QChar(0x00A0), QLatin1Char(' ')).simplified();
            entries.push_back(e);
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.start < b.start; });
    // A song once: its stems lanes, or a song cut into pieces, overlap it.
    std::vector<Entry> list;
    for (const Entry& e : entries) {
        const bool again = std::any_of(list.begin(), list.end(), [&e](const Entry& l) {
            return l.line() == e.line() && e.start < l.end + 1.0;
        });
        if (again) {
            for (Entry& l : list) {
                if (l.line() == e.line()) {
                    l.end = std::max(l.end, e.end);
                }
            }
            continue;
        }
        list.push_back(e);
    }
    // Times in the file: the export starts at t0 and follows the tempo line.
    const double limit = mixtape::DemoMode() ? mixtape::kDemoExportLimitSec : 1e12;
    std::vector<Entry> written;
    for (Entry e : list) {
        e.at = mixtape::RenderedSequence::OutputSeconds(t0, std::max(t0, e.start));
        if (e.at < limit) {
            written.push_back(e);
        }
    }
    if (written.empty()) {
        return {};
    }

    const QFileInfo audio(QString::fromStdString(audioPath));
    const QString base = audio.absolutePath() + QLatin1Char('/') + audio.completeBaseName();
    const QString name = audio.completeBaseName();

    QFile txt(base + QStringLiteral(".txt"));
    if (!txt.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        LOGW() << "Tracklist: cannot write " << txt.fileName();
        return {};
    }
    QTextStream t(&txt);
    t << name << "\n\n";
    for (const Entry& e : written) {
        t << clock(e.at) << "  " << e.line() << "\n";
    }
    t << "\nMixed with GlideMix\n";
    txt.close();

    QFile cue(base + QStringLiteral(".cue"));
    if (cue.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        const QString suffix = audio.suffix().toLower();
        QTextStream c(&cue);
        c << "REM COMMENT \"Mixed with GlideMix\"\n";
        c << "TITLE \"" << cueText(name) << "\"\n";
        c << "FILE \"" << cueText(audio.fileName()) << "\" " << (suffix == QLatin1String("mp3") ? "MP3" : "WAVE") << "\n";
        int n = 0;
        for (const Entry& e : written) {
            c << QString::asprintf("  TRACK %02d AUDIO\n", ++n);
            c << "    TITLE \"" << cueText(e.title) << "\"\n";
            if (!e.artist.isEmpty()) {
                c << "    PERFORMER \"" << cueText(e.artist) << "\"\n";
            }
            c << "    INDEX 01 " << cueIndex(e.at) << "\n";
        }
        cue.close();
    }
    LOGI() << "Tracklist: " << written.size() << " songs written to " << txt.fileName();
    return txt.fileName().toStdString();
}
