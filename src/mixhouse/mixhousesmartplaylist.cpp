/*
 * GlideMix
 */
#include "mixhousesmartplaylist.h"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "imixhousekeymatch.h"
#include "mixhousesearchquery.h"

namespace au::mixhouse {
namespace {
QJsonObject ruleToJson(const MixHouseSmartRule& r)
{
    return QJsonObject { { "field", r.field }, { "op", r.op }, { "value", r.value }, { "value2", r.value2 } };
}

MixHouseSmartRule ruleFromJson(const QJsonObject& o)
{
    return { o.value("field").toString(), o.value("op").toString(), o.value("value").toString(),
             o.value("value2").toString() };
}

QDateTime parseDate(const QString& text)
{
    if (text.isEmpty()) {
        return {};
    }
    QString t = text.trimmed();
    t.replace(QLatin1Char(' '), QLatin1Char('T'));
    QDateTime d = QDateTime::fromString(t, Qt::ISODate);
    if (!d.isValid()) {
        d = QDateTime::fromString(t.left(10), QStringLiteral("yyyy-MM-dd"));
    }
    return d;
}

// "5", "5.5" or "4:30" minutes, as seconds.
double parseMinutes(const QString& text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (parts.size() == 2) {
        return parts[0].toDouble() * 60.0 + parts[1].toDouble();
    }
    return text.toDouble() * 60.0;
}

int parseColor(const QString& text)
{
    bool ok = false;
    const int v = text.toInt(&ok);
    if (ok) {
        return v;
    }
    const QColor c(text.trimmed());
    return c.isValid() ? static_cast<int>(c.rgb() & 0xFFFFFF) : -1;
}

QString textOf(const QString& field, const MixHouseTrack& t)
{
    if (field == u"title") return t.title;
    if (field == u"artist") return t.artist;
    if (field == u"album") return t.album;
    if (field == u"albumArtist") return t.albumArtist;
    if (field == u"genre") return t.genre;
    if (field == u"composer") return t.composer;
    if (field == u"grouping") return t.grouping;
    if (field == u"comment") return t.comment;
    if (field == u"fileType") return t.fileType;
    if (field == u"fileName") {
        const qsizetype slash = t.filePath.lastIndexOf(QLatin1Char('/'));
        return slash >= 0 ? t.filePath.mid(slash + 1) : t.filePath;
    }
    return {};
}

double numberOf(const QString& field, const MixHouseTrack& t)
{
    if (field == u"bpm") return t.bpm;
    if (field == u"energy") return std::round(t.energy);
    if (field == u"rating") return t.rating;
    if (field == u"year") return t.year.left(4).toDouble();
    if (field == u"duration") return t.durationSec;
    if (field == u"timesPlayed") return t.timesPlayed;
    if (field == u"bitrate") return t.bitrate;
    return 0.0;
}

double ruleNumber(const QString& field, const QString& text)
{
    return field == u"duration" ? parseMinutes(text) : text.toDouble();
}

QString camelotOf(const MixHouseTrack& t)
{
    const int keyId = t.keyId > 0 ? t.keyId : keyIdFromText(t.key);
    return keyId > 0 ? IMixHouseKeyMatch::camelot(keyId) : QString();
}

bool textMatches(const QString& op, const QString& have, const QString& want)
{
    const auto cs = Qt::CaseInsensitive;
    if (op == u"is") return have.compare(want, cs) == 0;
    if (op == u"isNot") return have.compare(want, cs) != 0;
    if (op == u"contains") return have.contains(want, cs);
    if (op == u"notContains") return !have.contains(want, cs);
    if (op == u"startsWith") return have.startsWith(want, cs);
    if (op == u"endsWith") return have.endsWith(want, cs);
    if (op == u"isEmpty") return have.trimmed().isEmpty();
    if (op == u"isNotEmpty") return !have.trimmed().isEmpty();
    return false;
}

bool numberMatches(const QString& op, double have, double a, double b)
{
    constexpr double eps = 1e-6;
    if (op == u"is") return std::abs(have - a) < 0.005 + eps;
    if (op == u"isNot") return std::abs(have - a) >= 0.005 + eps;
    if (op == u"greater") return have > a + eps;
    if (op == u"less") return have < a - eps;
    if (op == u"between") return have >= std::min(a, b) - eps && have <= std::max(a, b) + eps;
    return false;
}
}

QString MixHouseSmartPlaylist::toJson() const
{
    QJsonArray g;
    for (const auto& group : groups) {
        QJsonArray rules;
        for (const MixHouseSmartRule& r : group) {
            rules.append(ruleToJson(r));
        }
        g.append(rules);
    }
    const QJsonObject o { { "match", matchAll ? "all" : "any" }, { "source", source }, { "groups", g },
                          { "sort", sort }, { "ascending", ascending }, { "limit", limit } };
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

MixHouseSmartPlaylist MixHouseSmartPlaylist::fromJson(const QString& json)
{
    MixHouseSmartPlaylist p;
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    p.matchAll = o.value("match").toString() != u"any";
    p.source = o.value("source").toString(QStringLiteral("collection"));
    for (const QJsonValue& gv : o.value("groups").toArray()) {
        std::vector<MixHouseSmartRule> group;
        for (const QJsonValue& rv : gv.toArray()) {
            group.push_back(ruleFromJson(rv.toObject()));
        }
        if (!group.empty()) {
            p.groups.push_back(std::move(group));
        }
    }
    p.sort = o.value("sort").toString(QStringLiteral("dateAdded"));
    p.ascending = o.value("ascending").toBool(false);
    p.limit = std::max(0, o.value("limit").toInt(0));
    return p;
}

const std::vector<MixHouseSmartField>& smartFields()
{
    static const std::vector<MixHouseSmartField> fields = {
        { "title", "Title", "text" },
        { "artist", "Artist", "text" },
        { "album", "Album", "text" },
        { "albumArtist", "Album Artist", "text" },
        { "genre", "Genre", "text" },
        { "composer", "Composer", "text" },
        { "grouping", "Grouping", "text" },
        { "comment", "Comment", "text" },
        { "bpm", "BPM", "number" },
        { "energy", "Energy (1-10)", "number" },
        { "key", "Key", "key" },
        { "rating", "Rating", "number" },
        { "color", "Color", "color" },
        { "year", "Year", "number" },
        { "duration", "Duration (minutes)", "number" },
        { "dateAdded", "Date Added", "date" },
        { "lastPlayed", "Last Played", "date" },
        { "timesPlayed", "Play Count", "number" },
        { "analyzed", "Analyzed", "bool" },
        { "inPlaylist", "Playlist", "playlist" },
        { "search", "Search", "search" },
        { "fileName", "File Name", "text" },
        { "fileType", "File Type", "text" },
        { "bitrate", "Bitrate (kbps)", "number" },
    };
    return fields;
}

const MixHouseSmartField* smartField(const QString& id)
{
    for (const MixHouseSmartField& f : smartFields()) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

std::vector<std::pair<QString, QString> > smartOperators(const QString& type)
{
    if (type == u"text") {
        return { { "contains", "contains" }, { "notContains", "does not contain" }, { "is", "is" },
                 { "isNot", "is not" }, { "startsWith", "starts with" }, { "endsWith", "ends with" },
                 { "isEmpty", "is empty" }, { "isNotEmpty", "is not empty" } };
    }
    if (type == u"number") {
        return { { "is", "is" }, { "isNot", "is not" }, { "greater", "is greater than" },
                 { "less", "is less than" }, { "between", "is between" }, { "near", "is within ±%" } };
    }
    if (type == u"key") {
        return { { "is", "is" }, { "isNot", "is not" }, { "fitsWith", "mixes with (harmonic)" } };
    }
    if (type == u"color") {
        return { { "is", "is" }, { "isNot", "is not" }, { "isEmpty", "has no color" } };
    }
    if (type == u"date") {
        return { { "inLast", "is in the last" }, { "notInLast", "is not in the last" },
                 { "after", "is after" }, { "before", "is before" }, { "isEmpty", "never" } };
    }
    if (type == u"bool") {
        return { { "isTrue", "yes" }, { "isFalse", "no" } };
    }
    if (type == u"playlist") {
        return { { "is", "is in" }, { "isNot", "is not in" } };
    }
    if (type == u"search") {
        return { { "is", "finds" }, { "isNot", "does not find" } };
    }
    return {};
}

QStringList harmonicCamelotKeys(const QString& camelot)
{
    const QString c = camelot.trimmed().toUpper();
    if (c.size() < 2) {
        return {};
    }
    const QChar letter = c.back();
    bool ok = false;
    const int n = c.left(c.size() - 1).toInt(&ok);
    if (!ok || n < 1 || n > 12 || (letter != u'A' && letter != u'B')) {
        return {};
    }
    const auto wrap = [](int v) { return ((v - 1 + 12) % 12) + 1; };
    const QChar other = letter == u'A' ? u'B' : u'A';
    return { QString::number(n) + letter, QString::number(wrap(n - 1)) + letter,
             QString::number(wrap(n + 1)) + letter, QString::number(n) + other };
}

bool smartRuleMatches(const MixHouseSmartRule& rule, const MixHouseTrack& t, const MixHouseSmartContext& context)
{
    const MixHouseSmartField* field = smartField(rule.field);
    if (!field) {
        return false;
    }
    const QString& op = rule.op;
    const QString& type = field->type;
    if (type == u"text") {
        return textMatches(op, textOf(rule.field, t), rule.value);
    }
    if (type == u"number") {
        const double have = numberOf(rule.field, t);
        if (op == u"near") {
            // BPM within ±value2 % of value (half and double count too: a
            // 64 BPM track mixes at 128).
            const double target = ruleNumber(rule.field, rule.value);
            const double pct = std::abs(rule.value2.toDouble()) / 100.0;
            if (target <= 0.0 || have <= 0.0) {
                return false;
            }
            for (const double f : { 1.0, 0.5, 2.0 }) {
                if (rule.field != u"bpm" && f != 1.0) {
                    continue;
                }
                if (std::abs(have * f - target) <= target * pct + 1e-9) {
                    return true;
                }
            }
            return false;
        }
        return numberMatches(op, have, ruleNumber(rule.field, rule.value), ruleNumber(rule.field, rule.value2));
    }
    if (type == u"key") {
        const QString have = camelotOf(t);
        const int wantId = keyIdFromText(rule.value);
        const QString want = wantId > 0 ? IMixHouseKeyMatch::camelot(wantId) : rule.value.trimmed().toUpper();
        if (op == u"is") return !have.isEmpty() && have == want;
        if (op == u"isNot") return have != want;
        if (op == u"fitsWith") return !have.isEmpty() && harmonicCamelotKeys(want).contains(have);
        return false;
    }
    if (type == u"color") {
        if (op == u"isEmpty") return t.color < 0;
        const int want = parseColor(rule.value);
        if (op == u"is") return t.color >= 0 && t.color == want;
        if (op == u"isNot") return t.color != want;
        return false;
    }
    if (type == u"date") {
        const QDateTime have = parseDate(rule.field == u"dateAdded" ? t.dateAdded : t.lastPlayedAt);
        if (op == u"isEmpty") return !have.isValid();
        if (!have.isValid()) {
            return op == u"notInLast";   // never played: not in the last month either
        }
        if (op == u"inLast" || op == u"notInLast") {
            const int n = std::max(0, rule.value.toInt());
            QDateTime from = context.now;
            const QString unit = rule.value2.isEmpty() ? QStringLiteral("days") : rule.value2;
            if (unit == u"weeks") from = from.addDays(-7 * n);
            else if (unit == u"months") from = from.addMonths(-n);
            else if (unit == u"years") from = from.addYears(-n);
            else from = from.addDays(-n);
            const bool inside = have >= from;
            return op == u"inLast" ? inside : !inside;
        }
        const QDateTime when = parseDate(rule.value);
        if (!when.isValid()) return false;
        if (op == u"after") return have.date() > when.date();
        if (op == u"before") return have.date() < when.date();
        return false;
    }
    if (type == u"bool") {
        return op == u"isTrue" ? t.analyzed : !t.analyzed;
    }
    if (type == u"playlist") {
        const auto it = context.playlistFiles.constFind(rule.value);
        const bool inside = it != context.playlistFiles.constEnd() && it->contains(t.filePath);
        return op == u"is" ? inside : !inside;
    }
    if (type == u"search") {
        const auto it = context.searchFiles.constFind(rule.value);
        const bool found = it != context.searchFiles.constEnd() && it->contains(t.filePath);
        return op == u"is" ? found : !found;
    }
    return false;
}

bool smartPlaylistMatches(const MixHouseSmartPlaylist& p, const MixHouseTrack& t, const MixHouseSmartContext& context)
{
    if (p.groups.empty()) {
        return false;   // no rules yet: empty, like Lexicon
    }
    for (const auto& group : p.groups) {
        const bool any = std::any_of(group.begin(), group.end(),
                                     [&](const MixHouseSmartRule& r) { return smartRuleMatches(r, t, context); });
        if (p.matchAll && !any) {
            return false;
        }
        if (!p.matchAll && any) {
            return true;
        }
    }
    return p.matchAll;
}

MixHouseTrackList applySmartPlaylist(const MixHouseSmartPlaylist& p, MixHouseTrackList tracks,
                                     const MixHouseSmartContext& context)
{
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                                [&](const MixHouseTrack& t) { return !smartPlaylistMatches(p, t, context); }),
                 tracks.end());
    if (p.limit <= 0 || static_cast<int>(tracks.size()) <= p.limit) {
        return tracks;
    }
    // "The newest 50": the playlist's own order picks which ones.
    const MixHouseSmartField* field = smartField(p.sort);
    const QString type = field ? field->type : QString();
    const auto less = [&](const MixHouseTrack& a, const MixHouseTrack& b) {
        if (type == u"number") return numberOf(p.sort, a) < numberOf(p.sort, b);
        if (type == u"date") {
            return parseDate(p.sort == u"dateAdded" ? a.dateAdded : a.lastPlayedAt)
                   < parseDate(p.sort == u"dateAdded" ? b.dateAdded : b.lastPlayedAt);
        }
        if (type == u"key") return camelotOf(a) < camelotOf(b);
        return QString::compare(textOf(p.sort, a), textOf(p.sort, b), Qt::CaseInsensitive) < 0;
    };
    std::stable_sort(tracks.begin(), tracks.end(), [&](const MixHouseTrack& a, const MixHouseTrack& b) {
        return p.ascending ? less(a, b) : less(b, a);
    });
    tracks.resize(static_cast<size_t>(p.limit));
    return tracks;
}
}
