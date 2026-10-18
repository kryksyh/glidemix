/*
 * GlideMix
 */
#pragma once

#include <atomic>
#include <memory>

#include <QHash>
#include <QString>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/itrackeditinteraction.h"
#include "trackedit/itracksinteraction.h"
#include "trackedit/iselectioncontroller.h"
#include "trackedit/iprojecthistory.h"

#include "mixhouse/imixhousebeatgrids.h"
#include "mixhouse/imixhouselibrary.h"
#include "mixhouse/imixhouselexicon.h"
#include "mixhouse/imixhouserekordboxxml.h"
#include "mixhouse/imixhouserekordboxphrases.h"
#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/mixhousewaveform.h"

namespace au::mixhouse {
class MixHouseBeatGrids : public IMixHouseBeatGrids, public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<trackedit::ITrackeditInteraction> trackeditInteraction { this };
    muse::ContextInject<trackedit::ITracksInteraction> tracksInteraction { this };
    muse::ContextInject<trackedit::ISelectionController> selectionController { this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory { this };
    muse::ContextInject<IMixHouseLibrary> library { this };
    muse::ContextInject<IMixHouseLexicon> lexicon { this };
    muse::ContextInject<IMixHouseRekordboxXml> rekordboxXml { this };
    muse::ContextInject<IMixHouseRekordboxPhrases> rekordboxPhrases { this };
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;

public:
    explicit MixHouseBeatGrids(const muse::modularity::ContextPtr& iocCtx);
    ~MixHouseBeatGrids() override;

    void init();

    std::vector<Tick> clipTicks(const trackedit::ClipKey& clip, double fromTime, double toTime) const override;

    std::vector<Cue> clipCues(const trackedit::ClipKey& clip, double fromTime, double toTime) const override;
    void seatBarOneAtProjectStart(const trackedit::ClipKey& clip) override;
    void beginClipMove(const trackedit::ClipKey& clip) override;
    void insertAtTransition(const trackedit::ClipKey& clip, bool reordered) override;
    void cancelClipMove() override;
    void tidyRowsAfterMove(const trackedit::ClipKey& clip) override;
    trackedit::TrackId clearRow(trackedit::TrackId preferred, double start, double end, int channels,
                                const trackedit::ClipKeyList& ignore = {},
                                const trackedit::TrackIdList& taken = {}) override;
    std::vector<Phrase> clipPhrases(const trackedit::ClipKey& clip, double fromTime, double toTime) const override;
    muse::async::Notification beatGridsChanged() const override;
    int clipLibraryTrackId(const trackedit::ClipKey& clip) const override;
    std::vector<WaveColumn> clipWaveColumns(const trackedit::ClipKey& clip, double fromTime, double toTime,
                                            int columns) const override;
    double clipWaveColumnRate(const trackedit::ClipKey& clip) const override;
    double clipBpm(const trackedit::ClipKey& clip) const override;
    bool clipFile(const trackedit::ClipKey& clip, ClipFile& out) const override;
    bool clipVolume(const trackedit::ClipKey& clip, double t0, double step, size_t count,
                    std::vector<double>& out) const override;
    bool clipMonoSamples(const trackedit::ClipKey& clip, double fromSec, double toSec, std::vector<float>& out,
                         double& sampleRate, double& startSec, int channel = -1) const override;
    int selectedLibraryTrackId() const override;
    double lockedClipStart(const trackedit::ClipKey& clip, double proposedStart, double pxPerSec = 0.0) const override;
    Anchors clipAnchors(const trackedit::ClipKey& clip) const override;
    double syncGridSec() const override;
    double magneticClipStart(const trackedit::ClipKey& clip, double proposedStart, double thresholdSec) const override;
    void moveClipAnchor(const trackedit::ClipKey& clip, int which, double projectTime, bool completed) override;
    void resetClipAnchors(const trackedit::ClipKey& clip) override;
    void adjustSelectedClips(Adjust adjust) override;

private:
    struct Grid {
        int trackId = -1;
        QString title;
        double bpm = 0.0;
        double firstBeatSec = 0.0;
        double downbeatSec = -1.0;
        double downbeatConfidence = 0.0;
        QString filePath;
        std::vector<MixHouseCue> cues;   ///< external library tracks: their cue points (source seconds)
        bool valid() const { return trackId != -1 && bpm > 0.0; }   // Lexicon tracks have ids < -1
    };

    //! The library grid of the track a clip was imported from, found by the
    //! clip's name (Audacity names an imported clip after its file).
    Grid gridForClipName(const QString& clipName, bool fresh) const;
    QString clipName(const trackedit::ClipKey& clip) const;
    void reseatClipsOf(const Grid& grid);
    double barLockedClipStart(const trackedit::ClipKey& clip, double proposedStart) const;
    //! The phrases shown for a grid's file: edited ones, else rekordbox's.
    std::vector<MixHousePhrase> phrasesFor(const Grid& grid) const;
    //! A clip's anchors in seconds into its file, and the clip's mapping.
    struct SourceAnchors {
        bool valid = false;
        double intro = 0.0;
        double outro = 0.0;
        bool moved = false;
        double sequenceStart = 0.0;   // project seconds of file second 0
        double ratio = 1.0;           // project seconds per file second
    };
    SourceAnchors sourceAnchors(const trackedit::ClipKey& clip) const;
    //! Anchor source seconds being dragged, by file (not yet saved).
    mutable QHash<QString, std::pair<double, double> > m_anchorDrag;

    //! MixMeister's linked anchors: while one side of a transition (an OUT
    //! anchor meeting the next track's IN) is dragged, the next track and
    //! every track after it move with it, so the mix stays joined.
    struct RippleDrag {
        bool active = false;
        trackedit::ClipKey clip;
        int which = -1;
        double originalAnchor = 0.0;   ///< the dragged anchor, project seconds
        double minShift = 0.0;         ///< so no follower runs into a clip before it
        double applied = 0.0;
        std::vector<std::pair<trackedit::ClipKey, double> > followers;   ///< and their start times
        QString filePath;                        ///< the dragged clip's song
        std::pair<double, double> anchorsBefore; ///< its anchors, source seconds
    };
    RippleDrag m_ripple;
    //! Where a dragged song's anchors were when the drag began (project seconds).
    std::optional<Anchors> m_moveBefore;
    bool m_moveCaptured = false;
    trackedit::TrackId m_moveFromTrack = -1;   // the row the dragged song started on
    QString m_moveFromTitle;
    void startRipple(const trackedit::ClipKey& clip, int which);
    void applyRipple(double anchorNow, bool completed, const std::pair<double, double>& anchorsAfter);

    //! Anchors are kept per song in the library, outside the project's undo
    //! history; a transition move records the song's anchors before and after
    //! it against its history step, so undo and redo put them back too.
    struct AnchorRecord {
        size_t stateIndex = 0;
        QString filePath;
        std::pair<double, double> before;
        std::pair<double, double> after;
    };
    std::vector<AnchorRecord> m_anchorHistory;
    bool m_subscribedToHistory = false;
    void subscribeToHistory();
    void setSongAnchors(const QString& filePath, const std::pair<double, double>& anchors);
    //! Saved anchors by file, read once (cleared on library changes).
    mutable QHash<QString, std::optional<std::pair<double, double> > > m_anchorCache;
    //! Library tracks rekordbox analysed take its grid (background read, one write).
    void applyRekordboxGrids();

    //! The colour waveform of each file, decoded once: rekordbox's own where
    //! there is one, else this app's analysis. An invalid entry means "looked,
    //! found nothing" and stops the lookup repeating every repaint.
    mutable QHash<QString, MixHouseWaveformOverview> m_waveCache;

    mutable QHash<QString, Grid> m_gridByClipName;
    std::vector<Grid> m_undo;
    muse::async::Notification m_beatGridsChanged;
    bool m_phraseRibbonVisible = true;
    bool m_rekordboxPassRunning = false;
    std::shared_ptr<std::atomic<bool> > m_alive;
};
}
