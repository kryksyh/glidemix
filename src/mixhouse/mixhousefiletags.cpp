/*
 * GlideMix
 */
#include "mixhousefiletags.h"
#include "mixhouse/mixhousenativepath.h"

#include <cmath>

#include <taglib/fileref.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>

using namespace au::mixhouse;

namespace {
QString fromTagLib(const TagLib::String& s)
{
    // Non-breaking spaces (common in shop tags and file names) become plain ones.
    return QString::fromStdString(s.to8Bit(true)).replace(QChar(0x00A0), QLatin1Char(' ')).trimmed();
}

//! "-7.03 dB" or "0.98" -> the number; false when there is none.
bool leadingNumber(QString text, double& out)
{
    text = text.trimmed();
    if (text.endsWith(QLatin1String("db"), Qt::CaseInsensitive)) {
        text.chop(2);
    }
    bool ok = false;
    out = text.trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    return ok && std::isfinite(out);
}

QString replayGainText(const TagLib::File* file, const char* propertyKey, const char* mp4Name)
{
    const TagLib::PropertyMap props = file->properties();
    const auto it = props.find(propertyKey);
    if (it != props.end() && !it->second.isEmpty()) {
        return fromTagLib(it->second.front());
    }
    // MP4 keeps them as iTunes freeform atoms the property map may leave out.
    if (const auto* mp4 = dynamic_cast<const TagLib::MP4::File*>(file); mp4 && mp4->tag()) {
        for (const auto& item : mp4->tag()->itemMap()) {
            if (item.first.upper() == TagLib::String(mp4Name).upper() && !item.second.toStringList().isEmpty()) {
                return fromTagLib(item.second.toStringList().front());
            }
        }
    }
    return {};
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
    if (!f.isNull() && f.file()) {
        double value = 0.0;
        if (leadingNumber(replayGainText(f.file(), "REPLAYGAIN_TRACK_GAIN", "----:com.apple.iTunes:replaygain_track_gain"), value)) {
            tags.replayGain = std::pow(10.0, value / 20.0);
        }
        if (leadingNumber(replayGainText(f.file(), "REPLAYGAIN_TRACK_PEAK", "----:com.apple.iTunes:replaygain_track_peak"), value)
            && value >= 0.0) {
            tags.replayGainPeak = value;
        }
    }
    tags.fromTags = !tags.title.isEmpty();
    if (tags.title.isEmpty()) {
        tags.title = QFileInfo(filePath).completeBaseName().replace(QChar(0x00A0), QLatin1Char(' '));
    }
    return tags;
}
