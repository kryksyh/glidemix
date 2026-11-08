/*
 * GlideMix
 */
#include "mixhousefiletags.h"
#include "mixhouse/mixhousenativepath.h"

#include <cmath>
#include <cstring>

#include <QRegularExpression>

#include <taglib/fileref.h>
#include <taglib/generalencapsulatedobjectframe.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
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

bool au::mixhouse::parseMixedInKeyComment(const QString& comment, QString& camelot, int& energy)
{
    static const QRegularExpression pattern(
        QStringLiteral("^\\s*(1[0-2]|[1-9])\\s*([AaBb])\\s*-\\s*(?:Energy\\s*)?(10|[1-9])\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = pattern.match(comment);
    if (!m.hasMatch()) {
        return false;
    }
    camelot = m.captured(1) + m.captured(2).toUpper();
    energy = m.captured(3).toInt();
    return true;
}

namespace {
//! Serato BeatGrid (GEOB): 0x01 0x00, uint32 count, then per marker a float
//! position in seconds and either a uint32 beat count (to the next marker)
//! or, for the last one, a float BPM. Big-endian.
void readSeratoGrid(TagLib::File* file, MixHouseFileTags& tags)
{
    auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(file);
    TagLib::ID3v2::Tag* id3 = mpeg ? mpeg->ID3v2Tag(false) : nullptr;
    if (!id3) {
        return;
    }
    for (TagLib::ID3v2::Frame* frame : id3->frameList("GEOB")) {
        auto* geob = dynamic_cast<TagLib::ID3v2::GeneralEncapsulatedObjectFrame*>(frame);
        if (!geob || geob->description() != "Serato BeatGrid") {
            continue;
        }
        const TagLib::ByteVector d = geob->object();
        if (d.size() < 14 || d[0] != 0x01) {
            return;
        }
        const auto u32 = [&](unsigned int at) { return d.toUInt(at, true); };
        const auto f32 = [&](unsigned int at) {
            const uint32_t bits = d.toUInt(at, true);
            float f;
            std::memcpy(&f, &bits, sizeof f);
            return static_cast<double>(f);
        };
        const unsigned int count = u32(2);
        if (count == 0 || d.size() < 6 + count * 8) {
            return;
        }
        const double first = f32(6);
        double bpm = 0.0;
        if (count == 1) {
            bpm = f32(10);
        } else {
            const unsigned int beats = u32(10);
            const double next = f32(14);
            if (beats > 0 && next > first) {
                bpm = 60.0 * beats / (next - first);
            }
        }
        if (bpm > 30.0 && bpm < 300.0 && std::isfinite(first)) {
            tags.seratoBpm = bpm;
            tags.seratoFirstBeatSec = first;
        }
        return;
    }
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
        tags.comment = fromTagLib(t->comment());
        parseMixedInKeyComment(tags.comment, tags.mikKey, tags.mikEnergy);
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
        tags.keyTag = replayGainText(f.file(), "INITIALKEY", "----:com.apple.iTunes:initialkey");
        // The tags the library has columns for already are left out.
        static const QStringList known = {
            "TITLE", "ARTIST", "ALBUM", "GENRE", "DATE", "YEAR", "COMMENT", "TRACKNUMBER", "DISCNUMBER",
            "ALBUMARTIST", "COMPOSER", "GROUPING", "CONTENTGROUP", "BPM", "INITIALKEY",
            "REPLAYGAIN_TRACK_GAIN", "REPLAYGAIN_TRACK_PEAK", "REPLAYGAIN_ALBUM_GAIN", "REPLAYGAIN_ALBUM_PEAK",
            "PICTURE", "COVERART", "METADATA_BLOCK_PICTURE", "LYRICS", "UNSYNCEDLYRICS", "ENCODEDBY", "ENCODING",
            "ENCODER", "ENCODERSETTINGS", "ENCODINGTIME", "TRACKTOTAL", "DISCTOTAL", "TOTALTRACKS", "TOTALDISCS",
        };
        const TagLib::PropertyMap props = f.file()->properties();
        for (auto it = props.begin(); it != props.end(); ++it) {
            const QString name = fromTagLib(it->first).toUpper();
            if (name.isEmpty() || known.contains(name) || name.startsWith(QLatin1String("COMMENT"))
                || name == u"LENGTH" || it->second.isEmpty()) {
                continue;
            }
            QStringList values;
            for (const TagLib::String& v : it->second) {
                values << fromTagLib(v).trimmed();
            }
            const QString value = values.join(QStringLiteral("; "));
            if (!value.isEmpty() && value.size() <= 500) {   // not lyrics or an embedded blob
                tags.extra.insert(name, value);
            }
        }
        readSeratoGrid(f.file(), tags);
    }
    tags.fromTags = !tags.title.isEmpty();
    if (tags.title.isEmpty()) {
        tags.title = QFileInfo(filePath).completeBaseName().replace(QChar(0x00A0), QLatin1Char(' '));
    }
    return tags;
}
