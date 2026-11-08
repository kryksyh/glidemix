/*
 * GlideMix: smart playlists -- a saved set of rules that picks tracks from a
 * library and updates by itself, like rekordbox's Intelligent Playlists and
 * Lexicon's Smartlists.
 *
 * A playlist matches all (or any) of its groups; a group matches when any of
 * its rules does, which gives Lexicon's "(Genre is House OR Techno) AND
 * Rating 3" in the All mode. Stored as JSON in GlideMix's own library.
 */
#pragma once

#include <QDateTime>
#include <QSet>
#include <QString>
#include <vector>

#include "mixhousetypes.h"

namespace au::mixhouse {
struct MixHouseSmartRule {
    QString field;    ///< see smartFields()
    QString op;       ///< see smartOperators()
    QString value;
    QString value2;   ///< second value: "between", "within ±%", "in the last" unit
};

struct MixHouseSmartPlaylist {
    int id = -1;
    QString name;
    bool matchAll = true;
    /// "collection" (GlideMix's own), "lexicon", "rekordbox", or "all".
    QString source = QStringLiteral("collection");
    /// Each inner list is one group: its rules are OR-ed.
    std::vector<std::vector<MixHouseSmartRule> > groups;
    QString sort = QStringLiteral("dateAdded");   ///< a field name, see smartFields()
    bool ascending = false;
    int limit = 0;                                ///< 0 = no limit

    QString toJson() const;
    static MixHouseSmartPlaylist fromJson(const QString& json);
};

/// What a rule can look at.
struct MixHouseSmartField {
    QString id;
    QString title;
    /// "text", "number", "key", "color", "date", "bool", "playlist", "search"
    QString type;
};
const std::vector<MixHouseSmartField>& smartFields();
const MixHouseSmartField* smartField(const QString& id);

/// The operators for a field type: {id, title}.
std::vector<std::pair<QString, QString> > smartOperators(const QString& fieldType);

/// What a rule needs besides the track itself.
struct MixHouseSmartContext {
    QDateTime now = QDateTime::currentDateTime();
    /// "In playlist" rules: the file paths of each playlist named in a rule
    /// (key: playlist id as text).
    QHash<QString, QSet<QString> > playlistFiles;
    /// "Search" rules: the files the search box would find for each rule's
    /// text ("genre:house bpm:>125"), so a saved search is a smart playlist.
    QHash<QString, QSet<QString> > searchFiles;
};

bool smartRuleMatches(const MixHouseSmartRule& rule, const MixHouseTrack& track, const MixHouseSmartContext& context);
bool smartPlaylistMatches(const MixHouseSmartPlaylist& playlist, const MixHouseTrack& track,
                          const MixHouseSmartContext& context);

/// Filters `tracks` by the playlist's rules, then applies its own sort and
/// limit ("the newest 50").
MixHouseTrackList applySmartPlaylist(const MixHouseSmartPlaylist& playlist, MixHouseTrackList tracks,
                                     const MixHouseSmartContext& context);

/// Camelot keys that mix with `camelot` ("8A"): itself, one step either way
/// on the wheel, and its relative major/minor. Empty if not a Camelot key.
QStringList harmonicCamelotKeys(const QString& camelot);
}
