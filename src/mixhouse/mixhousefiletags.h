/*
 * GlideMix: the tags of an audio file (TagLib), for adding it to the library.
 */
#pragma once

#include <QFileInfo>
#include <QString>

namespace au::mixhouse {
struct MixHouseFileTags {
    QString title;    ///< the file name (without extension) when the file has no title tag
    QString artist;
    QString album;
    QString genre;
    QString year;
    bool fromTags = false;   ///< the title came from the file's tags
    double replayGain = 0.0;       ///< REPLAYGAIN_TRACK_GAIN as a linear ratio, as library.replaygain keeps it; 0 = none
    double replayGainPeak = -1.0;  ///< REPLAYGAIN_TRACK_PEAK; -1 = none, like library.replaygain_peak
};

MixHouseFileTags readFileTags(const QString& filePath);
}
