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
};

MixHouseFileTags readFileTags(const QString& filePath);
}
