import QtQuick
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents

import Audacity.ProjectScene
import Audacity.Project
import Audacity.Playback

DropArea {
    id: root

    property var tracksItemsView: null
    property var tracksViewState: null
    property var timeline: null
    // GlideMix: ruler plus the TEMPO lane when shown.
    property real headerHeight: timeline ? timeline.height : 0

    signal setGuidelineRequested(var pos, bool visibility)

    signal externalDropAreaEntered(var drop)
    signal externalDropAreaExited
    signal externalDropAreaDropped(var drop)

    onExternalDropAreaEntered: drop => {
        handleOnEntered(drop, true)
    }

    onExternalDropAreaExited: {
        clearPreviewClipsTimer.start()
    }

    onExternalDropAreaDropped: drop => {
        handleOnDropped(drop, true)
    }

    QtObject {
        id: prv

        property var lastProbedUrls: null
        // GlideMix: a drop being imported. The library's drag is an OS drag;
        // while the import ran inside the drop, macOS sent the timeline the
        // drag again, which drew the preview clip, the guideline and an empty
        // lane back -- and no exit ever cleared them (the song showed twice).
        property bool dropping: false
    }

    DropController {
        id: dropController

        Component.onCompleted: listenForPlacedSongs()

        // GlideMix: a song double-clicked in the library: same view as a drop.
        onViewFocusRequested: function(start, end, trackId) {
            timeline.context.revealTimeRange(start, end)
            tracksItemsView.revealTrack(trackId)
        }
    }

    Timer {
        id: clearPreviewClipsTimer
        interval: 100
        onTriggered: {
            tracksItemsView.clearPreviewImportClip([])
            prv.lastProbedUrls = null
            dropController.endImportSession()
            root.setGuidelineRequested(-1, false)
        }
    }

    onEntered: drop => {
        handleOnEntered(drop, false)
    }

    onExited: {
        clearPreviewClipsTimer.start()
    }

    onPositionChanged:
    // NOTE! Qt does not reliably send onPositionChanged for external drags
    // it is expected that Qt may trigger entered/exited signals alternately
    // instead of positionChanged
    {}

    onDropped: drop => {
        handleOnDropped(drop, false)
    }

    function handleOnEntered(drop, externalDrop) {
        if (prv.dropping) {
            return
        }
        clearPreviewClipsTimer.stop()

        let urls = drop.urls
        dropController.startImportSession()
        if (!prv.lastProbedUrls) {
            // NOTE: working with urls list from DropArea
            // is expensive so avoid it otherwise the preview clip
            // move will be laggy
            dropController.probeAudioFiles(urls)
            prv.lastProbedUrls = urls
        }

        let dropX = 0
        let dropY = drop.y
        if (!externalDrop) {
            dropX = drop.x
            dropY -= root.headerHeight
        }

        var trackId = tracksViewState.trackAtPosition(dropX, dropY)
        let trackCount = dropController.requiredTracksCount()
        dropController.prepareConditionalTracks(trackId, trackCount)
        dropController.removeDragAddedTracks(trackId, trackCount)

        let tracksIds = dropController.draggedTracksIds(trackId, trackCount)
        tracksItemsView.clearPreviewImportClip(tracksIds /* tracks not to clear */)
        const durations = dropController.lastProbedDurations()
        const titles = dropController.lastProbedFileNames()

        tracksItemsView.previewImportClipRequested(tracksIds, dropX, durations, titles)

        root.setGuidelineRequested(dropX, true)
    }

    function handleOnDropped(drop, externalDrop) {
        let dropX = 0
        let dropY = drop.y
        if (!externalDrop) {
            dropX = drop.x
            dropY -= root.headerHeight
        }

        if (prv.dropping) {
            return
        }
        let trackId = tracksViewState.trackAtPosition(dropX, dropY)
        let trackCount = dropController.requiredTracksCount()
        let tracksIds = dropController.draggedTracksIds(trackId, trackCount);
        const dropTime = timeline.context.positionToTime(dropX)
        clearPreviewClipsTimer.stop()
        drop.acceptProposedAction()

        // GlideMix: the drop is accepted at once and imported on the next
        // turn of the event loop, so the OS drag ends before the import runs.
        prv.dropping = true
        tracksItemsView.clearPreviewImportClip([])
        root.setGuidelineRequested(-1, false)
        Qt.callLater(function() { root.importDropped(tracksIds, dropTime) })
    }

    function importDropped(tracksIds, dropTime) {
        // Started here too, so a failed import cannot leave drops ignored.
        droppingTimer.restart()
        // by this time, url list is already inside dropController
        dropController.pxPerSec = timeline.context.zoom   // GlideMix: the drop's magnet reach follows the zoom
        dropController.handleDroppedFiles(tracksIds, dropTime)

        dropController.endImportSession()

        // The drag's preview clip: the lanes are no longer rebuilt after a
        // drop, which used to wipe it.
        tracksItemsView.clearPreviewImportClip([])

        // GlideMix: the zoom stays; the view scrolls only if the new song is off screen.
        const range = dropController.lastDropFocusRange()
        if (range.length === 2) {
            timeline.context.revealTimeRange(range[0], range[1])
        }
        tracksItemsView.revealTrack(dropController.lastDropTrackId())

        root.setGuidelineRequested(-1, false)
        prv.lastProbedUrls = null
        // Any drag event the OS still had queued for this drop is ignored.
        droppingTimer.restart()
    }

    Timer {
        id: droppingTimer
        interval: 300
        onTriggered: prv.dropping = false
    }
}
