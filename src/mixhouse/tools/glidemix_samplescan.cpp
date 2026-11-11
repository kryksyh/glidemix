/*
 * GlideMix: developer tool -- scans a sample folder with the app's own
 * Samples scanner into samples.sqlite under $HOME (use a scratch HOME) and
 * prints what it found. Not shipped.
 *
 *   HOME=/tmp/x glidemix-samplescan <folder>
 */
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include <map>

#include "internal/mixhousesamples.h"

using namespace au::mixhouse;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: glidemix-samplescan <folder>\n");
        return 2;
    }
    MixHouseSamples samples;
    samples.scanProgress().onReceive(&samples, [](int done, int found) {
        if (found % 5000 == 0) {
            std::fprintf(stdout, "%d / %d\n", done, found);
            std::fflush(stdout);
        }
    });
    samples.addFolder(QString::fromLocal8Bit(argv[1]));
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        if (!samples.scanning()) {
            const auto& all = samples.samples();
            int bpm = 0, key = 0, loops = 0, bars = 0, noLength = 0;
            std::map<QString, int> categories;
            for (const MixHouseSample& s : all) {
                bpm += s.bpm > 0;
                key += s.keyId > 0;
                loops += s.loop;
                bars += s.bars > 0;
                noLength += s.durationSec <= 0.0;
                ++categories[s.category];
            }
            std::fprintf(stdout, "samples %zu  bpm %d  key %d  loops %d (with bars %d)  no length %d\n", all.size(), bpm,
                         key, loops, bars, noLength);
            for (const auto& [c, n] : categories) {
                std::fprintf(stdout, "  %s: %d\n", qPrintable(c), n);
            }
            app.quit();
        }
    });
    poll.start(1000);
    return app.exec();
}
