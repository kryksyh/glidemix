/*
 * GlideMix
 */
#pragma once

#include <map>
#include <optional>
#include <set>
#include <vector>

#include <QAbstractListModel>
#include <QHash>
#include <QQmlParserStatus>
#include <QtQmlIntegration>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "playback/iplayback.h"
#include "trackedit/iprojecthistory.h"
#include "trackedit/itrackeditinteraction.h"
#include "trackedit/itracksinteraction.h"

#include "mixhouse/imixhousebeatgrids.h"
#include "mixhouse/imixhouselibrary.h"
#include "mixhouse/imixhousetrackaligner.h"
#include "mixhouse/imixhousetransitions.h"
#include "mixhouse/imixhousetransitionconfiguration.h"

namespace au::mixhouse {
//! The Project tab: the project's songs as MixMeister's playlist shows them,
//! in mix order (each song's IN on the OUT of the one before it). Reordering
//! the rows re-lays out the mix; a song added here always lands linked.
//! docs/mixmeister-vs-glidemix.md section 2b has the rules.
class MixHouseProjectListModel : public QAbstractListModel, public QQmlParserStatus, public muse::Contextable,
    public muse::async::Asyncable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<IMixHouseBeatGrids> beatGrids { this };
    muse::ContextInject<IMixHouseTransitions> transitions { this };
    muse::ContextInject<IMixHouseLibrary> library { this };
    muse::ContextInject<IMixHouseTrackAligner> trackAligner { this };
    muse::GlobalInject<IMixHouseTransitionConfiguration> transitionConfiguration;
    muse::ContextInject<trackedit::IProjectHistory> projectHistory { this };
    muse::ContextInject<trackedit::ITrackeditInteraction> trackeditInteraction { this };
    muse::ContextInject<trackedit::ITracksInteraction> tracksInteraction { this };
    muse::ContextInject<playback::IPlayback> playback { this };

    Q_PROPERTY(int songCount READ songCount NOTIFY songsChanged)
    //! The transitions a row's drop-down offers (Preferences > Transitions).
    Q_PROPERTY(QStringList transitionNames READ transitionNames NOTIFY transitionNamesChanged)

public:
    explicit MixHouseProjectListModel(QObject* parent = nullptr);

    int songCount() const;
    QStringList transitionNames() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    //! Scrolls the timeline to the row's song.
    Q_INVOKABLE void revealRow(int row);
    //! Moves the playhead into the row's song, `fraction` (0..1) of the way through.
    Q_INVOKABLE void seekRow(int row, double fraction);
    //! Drag-and-drop reorder: the song at `from` goes before the song now at
    //! `to` (rows; `to` == rowCount() = the end). One undo step.
    Q_INVOKABLE void moveRow(int from, int to);
    //! The transition out of the row's song (onto the next one), by name.
    Q_INVOKABLE void setTransition(int row, const QString& name);
    //! Library songs dropped on the tab: inserted before `row` (-1 = appended).
    Q_INVOKABLE void addFiles(const QList<QUrl>& urls, int row);

    void classBegin() override {}
    void componentComplete() override;

signals:
    void songsChanged();
    void transitionNamesChanged();

private:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        BpmRole,
        StretchRole,
        KeyRole,
        TransitionRole,
        HasTransitionRole,
        OverlayRole,
        AnalysingRole,
        ProgressRole,
        PlayingRole,
    };

    //! One song of the mix: the clip with its IN, any later pieces of it
    //! (split at grid jumps), and the anchorless clips that start with it
    //! (stems), which all move together.
    struct Song {
        trackedit::ClipKey inClip;
        trackedit::ClipKey outClip;
        trackedit::ClipKeyList clips;   ///< every clip that moves with the song
        double start = 0.0;
        double end = 0.0;
        double intro = 0.0;
        double outro = 0.0;
    };
    struct Row {
        bool overlay = false;     ///< not in the chain: a clip without anchors
        bool analysing = false;   ///< added here, waiting for its analysis
        int song = -1;            ///< index into m_songs (chain rows)
        trackedit::ClipKey clip;  ///< overlay rows
        QString label;            ///< "1", "1a"...
        QString title;
        QString artist;
        QString key;
        double bpm = 0.0;
        int libraryTrackId = -1;
        double stretch = 0.0;     ///< percent faster (+) or slower (-) than the file
        double start = 0.0;
        double end = 0.0;
        QString transition;       ///< out of this song; "" with hasTransition = none applied
        bool hasTransition = false;
    };
    //! A song added on the tab that is not analysed yet: it joins the chain
    //! once its anchors exist.
    struct Pending {
        trackedit::ClipKeyList clips;
        trackedit::ClipKey after;   ///< chained after this song's IN clip (invalid = at the front)
        bool append = true;
    };

    void scheduleReload();
    void reload();
    std::vector<Song> buildSongs(std::vector<trackedit::Clip>& loose) const;
    void fillMetadata(Row& row, const trackedit::ClipKey& clip) const;
    //! The zone between two songs (false when they don't overlap).
    bool zoneBetween(const Song& a, const Song& b, IMixHouseTransitions::Zone& out) const;
    //! Lays `order` out as a chain from its first song on, re-makes the
    //! transitions of the pairs that are new compared with `before` (and of
    //! any pair that has none), and puts the lanes in mix order. One undo
    //! step named `name`; "" folds it into the step that is on top already.
    void applyOrder(const std::vector<Song>& before, std::vector<Song> order, const std::string& name);
    //! The lanes in mix order, top to bottom (MixMeister's staircase).
    void orderLanes(const std::vector<Song>& order);
    //! After an edit elsewhere: a deleted song leaves no gap, and a song put
    //! after the end of the mix joins it.
    void keepChained(std::vector<Song>& songs);
    void chainPending();
    void updateProgress();

    std::vector<Song> m_songs;
    std::map<trackedit::ClipKey, std::string> m_outNames;   ///< each song's transition out, as last shown
    std::vector<Row> m_rows;
    std::vector<Pending> m_pending;
    //! Library rows by file path: one lookup per song, not one per refresh.
    mutable QHash<QString, std::optional<MixHouseTrack> > m_trackCache;
    std::vector<int> m_progress;   ///< per row, tenths of a percent (what was last sent)
    bool m_reloadQueued = false;
    bool m_checkChain = false;
    std::set<int> m_analysisAsked;   ///< library tracks sent to the background analysis   ///< a new history state since the last reload
    bool m_applying = false;
};
}
