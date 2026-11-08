/*
 * GlideMix: the tags of an audio file (TagLib), for adding it to the library.
 */
#pragma once

#include <QFileInfo>
#include <QMap>
#include <QString>

namespace au::mixhouse {
struct MixHouseFileTags {
    QString title;    ///< the file name (without extension) when the file has no title tag
    QString artist;
    QString album;
    QString genre;
    QString year;
    QString comment;   ///< GlideMix: the Comment tag (Mixed In Key writes its key and energy here)
    bool fromTags = false;   ///< the title came from the file's tags
    double replayGain = 0.0;       ///< REPLAYGAIN_TRACK_GAIN as a linear ratio, as library.replaygain keeps it; 0 = none
    double replayGainPeak = -1.0;  ///< REPLAYGAIN_TRACK_PEAK; -1 = none, like library.replaygain_peak

    //! GlideMix: Mixed In Key writes "1A - Energy 8" (or "1A - 8") into the
    //! Comment tag. Only that clear pattern counts; 0 / empty = not there.
    int mikEnergy = 0;
    QString mikKey;            ///< Camelot, "1A".."12B"
    //! The key tag (TKEY / INITIALKEY), as written ("Gm", "6A", ...).
    QString keyTag;
    //! Serato's beat grid, kept in an MP3's ID3 frame "Serato BeatGrid".
    double seratoBpm = 0.0;
    double seratoFirstBeatSec = 0.0;
    //! GlideMix (Marc Mont): every other text tag, by its name in capitals
    //! ("MOOD", "MUSICBRAINZ_TRACKID", "ENERGY"), for custom tag columns.
    QMap<QString, QString> extra;
};

//! "1A - Energy 8", "8A - 7", "12B-Energy 10" -> key "1A" and energy 8.
bool parseMixedInKeyComment(const QString& comment, QString& camelot, int& energy);

MixHouseFileTags readFileTags(const QString& filePath);
}
