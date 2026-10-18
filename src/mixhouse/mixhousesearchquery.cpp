/*
 * MixHouse DJ 2
 */
#include "mixhousesearchquery.h"

#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <cmath>

namespace au::mixhouse {

namespace {

// ---------------------------------------------------------------------------
// Tokenizing. Mixxx's real splitter (searchqueryparser.cpp's
// kSplitIntoWordsRegexp) splits on spaces NOT inside a "quoted phrase", then
// separately re-stitches a field:"multi word" value across tokens
// (consumeQuotedArgument) because its regex only protects BARE quoted
// phrases. A single quote-aware character scan gets the same end result in
// one pass, including for field:"multi word" (the quote right after the
// colon simply keeps the space inside it from splitting), without needing
// the two-step split-then-restitch.
QStringList tokenize(const QString& query)
{
    QStringList tokens;
    QString current;
    bool inQuotes = false;
    for (const QChar& c : query) {
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            current += c;
        } else if (c.isSpace() && !inQuotes) {
            if (!current.isEmpty()) {
                tokens << current;
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.isEmpty()) {
        tokens << current;
    }
    return tokens;
}

QString stripQuotes(QString s)
{
    if (s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')) && s.size() >= 2) {
        s = s.mid(1, s.size() - 2);
    }
    return s;
}

// Real mixxx's kMissingFieldSearchTerm (searchquery.h:20): a token's value is
// LITERALLY the two-character text `""` (an explicit, closed, empty pair of
// quotes) when the user typed `field:""` on purpose -- as opposed to
// `field:` with nothing after the colon at all, which is just "nothing
// typed". Both strip down to an empty QString via stripQuotes(), so the
// distinction has to be made BEFORE stripping: this is that marker.
const QString kMissingFieldSearchTerm = QStringLiteral("\"\"");

// Real mixxx: SearchQueryParser::parseOrNode splits the whole query into
// AND-groups on a bare `|` or a standalone `OR` word (searchqueryparser.cpp,
// kSplitOnOrOperatorRegexp), each becoming its own database.cpp AndNode,
// then OR's the groups together -- the lookahead keeps a `|`/`OR` INSIDE a
// quoted phrase from splitting anything.
const QRegularExpression& orSplitRegexp()
{
    static const QRegularExpression re(
        QStringLiteral("(?:\\||\\bOR\\b)(?=[^\"]*(\"[^\"]*\"[^\"]*)*$)"));
    return re;
}

// ---------------------------------------------------------------------------
// Field tables, ported field-for-field from SearchQueryParser's constructor
// (library/searchqueryparser.cpp:75-147). Location/directory/crate/id are
// left out: we have no watched-directories or crate-membership search yet
// (crates are a separate feature, not a track column), and a bare numeric
// row id isn't something a MixHouse user has any reason to search by.
const QHash<QString, QString>& textFieldColumns()
{
    static const QHash<QString, QString> fields = {
        { "a", "artist" }, { "artist", "artist" },
        { "aa", "album_artist" }, { "album_artist", "album_artist" },
        { "al", "album" }, { "album", "album" },
        { "t", "title" }, { "title", "title" },
        { "g", "genre" }, { "genre", "genre" },
        { "cp", "composer" }, { "composer", "composer" },
        { "gr", "grouping" }, { "grouping", "grouping" },
        { "cm", "comment" }, { "comment", "comment" },
        { "type", "filetype" },
    };
    return fields;
}

// Real column, no CAST needed.
const QHash<QString, QString>& plainNumericFieldColumns()
{
    static const QHash<QString, QString> fields = {
        { "r", "rating" }, { "rating", "rating" },
        { "br", "bitrate" }, { "bitrate", "bitrate" },
        // Real mixxx: m_fieldToSqlColumns["pl"/"played"] << "timesplayed"
        // (searchqueryparser.cpp:132-133) -- e.g. "played:0" for never-played.
        { "pl", "timesplayed" }, { "played", "timesplayed" },
    };
    return fields;
}

QString negatedClauseOpen(bool negate) { return negate ? QStringLiteral("NOT (") : QStringLiteral("("); }
QString negatedClauseClose(bool negate) { Q_UNUSED(negate); return QStringLiteral(")"); }

// Real mixxx: NumericFilterNode::init() (library/searchquery.cpp:325-355) --
// a leading operator (<=, >=, =, <, >), else a "low-high" range, else a
// plain equality. Ported exactly, just emitting SQL + a bind instead of an
// in-memory comparison.
QString buildNumericClause(const QString& column, const QString& rawValue, bool negate,
                           int tokenIndex, std::vector<MixHouseSearchBind>& binds)
{
    static const QRegularExpression kOperatorRe(QStringLiteral("^(<=|>=|=|<|>)(.*)$"));
    QString value = rawValue;
    QString op = QStringLiteral("=");
    const QRegularExpressionMatch opMatch = kOperatorRe.match(value);
    if (opMatch.hasMatch()) {
        op = opMatch.captured(1);
        value = opMatch.captured(2);
    }

    bool ok = false;
    const double asNumber = value.toDouble(&ok);
    if (ok) {
        const QString name = QStringLiteral("nf%1").arg(tokenIndex);
        binds.push_back({ name, asNumber });
        return negatedClauseOpen(negate) + column + QStringLiteral(" ") + op
               + QStringLiteral(" :") + name + negatedClauseClose(negate);
    }

    const QStringList range = value.split(QLatin1Char('-'));
    if (range.size() == 2) {
        bool lowOk = false, highOk = false;
        const double low = range[0].toDouble(&lowOk);
        const double high = range[1].toDouble(&highOk);
        if (lowOk && highOk && low <= high) {
            const QString lo = QStringLiteral("nf%1lo").arg(tokenIndex);
            const QString hi = QStringLiteral("nf%1hi").arg(tokenIndex);
            binds.push_back({ lo, low });
            binds.push_back({ hi, high });
            return negatedClauseOpen(negate) + column + QStringLiteral(" BETWEEN :") + lo
                   + QStringLiteral(" AND :") + hi + negatedClauseClose(negate);
        }
    }
    // Doesn't parse as a number or a range -- real mixxx's NumericFilterNode
    // just never matches in this case; an always-false clause does the same.
    return negatedClauseOpen(negate) + QStringLiteral("0") + negatedClauseClose(negate);
}

// Real mixxx: YearFilterNode wraps NumericFilterNode with one difference --
// the column is `CAST(substr(year,1,4) AS INTEGER)`, since `year` can carry
// a full date string (searchquery.cpp:845-859). Our own `year` column is the
// same kind of free-text field, so the same cast applies.
QString buildYearClause(const QString& rawValue, bool negate, int tokenIndex,
                        std::vector<MixHouseSearchBind>& binds)
{
    return buildNumericClause(QStringLiteral("CAST(substr(l.year,1,4) AS INTEGER)"),
                              rawValue, negate, tokenIndex, binds);
}

// Real mixxx's NumericFilterNode::init() itself checks for the explicit-empty
// marker (searchquery.cpp:325-329, "m_bNullQuery"), whose toSql() always uses
// the RAW column, never a derived CAST -- confirmed against YearFilterNode's
// own override (searchquery.cpp:846-848, "year IS NULL", not the substr/CAST
// expression its other branches use). One shared helper for every numeric-ish
// field (rating/bitrate/played/track/year/duration/bpm all route through
// this for "field:\"\"").
QString buildNullNumericClause(const QString& rawColumn, bool negate)
{
    return negatedClauseOpen(negate) + rawColumn + QStringLiteral(" IS NULL") + negatedClauseClose(negate);
}

// Real mixxx: NullOrEmptyTextFilterNode::toSql() (searchquery.cpp:246-252) --
// used for both a text field's explicit empty (field:"") and, per
// buildKeyClause below, a key that guessKeyFromText() can't resolve at all
// when explicitly empty.
QString buildNullOrEmptyTextClause(const QString& rawColumn, bool negate)
{
    return negatedClauseOpen(negate) + rawColumn + QStringLiteral(" IS NULL OR ")
           + rawColumn + QStringLiteral(" IS '' ") + negatedClauseClose(negate);
}

// Real mixxx: DurationFilterNode::parse (searchquery.cpp:454-481) -- a plain
// number of seconds, "M:SS", or "Ns", via kDurationRegex
// ("^(\d+)(m|:)?([0-5]?\d)?s?$"). Ported literally, including the "only a
// number of seconds" branch when neither the m/: separator nor the trailing
// seconds group matched anything.
bool parseDurationSeconds(const QString& arg, double* outSeconds)
{
    static const QRegularExpression kDurationRe(
        QStringLiteral("^(\\d+)(m|:)?([0-5]?\\d)?s?$"));
    const QRegularExpressionMatch m = kDurationRe.match(arg);
    if (!m.hasMatch()) {
        return false;
    }
    bool ok = false;
    double minutes = 0.0;
    double seconds;
    if (m.captured(3).isEmpty() && m.captured(2).isEmpty()) {
        seconds = m.captured(1).toDouble(&ok);
    } else {
        minutes = m.captured(1).toDouble(&ok);
        seconds = m.captured(3).toDouble();
    }
    if (!ok) {
        return false;
    }
    *outSeconds = 60.0 * minutes + seconds;
    return true;
}

// Real mixxx: DurationFilterNode is a NumericFilterNode with parse()
// overridden to accept mm:ss/Ns text -- everything else (leading operator,
// "low-high" range, bare-number default) is identical to buildNumericClause
// above, just calling parseDurationSeconds() instead of QString::toDouble().
QString buildDurationClause(const QString& rawValue, bool negate, int tokenIndex,
                            std::vector<MixHouseSearchBind>& binds)
{
    static const QRegularExpression kOperatorRe(QStringLiteral("^(<=|>=|=|<|>)(.*)$"));
    QString value = rawValue;
    QString op = QStringLiteral("=");
    const QRegularExpressionMatch opMatch = kOperatorRe.match(value);
    if (opMatch.hasMatch()) {
        op = opMatch.captured(1);
        value = opMatch.captured(2);
    }

    double seconds = 0.0;
    if (parseDurationSeconds(value, &seconds)) {
        const QString name = QStringLiteral("du%1").arg(tokenIndex);
        binds.push_back({ name, seconds });
        return negatedClauseOpen(negate) + QStringLiteral("l.duration ") + op
               + QStringLiteral(" :") + name + negatedClauseClose(negate);
    }

    const QStringList range = value.split(QLatin1Char('-'));
    if (range.size() == 2) {
        double low = 0.0, high = 0.0;
        if (parseDurationSeconds(range[0], &low) && parseDurationSeconds(range[1], &high) && low <= high) {
            const QString lo = QStringLiteral("du%1lo").arg(tokenIndex);
            const QString hi = QStringLiteral("du%1hi").arg(tokenIndex);
            binds.push_back({ lo, low });
            binds.push_back({ hi, high });
            return negatedClauseOpen(negate) + QStringLiteral("l.duration BETWEEN :") + lo
                   + QStringLiteral(" AND :") + hi + negatedClauseClose(negate);
        }
    }
    // Same convention as buildNumericClause above: doesn't parse, always-false.
    return negatedClauseOpen(negate) + QStringLiteral("0") + negatedClauseClose(negate);
}

// Real mixxx: DateAddedFilterNode::parseDate() (searchquery.cpp:910-928).
// Parses via QLocale's short date format (so a track added under a
// different system locale still round-trips through the same locale here),
// defaults to an implicit "=" that becomes a whole-day [start, nextDay)
// range, converts a bare ">" into ">=" one day later so "added:>2025-01-01"
// excludes that day itself exactly like real mixxx.
//
// One deliberate deviation from the literal real-mixxx call, verified by
// direct experiment against Qt 6.10's actual QLocale::toDate(), not
// guessed: real mixxx passes a literal baseYear of 20 ("With 6.7+ we can
// specify the century, ie. 20 for 2000" -- its own comment). Qt's baseYear
// is a real calendar year defining a [baseYear, baseYear+99] window a
// 2-digit year gets placed into, not a "century code" -- baseYear=20 gives
// the window [20,119], so a 2-digit locale short-date format (e.g. this
// build's own en_DE default, "dd.MM.yy") parses "26" as literal year 26,
// which real mixxx's own following `if (year < 2000) addYears(100)` then
// turns into 126, not 2026. That reproduces upstream as a real but
// non-functional edge case for any 2-digit-year locale; passing the
// baseYear the comment's own English actually describes (2000) is what
// makes the SAME architecture (short-format locale parsing, the < 2000
// safety net kept below for a 4-digit-format locale's own edge cases)
// actually resolve "26" to 2026 instead of 126.
QDateTime parseAddedDate(const QString& dateStr)
{
    QDate date = QLocale().toDate(dateStr, QLocale::ShortFormat, 2000);
    if (!date.isValid()) {
        return {};
    }
    if (date.year() < 2000) {
        date = date.addYears(100);
    }
    return QDateTime(date, QTime(0, 0)).toUTC();
}

// library.datetime_added is written via SQLite's own CURRENT_TIMESTAMP
// (mixhouselibrary.cpp's addTracksFromFolder/ensureTrackForFile), which
// stores UTC as "yyyy-MM-dd HH:mm:ss" -- a space, not real mixxx's own
// dateStringIsoNoZ()'s ISO 'T' separator (that helper targets mixxx's own
// schema, which stores real ISO8601). Comparing a 'T'-separated bind
// against this column's space-separated text would silently misorder every
// query (' ' sorts before 'T', ASCII 0x20 vs 0x54), so this format has to
// match the column exactly, not real mixxx's own literal format string.
QString dateAddedSqlText(const QDateTime& date)
{
    return date.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString buildDateAddedClause(const QString& rawValue, bool negate, int tokenIndex,
                             std::vector<MixHouseSearchBind>& binds)
{
    static const QRegularExpression kOperatorRe(QStringLiteral("^(<=|>=|=|<|>)(.*)$"));
    QString value = rawValue;
    QString op = QStringLiteral("=");
    const QRegularExpressionMatch opMatch = kOperatorRe.match(value);
    if (opMatch.hasMatch()) {
        op = opMatch.captured(1);
        value = opMatch.captured(2);
    }

    const QDateTime date = parseAddedDate(value);
    if (!date.isValid()) {
        // Same always-false convention as the other date/numeric builders
        // above, kept for consistency within this file.
        return negatedClauseOpen(negate) + QStringLiteral("0") + negatedClauseClose(negate);
    }

    if (op == QLatin1String("=")) {
        const QString lo = QStringLiteral("ad%1lo").arg(tokenIndex);
        const QString hi = QStringLiteral("ad%1hi").arg(tokenIndex);
        binds.push_back({ lo, dateAddedSqlText(date) });
        binds.push_back({ hi, dateAddedSqlText(date.addDays(1)) });
        return negatedClauseOpen(negate) + QStringLiteral("l.datetime_added >= :") + lo
               + QStringLiteral(" AND l.datetime_added < :") + hi + negatedClauseClose(negate);
    }
    QString sqlOp = op;
    QDateTime cmp = date;
    if (op == QLatin1String(">")) {
        // Real mixxx: "added:>2025-01-01" -> ">=" the NEXT day, so the day
        // typed is excluded rather than half-included by time-of-day.
        cmp = date.addDays(1);
        sqlOp = QStringLiteral(">=");
    }
    const QString name = QStringLiteral("ad%1").arg(tokenIndex);
    binds.push_back({ name, dateAddedSqlText(cmp) });
    return negatedClauseOpen(negate) + QStringLiteral("l.datetime_added ") + sqlOp
           + QStringLiteral(" :") + name + negatedClauseClose(negate);
}

// Real mixxx: BpmFilterNode (searchquery.h:188-242). Ported subset: operator
// and range are identical to any other numeric field; a bare number with
// neither applies BpmFilterNode's own default "match this OR half OR double,
// each within a relative tolerance" rule (MatchMode::HalveDouble); a leading
// "~" applies just the tolerance to the bare value with no halving/doubling
// (MatchMode::Fuzzy). `s_relativeRange` defaults to 0.06 in real mixxx
// (BpmFilterNode::kRelativeRangeDefault) -- kept as the same constant here.
// Real mixxx's two BPM tolerance widths (library/searchquery.cpp/.h) --
// this port previously had these exactly backwards: the WIDE relative range
// applied unconditionally to a plain `bpm:` search (so `bpm:128` matched a
// far broader net than intended), while half/double matching -- which real
// mixxx includes in BOTH modes -- was only enabled for the FUZZY prefix.
// kLibraryRoundRange is a TIGHT ABSOLUTE tolerance (not a percentage) for a
// plain search: it exists only to catch floating-point display rounding on
// the exact integer typed, not to fuzzy-match nearby tempos. The relative
// 6% range is reserved for the explicit `~bpm:` fuzzy prefix.
constexpr double kLibraryRoundRange = 0.05;
// NOT constexpr, unlike kLibraryRoundRange above -- real mixxx's own
// BpmFilterNode::s_relativeRange is a plain mutable static too, changed
// live by DlgPrefLibrary's "fuzzy BPM search range" preference with no
// restart required. See setFuzzyBpmRelativeRange() (mixhousesearchquery.h)
// for the same thing here, wired from MixHouseLibraryPreferencesModel.
// 0.06 is real mixxx's own actual default result (its combo's own default
// choice, 75%, times its own default deck rate range, 8%: 75*8/10000).
double g_fuzzyBpmRelativeRange = 0.06;

QString buildBpmClause(const QString& rawValue, bool negate, bool fuzzy, int tokenIndex,
                       std::vector<MixHouseSearchBind>& binds)
{
    // Real mixxx: BpmFilterNode's own constructor checks the explicit-empty
    // marker before anything else (searchquery.cpp:553-558,783-785) --
    // MatchMode::Null, "bpm IS 0" (the schema's real default, never NULL).
    if (rawValue == kMissingFieldSearchTerm) {
        return negatedClauseOpen(negate) + QStringLiteral("l.bpm IS 0") + negatedClauseClose(negate);
    }

    static const QRegularExpression kOperatorRe(QStringLiteral("^(<=|>=|=|<|>)(.*)$"));

    QString value = rawValue;
    const QRegularExpressionMatch opMatch = kOperatorRe.match(value);
    if (opMatch.hasMatch() || value.contains(QLatin1Char('-'))) {
        return buildNumericClause(QStringLiteral("l.bpm"), rawValue, negate, tokenIndex, binds);
    }

    bool ok = false;
    const double bpm = value.toDouble(&ok);
    if (!ok || bpm <= 0.0) {
        return negatedClauseOpen(negate) + QStringLiteral("0") + negatedClauseClose(negate);
    }

    auto bindRange = [&](double lo, double hi, const QString& suffix) {
        const QString loName = QStringLiteral("bpm%1lo%2").arg(tokenIndex).arg(suffix);
        const QString hiName = QStringLiteral("bpm%1hi%2").arg(tokenIndex).arg(suffix);
        binds.push_back({ loName, lo });
        binds.push_back({ hiName, hi });
        return QStringLiteral("l.bpm BETWEEN :") + loName + QStringLiteral(" AND :") + hiName;
    };

    QStringList clauses;
    if (fuzzy) {
        // BpmFilterNode::MatchMode::Fuzzy (searchquery.cpp:667-674) -- wide
        // +-6% relative range, center AND half/double all included.
        auto fuzzyRange = [&](double center, const QString& suffix) {
            return bindRange(std::floor((1.0 - g_fuzzyBpmRelativeRange) * center),
                              std::ceil((1.0 + g_fuzzyBpmRelativeRange) * center), suffix);
        };
        clauses << fuzzyRange(bpm, QStringLiteral("a"));
        clauses << fuzzyRange(bpm / 2.0, QStringLiteral("b"));
        clauses << fuzzyRange(bpm * 2.0, QStringLiteral("c"));
    } else {
        // BpmFilterNode::MatchMode::HalveDouble (searchquery.cpp:677-699) --
        // tight absolute kLibraryRoundRange, deliberately asymmetric
        // (upper bound is center+1, catching any decimal mixxx would
        // display rounded down to this same integer), center AND
        // half/double all included here too.
        clauses << bindRange(bpm - kLibraryRoundRange, bpm + 1.0, QStringLiteral("a"));
        const double halfBpm = bpm / 2.0;
        const double halfLower = (std::floor(halfBpm) != halfBpm)
            ? std::floor(halfBpm) - kLibraryRoundRange
            : halfBpm - kLibraryRoundRange;
        clauses << bindRange(halfLower, halfBpm + 1.0, QStringLiteral("b"));
        clauses << bindRange((bpm * 2.0) - kLibraryRoundRange, (bpm + 1.0) * 2.0, QStringLiteral("c"));
    }
    return negatedClauseOpen(negate) + clauses.join(QStringLiteral(" OR ")) + negatedClauseClose(negate);
}

// The real ChromaticKey -> Camelot/OpenKey number table (KeyUtils::
// keyToOpenKeyNumber), already ported once for the Key column's text color
// (mixhousekeywheel.cpp) and duplicated here rather than shared across the
// QML/core module boundary mixhouselibrary.cpp itself never crosses.
constexpr int kOpenKeyNumberByChromaticKey[25] = {
    0,
    1, 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6,
    10, 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3
};

int openKeyNumberToKeyId(int openKeyNumber, bool minor)
{
    int n = ((openKeyNumber - 1) % 12 + 12) % 12 + 1;
    return minor ? (12 + kOpenKeyNumberByChromaticKey[12 + n]) : kOpenKeyNumberByChromaticKey[n];
}

// Real mixxx: KeyUtils::getCompatibleKeys (track/keyutils.cpp:748-791) --
// same key, its relative major/minor, and the perfect 4th/5th of both.
QList<int> compatibleKeyIds(int keyId)
{
    QList<int> result;
    if (keyId < 1 || keyId > 24) {
        return result;
    }
    const bool minor = keyId > 12;
    const int openKeyNumber = kOpenKeyNumberByChromaticKey[keyId];
    result << keyId;

    const int relativeKeyId = openKeyNumberToKeyId(openKeyNumber, !minor);
    const int relativeOpenKeyNumber = kOpenKeyNumberByChromaticKey[relativeKeyId];
    result << relativeKeyId;

    result << openKeyNumberToKeyId(openKeyNumber == 12 ? 1 : openKeyNumber + 1, minor);
    result << openKeyNumberToKeyId(relativeOpenKeyNumber == 12 ? 1 : relativeOpenKeyNumber + 1, !minor);
    result << openKeyNumberToKeyId(openKeyNumber == 1 ? 12 : openKeyNumber - 1, minor);
    result << openKeyNumberToKeyId(relativeOpenKeyNumber == 1 ? 12 : relativeOpenKeyNumber - 1, !minor);
    return result;
}

// A bare Camelot key, e.g. "8A" / "10B" -- digits then A (minor) or B
// (major). The Camelot number is the Open Key number rotated by 5 (8A = Am =
// Open Key 1m); it used to be passed through as an Open Key number, so
// key:8A found B-flat minor tracks.
bool tryParseCamelotKey(const QString& text, int* outKeyId)
{
    static const QRegularExpression kCamelotRe(QStringLiteral("^([1-9]|1[0-2])([ABab])$"));
    const QRegularExpressionMatch m = kCamelotRe.match(text.trimmed());
    if (!m.hasMatch()) {
        return false;
    }
    const int openKeyNumber = (m.captured(1).toInt() + 4) % 12 + 1;
    const bool minor = m.captured(2).compare(QLatin1String("A"), Qt::CaseInsensitive) == 0;
    *outKeyId = openKeyNumberToKeyId(openKeyNumber, minor);
    return true;
}

// An Open Key key, e.g. "1m" / "12d" -- m for minor, d for major.
bool tryParseOpenKey(const QString& text, int* outKeyId)
{
    static const QRegularExpression kOpenKeyRe(QStringLiteral("^([1-9]|1[0-2])([MDmd])$"));
    const QRegularExpressionMatch m = kOpenKeyRe.match(text.trimmed());
    if (!m.hasMatch()) {
        return false;
    }
    const bool minor = m.captured(2).compare(QLatin1String("M"), Qt::CaseInsensitive) == 0;
    *outKeyId = openKeyNumberToKeyId(m.captured(1).toInt(), minor);
    return true;
}

// keyId -> Traditional notation text, for tryParseTraditionalKey() below --
// duplicated from mixhouselibrary.cpp's own kKeyIdToText (same real
// proto/keys.proto ChromaticKey symbolic names), for the same reason
// kOpenKeyNumberByChromaticKey above is duplicated rather than shared: this
// file sits below the QML/core module boundary mixhouselibrary.cpp doesn't
// cross.
constexpr const char* kTraditionalKeyNames[25] = {
    "",
    "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
    "Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm"
};

// Real mixxx's KeyUtils::guessKeyFromText() also recognizes Traditional
// notation (not just Camelot) before falling back to raw text -- exact
// canonical spellings only here (not the full enharmonic alias table real
// mixxx's own guessKeyFromText carries), which is enough for what this
// library's own key column actually ever displays (kKeyIdToCamelotText) or
// analysis ever writes (this same kTraditionalKeyNames table).
bool tryParseTraditionalKey(const QString& text, int* outKeyId)
{
    const QString trimmed = text.trimmed();
    for (int id = 1; id <= 24; ++id) {
        if (trimmed.compare(QLatin1String(kTraditionalKeyNames[id]), Qt::CaseInsensitive) == 0) {
            *outKeyId = id;
            return true;
        }
    }
    return false;
}

// Any notation the Key column can show (Preferences > Music library), so
// typing what the column says always finds it -- "8A/Am" by its first half.
bool guessKeyIdFromText(const QString& text, int* outKeyId)
{
    const QString first = text.section(QLatin1Char('/'), 0, 0);
    return tryParseCamelotKey(first, outKeyId) || tryParseOpenKey(first, outKeyId)
           || tryParseTraditionalKey(first, outKeyId);
}

// Real mixxx: KeyFilterNode. Both plain `key:<text>` and fuzzy `~key:<text>`
// run the SAME guessKeyFromText() first (searchqueryparser.cpp:268-281) --
// this port previously only tried to parse Camelot on the FUZZY path,
// leaving plain `key:8A` doing a raw, un-normalized text compare against
// whatever notation happens to be stored (`l.key`), while the Key COLUMN
// itself displays normalized Camelot text computed FROM key_id
// (kKeyIdToCamelotText) -- typing exactly what the grid shows could
// silently return nothing. Falls back to the old raw-text compare only if
// neither Camelot nor Traditional parsing recognizes the input, matching
// real mixxx's own real fallback.
QString buildKeyClause(const QString& rawValue, bool negate, bool fuzzy, int tokenIndex,
                       std::vector<MixHouseSearchBind>& binds)
{
    // Real mixxx: guessKeyFromText() never resolves the explicit-empty
    // marker to a real key, so it falls to the SAME branch as any other
    // unrecognized text -- except that branch itself special-cases the
    // marker into NullOrEmptyTextFilterNode instead of a literal text
    // compare (searchqueryparser.cpp:271-278).
    if (rawValue == kMissingFieldSearchTerm) {
        return buildNullOrEmptyTextClause(QStringLiteral("l.key"), negate);
    }
    int keyId = 0;
    if (guessKeyIdFromText(rawValue, &keyId)) {
        if (fuzzy) {
            const QList<int> compatible = compatibleKeyIds(keyId);
            QStringList placeholders;
            for (int i = 0; i < compatible.size(); ++i) {
                const QString name = QStringLiteral("key%1_%2").arg(tokenIndex).arg(i);
                binds.push_back({ name, compatible[i] });
                placeholders << QStringLiteral(":") + name;
            }
            return negatedClauseOpen(negate) + QStringLiteral("l.key_id IN (")
                   + placeholders.join(QStringLiteral(", ")) + QStringLiteral(")") + negatedClauseClose(negate);
        }
        const QString name = QStringLiteral("key%1").arg(tokenIndex);
        binds.push_back({ name, keyId });
        return negatedClauseOpen(negate) + QStringLiteral("l.key_id = :") + name + negatedClauseClose(negate);
    }
    const QString name = QStringLiteral("key%1").arg(tokenIndex);
    binds.push_back({ name, rawValue });
    return negatedClauseOpen(negate) + QStringLiteral("l.key = :") + name
           + QStringLiteral(" COLLATE NOCASE") + negatedClauseClose(negate);
}

QString buildTextClause(const QString& column, const QString& rawValue, bool negate, int tokenIndex,
                        std::vector<MixHouseSearchBind>& binds)
{
    // Real mixxx: a leading "=" on the value requests StringMatch::Equals
    // instead of Contains (searchqueryparser.cpp's getTextArgument).
    QString value = rawValue;
    bool exact = false;
    if (value.startsWith(QLatin1Char('='))) {
        value = value.mid(1);
        exact = true;
    }
    const QString name = QStringLiteral("tf%1").arg(tokenIndex);
    binds.push_back({ name, exact ? value : (QStringLiteral("%") + value + QStringLiteral("%")) });
    const QString op = exact ? QStringLiteral("=") : QStringLiteral("LIKE");
    return negatedClauseOpen(negate) + column + QStringLiteral(" ") + op
           + QStringLiteral(" :") + name + QStringLiteral(" COLLATE NOCASE") + negatedClauseClose(negate);
}

// One token's own bare-word (no field prefix) clause. Real mixxx's actual
// default search column set (library/mixxxlibraryfeature.cpp:77-86):
// artist, album, album_artist, location (the file path), grouping, comment,
// title, genre, and crate name -- this port only matched title/artist/
// album, so a bare search for a genre tag, a comment note, an album-artist-
// only credit, or a grouping label found nothing even though real mixxx
// would. Crate-name matching is a real mixxx capability too but needs a
// join this port's flat per-track query doesn't have yet -- disclosed
// omission, not silently dropped.
QString buildBareClause(const QString& value, bool negate, int tokenIndex,
                        std::vector<MixHouseSearchBind>& binds)
{
    const QString name = QStringLiteral("bare%1").arg(tokenIndex);
    binds.push_back({ name, QStringLiteral("%") + value + QStringLiteral("%") });
    const QString ph = QStringLiteral(":") + name;
    static const QStringList kBareColumns = {
        QStringLiteral("l.title"), QStringLiteral("l.artist"), QStringLiteral("l.album"),
        QStringLiteral("l.album_artist"), QStringLiteral("t.location"),
        QStringLiteral("l.grouping"), QStringLiteral("l.comment"), QStringLiteral("l.genre"),
    };
    QStringList parts;
    for (const QString& col : kBareColumns) {
        parts << col + QStringLiteral(" LIKE ") + ph + QStringLiteral(" COLLATE NOCASE");
    }
    return negatedClauseOpen(negate) + parts.join(QStringLiteral(" OR ")) + negatedClauseClose(negate);
}

// One OR-segment's tokens, AND-joined -- the body of the old (pre-M6/M7/M8)
// parseSearchQuery loop, now taking a mutable token queue (so a field with
// nothing after the colon can pull in the NEXT token, mirroring real
// mixxx's getTextArgument()) and a globally-incrementing bind index shared
// across every OR-segment so bind names never collide.
QStringList clausesForTokens(QStringList tokens, int& globalIndex, std::vector<MixHouseSearchBind>& binds)
{
    QStringList clauses;
    while (!tokens.isEmpty()) {
        QString token = tokens.takeFirst();
        if (token.trimmed().isEmpty()) {
            continue;
        }
        const int i = globalIndex++;

        // Universal negate/fuzzy prefix (mixxx: kNegatePrefix "-", kFuzzyPrefix "~",
        // mutually exclusive on one token -- searchqueryparser.cpp's matcher
        // regexes are all "^[~-]?" or "^-?").
        bool negate = false;
        bool fuzzy = false;
        if (token.startsWith(QLatin1Char('-'))) {
            negate = true;
            token = token.mid(1);
        } else if (token.startsWith(QLatin1Char('~'))) {
            fuzzy = true;
            token = token.mid(1);
        }
        if (token.isEmpty()) {
            continue;
        }

        const int colon = token.indexOf(QLatin1Char(':'));
        QString field;
        QString rest;
        bool hasField = false;
        if (colon > 0) {
            const QString candidate = token.left(colon).toLower();
            if (textFieldColumns().contains(candidate) || plainNumericFieldColumns().contains(candidate)
                || candidate == QLatin1String("y") || candidate == QLatin1String("year")
                || candidate == QLatin1String("b") || candidate == QLatin1String("bpm")
                || candidate == QLatin1String("k") || candidate == QLatin1String("key")
                || candidate == QLatin1String("du") || candidate == QLatin1String("duration")
                || candidate == QLatin1String("tr") || candidate == QLatin1String("track")
                || candidate == QLatin1String("ad") || candidate == QLatin1String("added")
                || candidate == QLatin1String("dateadded") || candidate == QLatin1String("datetime_added")) {
                field = candidate;
                rest = token.mid(colon + 1);
                hasField = true;
            }
        }

        if (!hasField) {
            clauses << buildBareClause(stripQuotes(token), negate, i, binds);
            continue;
        }

        // Real mixxx's getTextArgument(): an empty value right after the
        // colon means the user put a space there ("artist: Muse") -- consume
        // the NEXT token as the argument instead of silently dropping the
        // whole field scope (searchqueryparser.cpp:172-180).
        if (rest.isEmpty() && !tokens.isEmpty()) {
            rest = tokens.takeFirst();
        }

        // A field's value is LITERALLY `""` (an explicit, closed, empty
        // quoted pair) when the user asked for "this field is blank" --
        // kMissingFieldSearchTerm, checked BEFORE stripQuotes() since
        // stripping would make it indistinguishable from "nothing typed"
        // (searchqueryparser.cpp's own consumeQuotedArgument does the same
        // check before unwrapping the quotes).
        const bool explicitEmpty = (rest == kMissingFieldSearchTerm);
        const QString value = explicitEmpty ? QString() : stripQuotes(rest);
        if (!explicitEmpty && value.isEmpty()) {
            continue;   // still nothing -- ignore rather than guess
        }

        if (field == QLatin1String("tr") || field == QLatin1String("track")) {
            clauses << (explicitEmpty
                ? buildNullNumericClause(QStringLiteral("l.tracknumber"), negate)
                : buildNumericClause(QStringLiteral("CAST(l.tracknumber AS INTEGER)"), value, negate, i, binds));
        } else if (plainNumericFieldColumns().contains(field)) {
            const QString column = QStringLiteral("l.") + plainNumericFieldColumns()[field];
            clauses << (explicitEmpty
                ? buildNullNumericClause(column, negate)
                : buildNumericClause(column, value, negate, i, binds));
        } else if (field == QLatin1String("y") || field == QLatin1String("year")) {
            clauses << (explicitEmpty
                ? buildNullNumericClause(QStringLiteral("l.year"), negate)
                : buildYearClause(value, negate, i, binds));
        } else if (field == QLatin1String("du") || field == QLatin1String("duration")) {
            clauses << (explicitEmpty
                ? buildNullNumericClause(QStringLiteral("l.duration"), negate)
                : buildDurationClause(value, negate, i, binds));
        } else if (field == QLatin1String("b") || field == QLatin1String("bpm")) {
            clauses << buildBpmClause(explicitEmpty ? kMissingFieldSearchTerm : value, negate, fuzzy, i, binds);
        } else if (field == QLatin1String("k") || field == QLatin1String("key")) {
            clauses << buildKeyClause(explicitEmpty ? kMissingFieldSearchTerm : value, negate, fuzzy, i, binds);
        } else if (field == QLatin1String("ad") || field == QLatin1String("added")
                   || field == QLatin1String("dateadded") || field == QLatin1String("datetime_added")) {
            // Real mixxx's DateAddedFilterNode is its own class, not a
            // NumericFilterNode subclass -- it never special-cases the
            // explicit-empty marker at all, so `added:""` just fails to
            // parse as a date like any other garbage input (see
            // buildDateAddedClause's own always-false-on-parse-failure
            // fallback, kept consistent with every other builder here).
            clauses << buildDateAddedClause(explicitEmpty ? kMissingFieldSearchTerm : value, negate, i, binds);
        } else if (textFieldColumns().contains(field)) {
            const QString column = QStringLiteral("l.") + textFieldColumns()[field];
            clauses << (explicitEmpty
                ? buildNullOrEmptyTextClause(column, negate)
                : buildTextClause(column, value, negate, i, binds));
        }
    }
    return clauses;
}

} // namespace

int keyIdFromText(const QString& text)
{
    int keyId = 0;
    return guessKeyIdFromText(text, &keyId) ? keyId : 0;
}

MixHouseParsedSearch parseSearchQuery(const QString& query)
{
    MixHouseParsedSearch result;
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return result;
    }

    // Real mixxx: SearchQueryParser::parseOrNode splits the RAW query string
    // into AND-groups on `|`/`OR` BEFORE tokenizing (searchqueryparser.cpp:
    // 344-360) -- each group becomes its own AND'd clause set, and the
    // groups are OR'd together. A bind-name index shared across every group
    // (rather than reset per group) keeps every generated `:name` unique.
    const QStringList orSegments = trimmed.split(orSplitRegexp(), Qt::SkipEmptyParts);

    QStringList groupClauses;
    int globalIndex = 0;
    for (const QString& segment : orSegments) {
        const QStringList segClauses = clausesForTokens(
            tokenize(segment.trimmed()), globalIndex, result.binds);
        if (!segClauses.isEmpty()) {
            groupClauses << segClauses.join(QStringLiteral(" AND "));
        }
    }

    if (groupClauses.size() == 1) {
        result.whereSql = groupClauses.first();
    } else if (groupClauses.size() > 1) {
        // Each group is already its own valid AND-clause; wrap each in its
        // own parens before OR-ing so operator precedence can't surprise
        // anyone reading the generated SQL, even though bare AND/OR
        // precedence would already group this correctly on its own.
        result.whereSql = QStringLiteral("(") + groupClauses.join(QStringLiteral(") OR (")) + QStringLiteral(")");
    }
    return result;
}

void setFuzzyBpmRelativeRange(double range)
{
    g_fuzzyBpmRelativeRange = range;
}

} // namespace au::mixhouse
