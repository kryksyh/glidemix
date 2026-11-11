/*
 * GlideMix: developer tool -- runs the app's own track analysis (beats, bar 1,
 * key, waveform, phrases) over a list of files and writes the results as JSON
 * Lines (one object per track, written as each finishes), for measuring the
 * detectors and for the overnight trainer. Not shipped.
 *
 *   glidemix-analyze <list.txt> <out.jsonl> [threads] [--no-key]
 *
 * Resumable: tracks already in <out.jsonl> are skipped, so a stopped run
 * continues where it left off. --no-key skips key detection.
 */
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>

#include "mixhouseanalyzer.h"
#include "mixhousephrasedetector.h"
#include "mixhousewaveform.h"

using namespace au::mixhouse;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: glidemix-analyze <list.txt> <out.jsonl> [threads] [--no-key]\n");
        return 2;
    }
    QFile list(QString::fromLocal8Bit(argv[1]));
    if (!list.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    int threads = 4;
    for (int a = 3; a < argc; ++a) {
        if (QByteArray(argv[a]) == "--no-key") {
            setMixHouseKeyDetectionEnabled(false);
        } else {
            threads = std::max(1, atoi(argv[a]));
        }
    }

    // Tracks already written by an earlier run.
    QSet<QString> done;
    QFile out(QString::fromLocal8Bit(argv[2]));
    if (out.open(QIODevice::ReadOnly)) {
        while (!out.atEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(out.readLine()).object();
            if (o.contains("path")) {
                done.insert(o.value("path").toString());
            }
        }
        out.close();
    }
    QStringList paths;
    for (QString p : QString::fromUtf8(list.readAll()).split(u'\n', Qt::SkipEmptyParts)) {
        p = p.trimmed();   // Windows lists end their lines in \r\n
        if (!p.isEmpty() && !done.contains(p)) {
            paths.append(p);
        }
    }
    if (!out.open(QIODevice::Append)) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::fprintf(stderr, "%d done before, %d to go\n", int(done.size()), int(paths.size()));

    std::atomic<int> next { 0 };
    std::atomic<int> finished { 0 };
    std::mutex mutex;
    std::vector<std::thread> pool;
    for (int w = 0; w < threads; ++w) {
        pool.emplace_back([&]() {
            for (int i; (i = next++) < paths.size();) {
                const MixHouseAnalysisResult r = analyzeTrackFile(paths[i]);
                const double downbeat = r.downbeatSec >= 0.0 ? r.downbeatSec : r.firstBeatSec;
                QJsonArray phrases;
                if (r.valid && r.bpm > 0.0) {
                    const MixHouseWaveformOverview overview = decodeWaveformOverview(qUncompress(
                        encodeWaveformOverview(r.waveformOverview, r.waveformLow, r.waveformMid, r.waveformHigh)));
                    for (const MixHousePhrase& p : detectPhrases(overview, r.durationSec, r.bpm, downbeat)) {
                        phrases.append(QJsonArray { p.startSec, p.endSec, p.label });
                    }
                }
                QJsonObject o;
                o.insert("path", paths[i]);
                o.insert("valid", r.valid);
                o.insert("bpm", r.bpm);
                o.insert("firstBeat", r.firstBeatSec);
                o.insert("downbeat", downbeat);
                o.insert("duration", r.durationSec);
                o.insert("key", r.keyId);
                o.insert("phrases", phrases);
                QJsonArray energy;
                for (float e : r.beatBandEnergy) {
                    energy.append(std::round(e * 100.0) / 100.0);
                }
                o.insert("energy", energy);
                QJsonArray features;
                for (float f : r.energyFeatures) {
                    features.append(std::round(f * 1000.0) / 1000.0);
                }
                o.insert("energy_features", features);
                o.insert("energy_rating", r.energy);
                QJsonArray soundsLike;
                for (float f : r.soundsLike) {
                    soundsLike.append(std::round(f * 1000.0) / 1000.0);
                }
                o.insert("sounds_like", soundsLike);
                QJsonArray model;
                for (const MixHousePhrase& p : r.modelPhrases) {
                    model.append(QJsonArray { p.startSec, p.endSec, p.label });
                }
                o.insert("model_phrases", model);
                const QByteArray line = QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
                std::lock_guard<std::mutex> lock(mutex);
                out.write(line);
                out.flush();
                std::fprintf(stdout, "%d/%d\n", ++finished, int(paths.size()));
                std::fflush(stdout);
            }
        });
    }
    for (std::thread& t : pool) {
        t.join();
    }
    std::fprintf(stderr, "%d tracks\n", int(finished));
    return 0;
}
