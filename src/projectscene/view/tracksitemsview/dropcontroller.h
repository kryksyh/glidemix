/*
* Audacity: A Digital Audio Editor
*/

#pragma once

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "importexport/import/iimporter.h"
#include "trackedit/itracksinteraction.h"
#include "trackedit/iprojecthistory.h"
#include "trackedit/iselectioncontroller.h"
#include "mixhouse/imixhousetrackaligner.h"
#include "mixhouse/imixhousebeatgrids.h"

namespace au::projectscene {
class DropController : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };
    muse::ContextInject<importexport::IImporter> importer{ this };
    muse::ContextInject<trackedit::ITracksInteraction> tracksInteraction{ this };
    muse::ContextInject<mixhouse::IMixHouseTrackAligner> mixhouseAligner{ this };   // MixHouse: beat-to-bar alignment of library tracks
    muse::ContextInject<mixhouse::IMixHouseBeatGrids> beatGrids{ this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory{ this };
    muse::ContextInject<trackedit::ISelectionController> selectionController{ this };   // GlideMix: the dropped song alone is selected   // GlideMix: a clear row for each dropped song

public:
    explicit DropController(QObject* parent = nullptr);

    //! GlideMix: the timeline's zoom when files are dropped (pixels per second).
    Q_PROPERTY(double pxPerSec MEMBER m_pxPerSec)

    Q_INVOKABLE void probeAudioFiles(const QStringList& fileUrls);
    Q_INVOKABLE QVariantList lastProbedDurations() const;
    Q_INVOKABLE QVariantList lastProbedFileNames() const;
    Q_INVOKABLE void startImportSession();
    Q_INVOKABLE void endImportSession();
    Q_INVOKABLE int requiredTracksCount() const;
    Q_INVOKABLE void prepareConditionalTracks(int currentTrackId, int draggedFileCount);
    Q_INVOKABLE QVariantList draggedTracksIds(int currentTrackId, int draggedFilesCount);
    Q_INVOKABLE void removeDragAddedTracks(int currentTrackId, int draggedFilesCount);
    //! GlideMix: after a drop, the time span of the dropped song and the songs
    //! before and after it, [start, end]; empty when nothing was dropped.
    Q_INVOKABLE QVariantList lastDropFocusRange() const;
    Q_INVOKABLE int lastDropTrackId() const { return m_focusTrack; }
    Q_INVOKABLE void handleDroppedFiles(const std::vector<trackedit::TrackId>& trackIds, double startTime);
    //! GlideMix: songs placed without a drop (double-click in the library)
    //! arrive as viewFocusRequested, so the view follows them too.
    Q_INVOKABLE void listenForPlacedSongs();

signals:
    void viewFocusRequested(double start, double end, int trackId);

private:
    std::vector<au::importexport::FileInfo> m_lastDraggedFilesInfo;
    QStringList m_lastDraggedUrls;
    int m_trackCountBeforeImport = -1;

private:
    double m_pxPerSec = 0.0;
    double m_focusStart = 0.0;
    double m_focusEnd = 0.0;
    int m_focusTrack = -1;
};
}
