/*
 * MixHouse DJ 2
 */
#include "mixhouseanalyzerworker.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>

namespace au::mixhouse {
void MixHouseAnalyzerWorker::analyzeTrack(int trackId, const QString& filePath, bool bpmLocked,
                                          double knownBpm, double knownFirstBeat, double knownDownbeat,
                                          int knownKey, const QString& knownSource)
{
    MixHouseKnownAnalysis known;
    known.bpm = knownBpm;
    known.firstBeatSec = knownFirstBeat;
    known.downbeatSec = knownDownbeat;
    known.keyId = knownKey;
    known.source = knownSource;
    // Runs here, on this object's OWN thread (see this class's own header
    // comment for why) -- pure compute, no SQL at all. analyzeTrackFile()
    // itself is safe to call concurrently from multiple threads: it only
    // ever touches fresh, stack-local decoder/analyzer instances per call
    // (confirmed by reading it and its dependencies before this pool was
    // built), except for libmpg123's own mpg123_new()/mpg123_init(), which
    // needed real fixes of their own -- see mixhouseaudiosource.cpp's own
    // comment on the two real crashes that exposed those.
    emit progress(trackId, 0);
    int lastPercent = 0;
    QElapsedTimer sinceLastEmit;
    sinceLastEmit.start();
    const MixHouseAnalysisResult analysis = analyzeTrackFile(filePath, [&](double value) {
        const int percent = static_cast<int>(std::round(100.0 * std::clamp(value, 0.0, 1.0)));
        const bool finalizing = value >= kMixHouseAnalyzerProgressFinalizing;
        if (percent == lastPercent || (!finalizing && sinceLastEmit.elapsed() < 60)) {
            return;
        }
        lastPercent = percent;
        sinceLastEmit.restart();
        emit progress(trackId, percent);
    }, known);
    emit analyzed(trackId, bpmLocked, analysis);
}
}
