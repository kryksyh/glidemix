/*
 * GlideMix
 *
 * High-definition waveforms, drawn the way djay draws them: bass, mids and
 * highs each measured 150 times a second, then painted as three layered
 * envelopes (red bass, green mids, blue highs) whose colours blend by which
 * band is strongest, bright in the middle and grainy at the edge.
 *
 * One analysis per audio file, done in the background the first time the
 * file is drawn and kept on disk, so the timeline clips and the library's
 * Waveform Overview column show the same picture.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <QImage>
#include <QString>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

namespace au::mixhouse {
/// A song's three band levels, 0..255 each, `rate` columns per second of the
/// file. Already on the drawing scale (a 32 dB range against the song's own
/// loud parts), so painting only has to look them up.
struct MixHouseHdWave {
    double rate = 150.0;
    double seconds = 0.0;
    std::vector<uint8_t> low, mid, high;
    //! Each band's loud level (mean square) the levels are measured against,
    //! so a close-up computed from the audio itself matches the stored picture.
    double refLow = 1.0, refMid = 1.0, refHigh = 1.0;
    size_t size() const { return low.size(); }
};
using MixHouseHdWavePtr = std::shared_ptr<const MixHouseHdWave>;

/// Paints `wave` from file second `fromSec` to `toSec` into `image` (whole
/// image), style E. The grain is tied to the audio, so it does not shimmer
/// while scrolling. Transparent where there is no sound.
/// `gain`, when given, holds the clip's volume (amplitude, 1 = as analysed)
/// for each image column: the picture shrinks with it as the plain waveform does.
void paintHdWave(QImage& image, const MixHouseHdWave& wave, double fromSec, double toSec,
                 const std::vector<double>* gain = nullptr);

/// A close-up: the same picture measured from the audio itself, one column
/// per image pixel, for zooms past the stored 150 columns a second. `mono`
/// holds `warmup` samples before the painted stretch (to settle the band
/// filters) and then the painted stretch; `wave` gives the loud levels.
/// `grainStart` ties the grain to the audio (e.g. the first pixel's time in
/// milliseconds * 8).
void paintHdCloseUp(QImage& image, const MixHouseHdWave& wave, const float* mono, size_t count, size_t warmup,
                    double sampleRate, long long grainStart);

/// The colour the painter gives file seconds [fromSec, toSec): the band
/// balance there, as 0xRRGGBB. For tinting the real audio when zoomed in
/// further than the analysis resolves.
uint32_t hdWaveColour(const MixHouseHdWave& wave, double fromSec, double toSec);

/// The analysis itself (decode + three band filters), for the background
/// workers and tests. Null when the file can't be decoded.
MixHouseHdWavePtr analyzeHdWave(const QString& filePath);
/// Left and right measured separately, against the same loud levels (both
/// null when the file can't be decoded; a mono file gives the same twice).
std::pair<MixHouseHdWavePtr, MixHouseHdWavePtr> analyzeHdWaveStereo(const QString& filePath);

class IMixHouseHdWaveforms : MODULE_GLOBAL_EXPORT_INTERFACE
{
    INTERFACE_ID(IMixHouseHdWaveforms)

public:
    virtual ~IMixHouseHdWaveforms() = default;

    /// The wave of `filePath` if it is ready; otherwise null, and the file is
    /// queued for analysis (ready() fires when it is done). Thread-safe.
    virtual MixHouseHdWavePtr wave(const QString& filePath) = 0;
    /// The same for the left and right channels, analysed separately (the
    /// stereo colour waveform); kept in their own cache file.
    virtual std::pair<MixHouseHdWavePtr, MixHouseHdWavePtr> stereoWave(const QString& filePath) = 0;
    /// Fires on the main thread whenever a queued file's wave is ready.
    virtual muse::async::Notification ready() const = 0;
};
}
