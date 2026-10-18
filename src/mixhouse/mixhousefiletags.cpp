/*
 * GlideMix
 */
#include "mixhousefiletags.h"
#include "mixhouse/mixhousenativepath.h"

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tstring.h>

using namespace au::mixhouse;

namespace {
QString fromTagLib(const TagLib::String& s)
{
    // Non-breaking spaces (common in shop tags and file names) become plain ones.
    return QString::fromStdString(s.to8Bit(true)).replace(QChar(0x00A0), QLatin1Char(' ')).trimmed();
}
}

MixHouseFileTags au::mixhouse::readFileTags(const QString& filePath)
{
    MixHouseFileTags tags;
    TagLib::FileRef f = au::mixhouse::openTagFile(filePath);
    if (!f.isNull() && f.tag()) {
        const TagLib::Tag* t = f.tag();
        tags.title = fromTagLib(t->title());
        tags.artist = fromTagLib(t->artist());
        tags.album = fromTagLib(t->album());
        tags.genre = fromTagLib(t->genre());
        tags.year = t->year() > 0 ? QString::number(t->year()) : QString();
    }
    tags.fromTags = !tags.title.isEmpty();
    if (tags.title.isEmpty()) {
        tags.title = QFileInfo(filePath).completeBaseName().replace(QChar(0x00A0), QLatin1Char(' '));
    }
    return tags;
}
