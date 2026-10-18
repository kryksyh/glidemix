/*
 * GlideMix
 */
#pragma once

#include <vector>

#include <QColor>
#include <QString>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"
#include "trackedit/trackedittypes.h"

namespace au::mixhouse {
/// The beat grid of a library track as it lies on the timeline: the beat and
/// bar ticks drawn on its clips, and the parked MixHouse DJ app's grid tools
/// (nudge earlier/later, tempo faster/slower, undo) applied to the selected
/// clips' tracks.
class IMixHouseBeatGrids : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IMixHouseBeatGrids)

public:
    virtual ~IMixHouseBeatGrids() = default;

    struct Tick {
        double time = 0.0;   ///< project seconds
        bool isBar = false;  ///< beat 1 of a bar
    };

    /// Beat ticks of `clip` between project times `fromTime` and `toTime`.
    /// Empty when the clip isn't an analyzed library track.
    virtual std::vector<Tick> clipTicks(const trackedit::ClipKey& clip, double fromTime, double toTime) const = 0;

    /// A cue point of the clip's track, placed on the timeline.
    struct Cue {
        double time = 0.0;      ///< project seconds
        double endTime = -1.0;  ///< loops: project seconds of the loop end
        int hotCueIndex = -1;   ///< 0.. = hot cue A..; -1 = memory cue
        QString name;
        QColor color;
    };

    /// Cue points of `clip` between project times `fromTime` and `toTime`
    /// (from the track's external library -- Lexicon DJ -- for now).
    virtual std::vector<Cue> clipCues(const trackedit::ClipKey& clip, double fromTime, double toTime) const = 0;

    /// A phrase of the clip's track (rekordbox's phrase analysis), placed on
    /// the timeline.
    struct Phrase {
        double time = 0.0;      ///< project seconds
        double endTime = 0.0;
        double fillTime = -1.0;  ///< project seconds where rekordbox's fill-in starts (-1 = none)
        QString label;
        QColor color;
    };

    /// Phrases of `clip` between project times `fromTime` and `toTime`.
    virtual std::vector<Phrase> clipPhrases(const trackedit::ClipKey& clip, double fromTime, double toTime) const = 0;

    /// MixMeister's anchors on a clip, in project seconds: the Intro anchor
    /// (where it mixes in: the end of rekordbox's Intro phrases, else bar 1)
    /// and the Outro anchor (where the next track takes over: rekordbox's
    /// Outro phrase, else the last 8-bar phrase boundary leaving 8 bars) --
    /// the parked app's mixInSec/mixOutSec. An Outro anchor and the next track's
    /// Intro anchor meeting is a transition.
    struct Anchors {
        bool valid = false;
        double intro = 0.0;
        double outro = 0.0;
        bool moved = false;   ///< placed by hand
    };
    virtual Anchors clipAnchors(const trackedit::ClipKey& clip) const = 0;
    /// Drags anchor `which` (0 intro, 1 outro) to `projectTime`, on the
    /// track's beats while SYNC is on. Saved for the file when `completed`.
    virtual void moveClipAnchor(const trackedit::ClipKey& clip, int which, double projectTime, bool completed) = 0;
    /// Back to the default anchors.
    virtual void resetClipAnchors(const trackedit::ClipKey& clip) = 0;

    /// One pixel column of a clip's colour waveform: rekordbox's own colour
    /// and height for that moment of the track.
    struct WaveColumn {
        uint32_t rgb = 0;   ///< 0xRRGGBB
        uint8_t level = 0;  ///< the loudest moment in this pixel, 0..255 of the lane's half height
        uint8_t core = 0;   ///< its average: the brighter body rekordbox draws inside the peaks
        bool valid = false;
    };

    /// `columns` colour waveform columns spanning project times [fromTime,
    /// toTime) of `clip`, from rekordbox's scrolling-deck waveform where the
    /// track has one, else from this app's own analysis (much coarser: one
    /// colour every eighth of a second). Empty when the track has neither.
    virtual std::vector<WaveColumn> clipWaveColumns(const trackedit::ClipKey& clip, double fromTime, double toTime,
                                                    int columns) const = 0;

    /// How many stored waveform columns there are per second of project time
    /// for `clip` (150 from rekordbox, about 8 from this app's own analysis,
    /// 0 when there is no colour waveform). Past this the columns are wider
    /// than a pixel and the lane has to draw the audio itself.
    virtual double clipWaveColumnRate(const trackedit::ClipKey& clip) const = 0;

    /// The audio file of the library track `clip` was imported from, and how
    /// its seconds map onto the timeline: file second `a` plays at
    /// sequenceStart + a * ratio. False when the clip is not a library track.
    struct ClipFile {
        QString path;
        double sequenceStart = 0.0;
        double ratio = 1.0;
    };
    virtual bool clipFile(const trackedit::ClipKey& clip, ClipFile& out) const = 0;
    /// The clip's audio from file second `fromSec` to `toSec`, mixed to mono,
    /// for drawing close-ups. Clamped to what the clip holds; `startSec`
    /// says where the returned samples really begin. False when unavailable.
    /// The clip's volume line (curves included) at `count` times from project
    /// second `t0`, `step` apart. False (and `out` left empty) without the clip.
    virtual bool clipVolume(const trackedit::ClipKey& clip, double t0, double step, size_t count,
                            std::vector<double>& out) const = 0;
    /// `channel` >= 0 reads that channel alone instead of the mono mix.
    virtual bool clipMonoSamples(const trackedit::ClipKey& clip, double fromSec, double toSec, std::vector<float>& out,
                                 double& sampleRate, double& startSec, int channel = -1) const = 0;

    /// The BPM of the library track `clip` was imported from (0 = unknown).
    virtual double clipBpm(const trackedit::ClipKey& clip) const = 0;

    /// The library track `clip` was imported from (-1 = none), for the Beatgrid editor.
    virtual int clipLibraryTrackId(const trackedit::ClipKey& clip) const = 0;
    /// The library track of the first selected clip (or of a clip on a selected track); -1 = none.
    virtual int selectedLibraryTrackId() const = 0;

    /// Fires when a grid changes (so clips repaint their ticks).
    virtual muse::async::Notification beatGridsChanged() const = 0;

    /// Where a clip being dragged should start so that, with SYNC on, its bar 1
    /// sits on the nearest project bar line (keeping the lock while moving).
    /// Returns `proposedStart` unchanged when SYNC is off or the clip isn't an
    /// analyzed library track.
    /// `pxPerSec` > 0 also pulls the clip's Intro anchor onto another clip's
    /// Outro anchor (and its Outro onto another's Intro) within a few pixels:
    /// MixMeister's magnetic anchors.
    virtual double lockedClipStart(const trackedit::ClipKey& clip, double proposedStart, double pxPerSec = 0.0) const = 0;
    /// Where `clip` should start so its Intro anchor lands on another clip's
    /// Outro anchor (or its Outro on another's Intro), if one is within
    /// `thresholdSec` of `proposedStart`'s anchors; else `proposedStart`.
    /// The grid SYNC locks tracks to and Mix Now lands on, in project seconds:
    /// the timeline's Snap setting (Bar, 1/2, 1/4 ... with triplets) when Snap
    /// is on; a bar when it's off or set to seconds/frames.
    virtual double syncGridSec() const = 0;

    virtual double magneticClipStart(const trackedit::ClipKey& clip, double proposedStart, double thresholdSec) const = 0;

    /// After a SYNC drag: a clip dropped at the project start whose bar 1
    /// sits a little after it (a few ms to a beat of lead-in before the first
    /// downbeat) gets that lead-in trimmed, so its bar 1 lands on bar 1.
    virtual void seatBarOneAtProjectStart(const trackedit::ClipKey& clip) = 0;

    /// MixMeister's insert: a song dropped into a transition (its IN on one
    /// song's OUT, while another song already comes in there) pushes that
    /// song and every song after it later, to make room; its row moves in
    /// between too. `reordered`: the song was dragged out of its own place in
    /// the mix (after beginClipMove), so that gap closes first. One undo step
    /// with the move or drop that placed it.
    virtual void beginClipMove(const trackedit::ClipKey& clip) = 0;
    virtual void insertAtTransition(const trackedit::ClipKey& clip, bool reordered) = 0;
    /// A drag that ends without an insert (moved freely, several clips).
    virtual void cancelClipMove() = 0;
    /// After a song was dragged to another row: its old row goes when it is
    /// left empty, and a new row ("Audio N") takes the old row's name.
    virtual void tidyRowsAfterMove(const trackedit::ClipKey& clip) = 0;

    /// Songs never cut each other (Audacity trims whatever a clip lands on):
    /// a row where [start, end) is clear of other clips (`ignore` aside) --
    /// `preferred` if it is, else the first clear audio row below it, else a
    /// new row at the bottom with `channels` channels. `taken` rows are skipped.
    virtual trackedit::TrackId clearRow(trackedit::TrackId preferred, double start, double end, int channels,
                                        const trackedit::ClipKeyList& ignore = {},
                                        const trackedit::TrackIdList& taken = {}) = 0;

    enum class Adjust {
        Earlier,   ///< move the grid 5 ms earlier (Mixxx's beats_translate_earlier)
        Later,     ///< move the grid 5 ms later
        Faster,    ///< +0.01 BPM (Mixxx's beats_adjust_faster)
        Slower,    ///< -0.01 BPM
        BarEarlier, ///< bar 1 starts one beat earlier (fixes a downbeat found on the wrong beat)
        BarLater,   ///< bar 1 starts one beat later
        Undo,      ///< undo the last grid change
    };

    /// Applies to the library tracks of the selected clips (or, with no clip
    /// selected, of every clip on the selected tracks), then re-seats every
    /// clip of those tracks on the bar grid with the corrected beats.
    virtual void adjustSelectedClips(Adjust adjust) = 0;
};
}
