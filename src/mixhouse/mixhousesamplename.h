/*
 * GlideMix Samples: what a sample's file and folder names say about it --
 * tempo, key and whether it is a loop or a one-shot. Tuned on a real
 * 42,000-file library of commercial and free packs (names like
 * "HTR_135_C#m_Synth_Loop_02", "158 Synth Loop (G#_130bpm)",
 * "hbpt_hat_oneshot_153", "LBG BLEEP RISE SYNTH Em 126BPM").
 */
#pragma once

#include <QString>

namespace au::mixhouse {
/// Raise when the name rules change: scanned samples are read again by name
/// (not the files) at the next scan.
constexpr int kSampleNameRulesVersion = 1;

struct SampleNameInfo {
    enum class Type { Unknown, Loop, OneShot };

    QString category;   ///< first folder under the added folder ("Kicks"); empty for files at the top
    QString pack;       ///< second folder ("Some Pack Vol 1"); empty when there is none
    QString name;       ///< file name without extension
    double bpm = 0.0;   ///< 0 = none in the name
    int keyId = 0;      ///< Mixxx key id (1-12 major C..B, 13-24 minor C..B), 0 = none
    Type type = Type::Unknown;
};

/// `relativePath` is the file's path below the added folder, with '/'
/// ("Loops/Some Pack/HTR_135_Full_Drum_Loop.wav").
SampleNameInfo parseSampleName(const QString& relativePath);

/// For a sample of unknown type: a loop when its length is close to a whole
/// number of bars (1, 2, 4, 8, 16, 32) at `bpm`, else a one-shot.
SampleNameInfo::Type guessTypeFromLength(double seconds, double bpm);

/// Bars a loop of `seconds` spans at `bpm`, rounded; 0 when not a loop length.
int loopBars(double seconds, double bpm);
}
