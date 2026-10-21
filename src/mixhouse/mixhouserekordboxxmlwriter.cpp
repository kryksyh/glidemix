/*
 * GlideMix
 */
#include "mixhouserekordboxxmlwriter.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QUrl>
#include <QXmlStreamWriter>

namespace au::mixhouse {
namespace {
// rekordbox's file URL: file://localhost/ and the path percent-encoded.
QString locationUrl(const QString& path)
{
    QString url = QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded());
    if (url.startsWith(u"file:///")) {
        url = QStringLiteral("file://localhost/") + url.mid(8);
    }
    return url;
}

QString kindFor(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == u"mp3") return QStringLiteral("MP3 File");
    if (ext == u"wav") return QStringLiteral("WAV File");
    if (ext == u"aif" || ext == u"aiff") return QStringLiteral("AIFF File");
    if (ext == u"flac") return QStringLiteral("FLAC File");
    if (ext == u"m4a" || ext == u"aac" || ext == u"mp4") return QStringLiteral("M4A File");
    if (ext == u"ogg") return QStringLiteral("OGG File");
    return ext.toUpper() + QStringLiteral(" File");
}

QString dateOnly(const QString& text)
{
    QString t = text.trimmed();
    t.replace(QLatin1Char(' '), QLatin1Char('T'));
    const QDateTime d = QDateTime::fromString(t, Qt::ISODate);
    return d.isValid() ? d.date().toString(QStringLiteral("yyyy-MM-dd")) : text.left(10);
}

void writePlaylist(QXmlStreamWriter& xml, const MixHouseXmlPlaylist& p, const QHash<QString, int>& ids)
{
    xml.writeStartElement(QStringLiteral("NODE"));
    xml.writeAttribute(QStringLiteral("Name"), p.name);
    if (p.folder) {
        xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("Count"), QString::number(p.children.size()));
        for (const MixHouseXmlPlaylist& c : p.children) {
            writePlaylist(xml, c, ids);
        }
    } else {
        QList<int> keys;
        for (const QString& f : p.files) {
            const auto it = ids.constFind(f);
            if (it != ids.constEnd()) {
                keys << *it;
            }
        }
        xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("1"));
        xml.writeAttribute(QStringLiteral("KeyType"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("Entries"), QString::number(keys.size()));
        for (const int key : keys) {
            xml.writeEmptyElement(QStringLiteral("TRACK"));
            xml.writeAttribute(QStringLiteral("Key"), QString::number(key));
        }
    }
    xml.writeEndElement();
}
}

QString rekordboxColourFor(int rgb)
{
    // GlideMix's palette (rekordbox's 8, as rekordbox paints them) -> the
    // values rekordbox writes in its own XML.
    static const QHash<int, QString> colours = {
        { 0xF870F8, "0xFF007F" }, { 0xF84C4C, "0xFF0000" }, { 0xF8A030, "0xFFA500" }, { 0xF8E331, "0xFFFF00" },
        { 0x1EE000, "0x00FF00" }, { 0x10B1F8, "0x25FDE9" }, { 0x3264F8, "0x0000FF" }, { 0x9050F8, "0x660099" },
    };
    return colours.value(rgb);
}

QString writeRekordboxXml(const QString& path, const MixHouseXmlExport& data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return file.errorString();
    }
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(2);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("DJ_PLAYLISTS"));
    xml.writeAttribute(QStringLiteral("Version"), QStringLiteral("1.0.0"));
    xml.writeEmptyElement(QStringLiteral("PRODUCT"));
    xml.writeAttribute(QStringLiteral("Name"), QStringLiteral("GlideMix"));
    xml.writeAttribute(QStringLiteral("Version"), data.productVersion);
    xml.writeAttribute(QStringLiteral("Company"), QStringLiteral("GlideMix"));

    QHash<QString, int> ids;
    xml.writeStartElement(QStringLiteral("COLLECTION"));
    xml.writeAttribute(QStringLiteral("Entries"), QString::number(data.tracks.size()));
    int nextId = 1;
    for (const MixHouseTrack& t : data.tracks) {
        if (t.filePath.isEmpty() || ids.contains(t.filePath)) {
            continue;
        }
        const int id = nextId++;
        ids.insert(t.filePath, id);
        xml.writeStartElement(QStringLiteral("TRACK"));
        const auto attr = [&](const char* name, const QString& value) { xml.writeAttribute(QLatin1String(name), value); };
        attr("TrackID", QString::number(id));
        attr("Name", t.title.isEmpty() ? QFileInfo(t.filePath).completeBaseName() : t.title);
        attr("Artist", t.artist);
        attr("Composer", t.composer);
        attr("Album", t.album);
        attr("Grouping", t.grouping);
        attr("Genre", t.genre);
        attr("Kind", kindFor(t.filePath));
        attr("Size", QString::number(QFileInfo(t.filePath).size()));
        attr("TotalTime", QString::number(static_cast<int>(std::lround(std::max(0.0, t.durationSec)))));
        attr("TrackNumber", t.trackNumber.section(QLatin1Char('/'), 0, 0));
        attr("Year", t.year.left(4));
        attr("AverageBpm", QString::number(t.bpm, 'f', 2));
        attr("DateAdded", dateOnly(t.dateAdded));
        attr("BitRate", QString::number(t.bitrate));
        attr("SampleRate", QString::number(t.sampleRate));
        attr("Comments", t.comment);
        attr("PlayCount", QString::number(t.timesPlayed));
        attr("Rating", QString::number(std::clamp(t.rating, 0, 5) * 51));
        attr("Location", locationUrl(t.filePath));
        attr("Tonality", t.key);
        const QString colour = t.color >= 0 ? rekordboxColourFor(t.color) : QString();
        if (!colour.isEmpty()) {
            attr("Colour", colour);
        }
        // The beat grid: one TEMPO from the first beat (rekordbox wants it
        // inside the track), counting bars from the downbeat when known.
        if (t.beats.valid && t.beats.bpm > 0.0 && t.beats.beatLengthSec > 0.0) {
            double first = t.beats.firstBeatSec;
            int beat = 0;
            if (first < 0.0) {
                const int skip = static_cast<int>(std::ceil(-first / t.beats.beatLengthSec));
                first += skip * t.beats.beatLengthSec;
                beat += skip;
            }
            int battito = 1;
            if (t.downbeatSec >= 0.0) {
                const long n = std::lround((first - t.downbeatSec) / t.beats.beatLengthSec);
                battito = static_cast<int>(((n % 4) + 4) % 4) + 1;
            } else {
                battito = (beat % 4) + 1;
            }
            xml.writeEmptyElement(QStringLiteral("TEMPO"));
            xml.writeAttribute(QStringLiteral("Inizio"), QString::number(first, 'f', 3));
            xml.writeAttribute(QStringLiteral("Bpm"), QString::number(t.beats.bpm, 'f', 2));
            xml.writeAttribute(QStringLiteral("Metro"), QStringLiteral("4/4"));
            xml.writeAttribute(QStringLiteral("Battito"), QString::number(battito));
        }
        if (data.cues) {
            for (const MixHouseCue& c : data.cues(t)) {
                xml.writeEmptyElement(QStringLiteral("POSITION_MARK"));
                xml.writeAttribute(QStringLiteral("Name"), c.name);
                xml.writeAttribute(QStringLiteral("Type"), c.loop ? QStringLiteral("4") : QStringLiteral("0"));
                xml.writeAttribute(QStringLiteral("Start"), QString::number(c.startSec, 'f', 3));
                if (c.loop && c.endSec > c.startSec) {
                    xml.writeAttribute(QStringLiteral("End"), QString::number(c.endSec, 'f', 3));
                }
                xml.writeAttribute(QStringLiteral("Num"), QString::number(c.hotCueIndex));
                if (c.color.isValid() && c.hotCueIndex >= 0) {
                    xml.writeAttribute(QStringLiteral("Red"), QString::number(c.color.red()));
                    xml.writeAttribute(QStringLiteral("Green"), QString::number(c.color.green()));
                    xml.writeAttribute(QStringLiteral("Blue"), QString::number(c.color.blue()));
                }
            }
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();   // COLLECTION

    xml.writeStartElement(QStringLiteral("PLAYLISTS"));
    MixHouseXmlPlaylist root;
    root.name = QStringLiteral("ROOT");
    root.folder = true;
    root.children = data.playlists;
    writePlaylist(xml, root, ids);
    xml.writeEndElement();   // PLAYLISTS
    xml.writeEndElement();   // DJ_PLAYLISTS
    xml.writeEndDocument();
    if (xml.hasError() || !file.commit()) {
        return QStringLiteral("could not write %1").arg(path);
    }
    return {};
}
}
