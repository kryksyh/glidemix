/*
 * MixHouse DJ 2
 *
 * Field-scoped search, ported from mixxx's real library/searchqueryparser.cpp
 * and library/searchquery.cpp (field table, numeric operator/range syntax,
 * and the BpmFilterNode/KeyFilterNode special cases) -- not reinvented.
 * Deliberately lives in the core mixhouse module (no QML dependency), same
 * as mixhouselibrary.{h,cpp} itself.
 */
#pragma once

#include <QString>
#include <QVariant>
#include <vector>

namespace au::mixhouse {
/// One `:tokN` placeholder and the value to bind to it.
struct MixHouseSearchBind {
    QString name;
    QVariant value;
};

struct MixHouseParsedSearch {
    /// A SQL boolean expression (already `AND`-joined across every term),
    /// referencing columns as `l.column` (the same alias mixhouselibrary.cpp
    /// already uses for the `library` table) and named placeholders that
    /// match `binds`. Empty string if the query had no terms at all.
    QString whereSql;
    std::vector<MixHouseSearchBind> binds;
};

/// Parses mixxx's own field-scoped search syntax against our simpler
/// single-table schema:
///   - Bare word(s) or a "quoted phrase" match title/artist/album (mixxx's
///     own default search columns) -- the exact behaviour the plain-text
///     search already had, now just one case among several.
///   - `field:value` / `field:"quoted value"` text filters: a/artist,
///     aa/album_artist, al/album, t/title, g/genre, cp/composer,
///     gr/grouping, cm/comment, type. A leading "=" on the value (e.g.
///     `artist:=Muse`) requests an exact match instead of "contains".
///   - `field:value` numeric filters with mixxx's real operator (<,>,<=,>=)
///     and range (`min-max`) syntax: tr/track, r/rating, br/bitrate,
///     y/year, du/duration.
///   - `bpm:value` / `b:value` -- mixxx's own BpmFilterNode rules: operator/
///     range same as other numerics; a bare number with neither matches it
///     (and its half/double) within a +/-6% tolerance
///     (BpmFilterNode::kRelativeRangeDefault); a leading `~` (e.g. `~bpm:128`)
///     applies that same +/-6% tolerance WITHOUT the half/double widening.
///   - `key:value` / `k:value` -- exact (case-insensitive) match against the
///     track's own stored key text (whatever notation it's in). A leading
///     `~` with a Camelot-notation value (e.g. `~key:8A`) instead matches
///     the real 6-key harmonically-compatible set
///     (KeyUtils::getCompatibleKeys's exact rule: same key, relative major/
///     minor, and the perfect 4th/5th of both), by key_id.
///   - A leading "-" on ANY term (bare, text, numeric, bpm, or key) negates
///     it, mixxx's own universal negation prefix.
/// Multiple terms are ANDed together, same as mixxx's own AndNode.
MixHouseParsedSearch parseSearchQuery(const QString& query);

/// Real mixxx: BpmFilterNode::setBpmRelativeRange() (library/searchquery.h/
/// .cpp) -- a plain static setter over the same module-global tolerance
/// buildBpmClause()'s fuzzy path (mixhousesearchquery.cpp) reads, applied
/// live from the "Music library" Preferences page
/// (MixHouseLibraryPreferencesModel) exactly the way real mixxx's own
/// DlgPrefLibrary::slotBpmRangeSelected() applies it immediately, no
/// restart required. Also set once at startup from the persisted value
/// (MixHouseLibrary::init()). Deliberately a free function, not routed
/// through IMixHouseLibraryConfiguration itself: this file has zero Qt-IoC
/// dependencies by design (see this header's own top comment) and reads a
/// plain in-memory value the same way real mixxx's own static does.
void setFuzzyBpmRelativeRange(double range);

/// The key id (1..24) of a key written in any notation the Key column can
/// show -- "8A", "1m", "Am", "8A/Am" -- or 0 if it is not a key.
int keyIdFromText(const QString& text);
}
