/*
 * MixHouse DJ 2 -- decode an audio file to interleaved float, for analysis.
 *
 * Mixxx's analyzers take exactly this: interleaved CSAMPLE (float) buffers plus a
 * sample rate and channel count (see Analyzer::initialize / processSamples in
 * mixxx/src/analyzer/analyzer.h). So this is the one piece the analyzer port needs
 * that Mixxx would otherwise supply via SoundSourceProxy.
 *
 * Deliberately NOT a port of mixxx/src/sources (12,265 lines of format decoders):
 * Audacity already links libsndfile, mpg123, FLAC, ogg/vorbis, opus, wavpack and
 * ffmpeg. Re-porting decoders it already has would be duplication, not fidelity.
 *
 * Also not Audacity's own Importer: that wants an AudacityProject and a
 * WaveTrackFactory and yields WaveTracks, which is the wrong shape for feeding a
 * streaming analyzer.
 */
#pragma once

#include <QString>
#include <functional>
#include <vector>

namespace au::mixhouse {
struct AudioInfo {
    bool valid = false;
    int sampleRate = 0;
    int channels = 0;
    long long frames = 0;      ///< frames per channel; 0 if the decoder cannot say
    double durationSec = 0.0;
    QString error;
};

/// Decode `path`, handing interleaved float frames to `onSamples` in chunks.
/// `onSamples(buffer, frameCount)` — buffer holds frameCount * channels floats.
/// Returns info about the stream; info.valid is false on failure.
AudioInfo decodeAudioFile(const QString& path,
                          const std::function<bool(const float*, long long)>& onSamples);

/// Header-only probe: sample rate, channels, duration. No decoding.
AudioInfo probeAudioFile(const QString& path);
}
