/*
 * GlideMix
 */
#pragma once

#include <QColor>
#include <QString>
#include <vector>

#include "imixhouselibrary.h"
#include "mixhousetypes.h"

namespace au::mixhouse {
/// A cue point of a track in an external DJ library (source seconds).
struct MixHouseCue {
    QString name;
    bool loop = false;
    int hotCueIndex = -1;     ///< 0.. = hot cue A..; -1 = memory cue
    double startSec = 0.0;
    double endSec = -1.0;     ///< loops only
    QColor color;
};

/// Another DJ app's library shown in the sidebar (Lexicon DJ, a rekordbox
/// XML export): its folder/playlist tree, tracks, beat grids and cue points,
/// read-only. Its tracks get negative MixHouseTrack ids in a range of their
/// own, so library writes never touch them.
class IMixHouseExternalLibrary
{
public:
    virtual ~IMixHouseExternalLibrary() = default;

    enum class NodeType { Folder, Playlist, SmartList };

    struct Node {
        int id = -1;
        QString name;
        NodeType type = NodeType::Playlist;
        int parentId = -1;        ///< -1 = top level
        int trackCount = 0;       ///< playlists: tracks; folders: direct children
    };

    virtual bool available() const = 0;
    virtual std::vector<Node> nodes() const = 0;
    virtual int trackCount() const = 0;

    /// Tracks of a playlist (`playlistId` -1 = all), in playlist order unless
    /// sorted; filter / key filters / location / fileBaseName / sort / limit
    /// as in IMixHouseLibrary::TrackQuery.
    virtual MixHouseTrackList tracks(int playlistId, const IMixHouseLibrary::TrackQuery& query) const = 0;

    /// Whether a MixHouseTrack id belongs to this library.
    virtual bool ownsTrack(int trackId) const = 0;
    virtual std::vector<MixHouseCue> cues(int trackId) const = 0;
};

/// Track id ranges: Lexicon -(id + 1) down to -999,999,999; rekordbox XML
/// -1,000,000,000 - index. -1 stays "no track".
inline bool isExternalLibraryTrack(int trackId) { return trackId < -1; }

/// The first match for `query` among external libraries (Lexicon, then
/// rekordbox XML) -- a track loaded from one of them.
inline MixHouseTrackList externalLibraryTracks(const IMixHouseExternalLibrary* a, const IMixHouseExternalLibrary* b,
                                               const IMixHouseLibrary::TrackQuery& query)
{
    for (const IMixHouseExternalLibrary* library : { a, b }) {
        if (library && library->available()) {
            MixHouseTrackList found = library->tracks(-1, query);
            if (!found.empty()) {
                return found;
            }
        }
    }
    return {};
}

/// Key id (MixHouseTrack::keyId) of a Camelot key text such as "10A"; 0 if not one.
int keyIdForCamelot(const QString& text);

/// Sorts and trims `tracks` in memory the way a TrackQuery asks (for
/// libraries without SQL). `playlistOrder` keeps the given order for the
/// Position sort.
void sortAndLimitTracks(MixHouseTrackList& tracks, const IMixHouseLibrary::TrackQuery& query, bool playlistOrder);

/// The song's file on the drive: modified and created ("yyyy-MM-dd HH:mm:ss",
/// "" when the file is missing) and size in bytes. Asked once per file while
/// GlideMix runs (a list of 40k songs doesn't stat the drive on every repaint).
struct MixHouseFileInfo {
    QString modified;
    QString created;
    qint64 size = 0;
};
MixHouseFileInfo mixhouseFileInfo(const QString& path);
}
