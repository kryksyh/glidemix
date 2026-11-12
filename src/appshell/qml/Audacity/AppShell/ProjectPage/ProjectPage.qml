/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
import QtQuick
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents
import Muse.Dock

import Audacity.AppShell
import Audacity.ProjectScene
import Audacity.Playback
import Audacity.TrackEdit
import Audacity.MixHouse

DockPage {
    id: root

    objectName: "ProjectPage"
    uri: "audacity://project"

    property var topToolKeyNavSec

    property ProjectPageModel pageModel: ProjectPageModel {
        //! GlideMix (Chris): a new or opened project shows the Music library tab.
        onLibraryTabWanted: mixHouseLibraryPanel.raiseTab()
    }

    //! GlideMix: the + beside the bottom tabs: a menu of the closed ones.
    Component {
        id: bottomTabsPlus

        Item {
            anchors.fill: parent

            FlatButton {
                id: plusButton
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24
                icon: IconCode.PLUS
                transparent: true
                // No tooltip: it stayed over the menu (the menu says what it does).
                onClicked: {
                    const items = root.pageModel.closedBottomTabs()
                    plusMenu.toggleOpened(items.length === 0
                                          ? [{ id: "none", title: qsTrc("appshell", "All tabs are open"), enabled: false }]
                                          : items)
                }

                StyledMenuLoader {
                    id: plusMenu
                    onHandleMenuItem: function(itemId) {
                        if (itemId !== "none") {
                            root.pageModel.openDock(itemId)
                        }
                    }
                }
            }
        }
    }

    signal externalDropAreaEntered(var drop)
    signal externalDropAreaExited
    signal externalDropAreaDropped(var drop)

    TrackNavigationModel {
        id: tracksNavModel

        Component.onCompleted: {
            tracksNavModel.init(keynavCentralPanelSec)
        }
    }

    property NavigationSection playbackToolBarKeyNavSec: NavigationSection {
        id: keynavSec
        name: "PlaybackSection"
        enabled: root.visible
        order: 2
    }

    property NavigationSection trackEffectsKeyNavSec: NavigationSection {
        name: "TrackEffectsSection"
        enabled: root.visible && tracksPanel.showEffectsSection
        order: playbackToolBarKeyNavSec.order + 1
    }

    property NavigationSection masterEffectsKeyNavSec: NavigationSection {
        name: "MasterEffectsSection"
        enabled: root.visible && tracksPanel.showEffectsSection
        order: trackEffectsKeyNavSec.order + 1
    }

    property NavigationSection addNewTrackKeyNavSec: NavigationSection {
        name: "AddNewTrackSection"
        enabled: root.visible
        order: masterEffectsKeyNavSec.order + 1
    }

    property NavigationSection keynavTopPanelSec: NavigationSection {
        name: "NavigationTopPanel"
        enabled: root.visible
        order: addNewTrackKeyNavSec.order + 1
    }

    property NavigationSection keynavLeftPanelSec: NavigationSection {
        name: "NavigationLeftPanel"
        enabled: root.visible
        order: keynavTopPanelSec.order + 1
    }

    property NavigationSection keynavCentralPanelSec: NavigationSection {
        name: "TrackViewSection"
        enabled: root.visible
        order: keynavLeftPanelSec.order + 1
    }

    property NavigationSection keynavRightPanelSec: NavigationSection {
        name: "NavigationRightPanel"
        enabled: root.visible
        order: keynavCentralPanelSec.order + 1
    }

    property NavigationSection keynavBottomPanelSec: NavigationSection {
        name: "NavigationBottomPanel"
        enabled: root.visible
        order: keynavRightPanelSec.order + 1
    }

    function navigationPanelSec(location) {
        switch (location) {
        case Location.Top:
            return keynavTopPanelSec
        case Location.Left:
            return keynavLeftPanelSec
        case Location.Right:
            return keynavRightPanelSec
        case Location.Bottom:
            return keynavBottomPanelSec
        }

        return null
    }

    onInited: {
        Qt.callLater(pageModel.init)

        tracksNavModel.activateDefaultNavigation()
    }

    readonly property int verticalPanelDefaultWidth: 281

    readonly property int horizontalPanelMinHeight: 100
    readonly property int horizontalPanelMaxHeight: 520

    readonly property string verticalPanelsGroup: "VERTICAL_PANELS"
    readonly property string bottomPanelsGroup: "BOTTOM_PANELS"
    readonly property string horizontalPanelsGroup: "HORIZONTAL_PANELS"

    readonly property var verticalPanelDropDestinations: [
        {
            "dock": root.centralDock,
            "dropLocation": Location.Left,
            "dropDistance": root.verticalPanelDefaultWidth
        },
        {
            "dock": root.centralDock,
            "dropLocation": Location.Right,
            "dropDistance": root.verticalPanelDefaultWidth
        }
    ]

    readonly property var horizontalPanelDropDestinations: [root.panelTopDropDestination, root.panelBottomDropDestination]

    mainToolBars: [
        DockToolBar {
            id: projectToolBar

            objectName: pageModel.projectToolBarName()
            title: qsTrc("appshell", "Project toolbar")

            floatable: false
            closable: false
            resizable: false
            separatorsVisible: false

            alignment: DockToolBarAlignment.Center
            contentBottomPadding: 2

            compactPriorityOrder: 2

            // GlideMix: the clocks (timecode, BPM, time signature) sit
            // centered under the LED meter.
            thickness: 84

            Column {
                spacing: 4

                // GlideMix: the LED meter -- the mix the audience hears, as a
                // dot matrix in the LOW / MID / HIGH colours. Right-click hides
                // or shows it. (Audio setup and Music library moved to the right,
                // beside the Workspace picker.)
                Item {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: ledMeter.implicitWidth + 16
                    height: ledMeter.implicitHeight + 2

                    MixHouseLedMeter {
                        id: ledMeter
                        anchors.centerIn: parent
                        width: implicitWidth
                        height: implicitHeight
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: function (mouse) {
                            ledMenu.show(Qt.point(mouse.x, mouse.y), [
                                { id: "toggle", title: ledMeter.shown ? qsTrc("appshell", "Hide LED meter")
                                                                      : qsTrc("appshell", "Show LED meter") }
                            ])
                        }
                    }

                    ContextMenuLoader {
                        id: ledMenu
                        onHandleMenuItem: function (itemId) {
                            if (itemId === "toggle") {
                                ledMeter.shown = !ledMeter.shown
                            }
                        }
                    }
                }

                // GlideMix modern look: one capsule display -- the clocks,
                // then key, energy, song n/N, CPU and the play light.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: displayRow.width + 16
                    height: 40
                    radius: 12
                    color: ui.theme.backgroundQuarternaryColor
                    border.width: 1
                    border.color: ui.theme.strokeColor

                    // a faint lighter top edge (no blur)
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: parent.radius - 1
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.05) }
                            GradientStop { position: 0.4; color: Qt.rgba(1, 1, 1, 0) }
                        }
                    }

                    Row {
                        id: displayRow
                        anchors.centerIn: parent
                        spacing: 6

                        PlaybackToolBar {
                            anchors.verticalCenter: parent.verticalCenter

                            clocksOnly: true

                            navigationPanel.section: root.topToolKeyNavSec
                            navigationPanel.order: 3
                        }

                        MixHouseNowPlaying {
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        },
        DockToolBar {
            id: workspacesToolBar

            objectName: pageModel.workspacesToolBarName()
            title: qsTrc("appshell", "Workspaces toolbar")

            floatable: false
            closable: false
            resizable: false
            separatorsVisible: false

            alignment: DockToolBarAlignment.Right
            contentBottomPadding: 2

            compactPriorityOrder: 1

            // GlideMix: Audio setup and Music library, left of the Workspace
            // picker (they made way for the LED meter above the clock).
            Row {
                spacing: 40

                // Level with the Workspace box (its label sits above it).
                ProjectToolBar {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -2

                    isCompactMode: workspacesToolBar.isCompact

                    navigationPanel.section: root.topToolKeyNavSec
                    navigationPanel.order: 2
                }

                WorkspacesToolBar {
                    anchors.verticalCenter: parent.verticalCenter

                    isCompactMode: workspacesToolBar.isCompact

                    navigationPanel.section: root.topToolKeyNavSec
                    navigationPanel.order: 3
                }
            }
        },
        DockToolBar {
            id: undoRedoToolBar

            objectName: pageModel.undoRedoToolBarName()
            title: qsTrc("appshell", "Undo/redo toolbar")

            floatable: false
            closable: false
            resizable: false
            separatorsVisible: false

            alignment: DockToolBarAlignment.Right
            contentBottomPadding: 2

            UndoRedoToolBar {
                navigationPanel.section: root.topToolKeyNavSec
                navigationPanel.order: 4
            }
        }
    ]

    toolBars: [
        DockToolBar {
            id: playbackToolBar

            objectName: pageModel.playbackToolBarName()
            title: qsTrc("appshell", "Play toolbar")

            dropDestinations: [root.toolBarTopDropDestination, root.toolBarBottomDropDestination]

            minimumWidth: 300
            thickness: 48 /* see PlaybackToolBar rowHeight */
            resizable: true

            PlaybackToolBar {
                id: playbackToolBarContent

                floating: playbackToolBar.floating
                onFloatingChanged: {
                    //! HACK: When docking and undocking we recalculate
                    //        the toolbar's content size and it looks ugly for the user.
                    //        Let's hide the content, delayedly relayout the window and show the content.
                    relayout()
                }

                maximumWidth: playbackToolBar.width - 30 /* grip button */
                maximumHeight: playbackToolBar.height

                onHeightChanged: {
                    if (!playbackToolBar.inited) {
                        return
                    }

                    playbackToolBar.thickness = height
                }

                onRelayoutRequested: {
                    relayout()
                }

                navigationPanel.section: root.playbackToolBarKeyNavSec
                navigationPanel.order: 1

                function relayout() {
                    playbackToolBarRelayoutTimer.start()
                }

                Timer {
                    id: playbackToolBarRelayoutTimer
                    interval: 20
                    onTriggered: {
                        root.layoutRequested()
                        playbackToolBarContent.visible = true
                    }
                }
            }
        }
    ]

    panels: [
        DockPanel {
            id: tracksPanel

            property int effectsSectionWidth: 240
            property bool showEffectsSection: false
            property int titleBarHeight: 39
            // The track-control column is independently resizable from the
            // realtime-effects column.  Keep the user's width while the
            // effects panel is opened/closed and only add/remove the effects
            // width around it.
            property int trackHeaderWidth: root.verticalPanelDefaultWidth
            readonly property int minimumTrackHeaderWidth: 180
            readonly property int maximumTrackHeaderWidth: 520
            property int panelWidth: trackHeaderWidth + (showEffectsSection ? effectsSectionWidth : 0)
            property bool updatingPanelWidth: false
            property int resizeStartTrackHeaderWidth: trackHeaderWidth
            property int resizeStartEffectsSectionWidth: effectsSectionWidth
            property real resizeStartEffectsPointerX: 0

            signal add(type: int)

            objectName: pageModel.tracksPanelName()
            title: qsTrc("appshell", "Tracks")

            closable: false
            floatable: false
            resizable: true

            minimumWidth: minimumTrackHeaderWidth + (showEffectsSection ? effectsSectionWidth : 0)
            maximumWidth: maximumTrackHeaderWidth + (showEffectsSection ? effectsSectionWidth : 0)

            Component.onCompleted: {
                // Give the dock its initial size once.  After that, the dock
                // layout must be free to change width while the user drags
                // its splitter; keeping a permanent `width: panelWidth`
                // binding would immediately snap every drag back.
                width = panelWidth
            }

            onWidthChanged: {
                if (!updatingPanelWidth) {
                    const newTrackHeaderWidth = Math.max(minimumTrackHeaderWidth,
                                                         Math.min(maximumTrackHeaderWidth,
                                                                  width - (showEffectsSection ? effectsSectionWidth : 0)))
                    if (trackHeaderWidth !== newTrackHeaderWidth) {
                        trackHeaderWidth = newTrackHeaderWidth
                    }
                }
            }

            onShowEffectsSectionChanged: {
                updatingPanelWidth = true
                width = trackHeaderWidth + (showEffectsSection ? effectsSectionWidth : 0)
                updatingPanelWidth = false
            }

            minimumHeight: 83

            groupName: root.verticalPanelsGroup

            location: Location.Left

            Column {
                anchors.fill: parent
                spacing: 0

                Component.onCompleted: {
                    tracksNavModel.fallbackNavigationControl = trackstitleBarItem.addTrackNavigation
                }

                TracksTitleBar {
                    id: trackstitleBarItem

                    navigation.section: root.addNewTrackKeyNavSec

                    effectsSectionWidth: tracksPanel.effectsSectionWidth
                    showEffectsSection: tracksPanel.showEffectsSection

                    height: tracksPanel.titleBarHeight
                    width: parent.width

                    onAddRequested: function (type) {
                        tracksPanel.add(type)
                    }

                    onEffectsSectionCloseRequested: {
                        tracksPanel.showEffectsSection = false
                    }
                }

                // GlideMix: the TEMPO lane's header, level with the lane
                // under the ruler (parked app: "TEMPO · MASTER" + Reset).
                Rectangle {
                    id: tempoLaneHeader

                    width: parent.width
                    // GlideMix: the field shows the tempo line, the mix's
                    // energy, or both.
                    readonly property bool tempoOn: tempoLaneModel.laneVisible
                    readonly property bool energyOn: tempoLaneModel.energyLaneVisible
                    height: (tempoOn || energyOn) ? tempoLaneModel.laneHeight : 0
                    visible: tempoOn || energyOn
                    color: ui.theme.backgroundPrimaryColor

                    MixHouseTempoLaneModel {
                        id: tempoLaneModel
                    }

                    StyledTextLabel {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: resetTempoButton.verticalCenter
                        text: !tempoLaneHeader.tempoOn ? qsTrc("mixhouse", "ENERGY · 1-10")
                              : tempoLaneHeader.energyOn ? qsTrc("mixhouse", "TEMPO · ENERGY")
                              : qsTrc("mixhouse", "TEMPO · MASTER")
                        font: ui.theme.bodyBoldFont
                    }

                    FlatButton {
                        id: resetTempoButton

                        anchors.top: parent.top
                        anchors.topMargin: 6
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        height: 22
                        visible: tempoLaneHeader.tempoOn
                        text: qsTrc("mixhouse", "Reset")
                        toolTipTitle: qsTrc("mixhouse", "Reset the tempo line to the tracks' own tempos")
                        onClicked: tempoLaneModel.reset()
                    }

                    // MixMeister: tempo follows the tracks (Beatmix glides),
                    // and Master BPM moves the whole mix.
                    Row {
                        anchors.top: resetTempoButton.bottom
                        anchors.topMargin: 6
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        spacing: 4
                        visible: tempoLaneHeader.tempoOn && tempoLaneHeader.height >= 62

                        FlatButton {
                            height: 22
                            text: qsTrc("mixhouse", "Follow tracks")
                            accentButton: tempoLaneModel.followsTracks
                            toolTipTitle: qsTrc("mixhouse", "Tempo follows the tracks")
                            toolTipDescription: qsTrc("mixhouse", "Each track plays at its own BPM; where tracks overlap the tempo glides from the outgoing to the incoming one (MixMeister Beatmix). Rebuilt as you move tracks; drawing on the line turns it off")
                            onClicked: tempoLaneModel.followsTracks = !tempoLaneModel.followsTracks
                        }
                        FlatButton {
                            width: 26
                            height: 22
                            text: "−"
                            toolTipTitle: qsTrc("mixhouse", "Master BPM −1")
                            toolTipDescription: qsTrc("mixhouse", "The whole mix 1 BPM slower (at the playhead), keeping the tempo line's shape")
                            onClicked: tempoLaneModel.nudgeMasterBpm(-1)
                        }
                        FlatButton {
                            width: 26
                            height: 22
                            text: "+"
                            toolTipTitle: qsTrc("mixhouse", "Master BPM +1")
                            toolTipDescription: qsTrc("mixhouse", "The whole mix 1 BPM faster (at the playhead), keeping the tempo line's shape")
                            onClicked: tempoLaneModel.nudgeMasterBpm(1)
                        }
                    }

                    // Drag the bottom edge to resize the lane; double-click
                    // restores the default height.
                    MouseArea {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 8
                        cursorShape: Qt.SizeVerCursor

                        property real startY: 0
                        property int startHeight: 0

                        onPressed: function (e) {
                            startY = mapToGlobal(e.x, e.y).y
                            startHeight = tempoLaneModel.laneHeight
                        }
                        onPositionChanged: function (e) {
                            if (pressed) {
                                tempoLaneModel.laneHeight = startHeight + (mapToGlobal(e.x, e.y).y - startY)
                            }
                        }
                        onDoubleClicked: tempoLaneModel.laneHeight = tempoLaneModel.defaultLaneHeight
                    }

                    SeparatorLine {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        color: ui.theme.strokeColor
                    }
                }

                TracksPanel {
                    id: tracksPanelContent

                    width: parent.width
                    height: parent.height - trackstitleBarItem.height - tempoLaneHeader.height

                    navigationPanels: tracksNavModel.trackItemPanels
                    headerNavigationPanels: tracksNavModel.trackHeaderPanels
                    effectColumnNavigationPanel: trackstitleBarItem.closeEffectsNavigation
                    effectsSectionWidth: tracksPanel.effectsSectionWidth

                    trackEffectsNavigationSection: root.trackEffectsKeyNavSec
                    masterEffectsNavigationSection: root.masterEffectsKeyNavSec

                    onOpenEffectsRequested: {
                        tracksPanel.showEffectsSection = true
                    }

                    onShowEffectsSectionChanged: {
                        tracksPanel.showEffectsSection = showEffectsSection
                    }

                    onEffectsSectionResizeStarted: function(globalX) {
                        tracksPanel.resizeStartEffectsPointerX = globalX
                        tracksPanel.resizeStartEffectsSectionWidth = tracksPanel.effectsSectionWidth
                    }

                    onEffectsSectionResizeMoved: function(globalX) {
                        const delta = globalX - tracksPanel.resizeStartEffectsPointerX
                        const newWidth = Math.max(180, Math.min(520,
                                                               tracksPanel.resizeStartEffectsSectionWidth + delta))
                        if (newWidth === tracksPanel.effectsSectionWidth) {
                            return
                        }

                        tracksPanel.effectsSectionWidth = newWidth
                        tracksPanel.updatingPanelWidth = true
                        tracksPanel.width = tracksPanel.trackHeaderWidth + newWidth
                        tracksPanel.updatingPanelWidth = false
                    }

                    onPanelActive: function (trackId) {
                        tracksNavModel.moveFocusTo(trackId)
                    }

                    DropArea {
                        anchors.fill: parent

                        onEntered: function (drop) {
                            root.externalDropAreaEntered(drop)
                        }

                        onExited: {
                            root.externalDropAreaExited()
                        }

                        onDropped: function (drop) {
                            root.externalDropAreaDropped(drop)
                        }
                    }

                    Connections {
                        target: tracksPanel

                        function onAdd(type) {
                            tracksPanelContent.tracksModel.addTrack(type)
                        }

                        function onShowEffectsSectionChanged() {
                            tracksPanelContent.showEffectsSection = tracksPanel.showEffectsSection
                        }
                    }
                }
            }

        },
        DockPanel {
            id: playbackMeterPanel

            objectName: pageModel.playbackMeterPanelName()
            title: qsTrc("appshell", "Playback meter")

            navigationSection: root.navigationPanelSec(playbackMeterPanel.location)

            closable: false
            floatable: false

            width: 56
            minimumWidth: 56
            maximumWidth: 56

            location: Location.Right

            visible: false

            PlaybackMeterPanel {
                navigationSection: playbackMeterPanel.navigationSection
                navigationOrderStart: playbackMeterPanel.contentNavigationPanelOrderStart
            }
        },
        DockPanel {
            id: historyPanel

            objectName: root.pageModel.historyPanelName()
            title: qsTrc("appshell", "History")

            navigationSection: root.navigationPanelSec(historyPanel.location)

            width: root.verticalPanelDefaultWidth
            minimumWidth: root.verticalPanelDefaultWidth
            maximumWidth: root.verticalPanelDefaultWidth

            groupName: root.verticalPanelsGroup
            location: Location.Right

            //! NOTE: hidden by default
            visible: false

            dropDestinations: root.verticalPanelDropDestinations

            HistoryPanel {
                navigationSection: historyPanel.navigationSection
                navigationOrderStart: historyPanel.contentNavigationPanelOrderStart
            }
        },

        // MixHouse DJ 2: the music library, docked along the bottom the way it sat
        // in the Mixxx fork. Appended AFTER Audacity's own panels so none of their
        // positions change, and resizable via the standard dock splitter -- the
        // height below is a starting size with a minimum, not a fixed value.
        DockPanel {
            id: mixHouseLibraryPanel

            objectName: "mixHouseLibraryPanel"
            title: qsTrc("appshell", "Music library")

            navigationSection: root.navigationPanelSec(mixHouseLibraryPanel.location)

            // Full width only: docked under the timeline alone, the lane
            // headers ran on beside it and no longer lined up with the clips.
            dropDestinations: root.horizontalPanelDropDestinations

            height: 260
            minimumHeight: 120

            // The comment above always claimed this; the property that actually
            // does it was never set. DockBase::resizable defaults to false
            // (docktypes.h), so the splitter never engaged -- nothing to do
            // with any of this panel's own QML, which never had a way to
            // touch this dock-level flag at all.
            resizable: true

            location: Location.Bottom

            //! GlideMix: shares its tabs with the Project panel below.
            groupName: root.bottomPanelsGroup

            //! GlideMix: + beside the tabs opens a closed one again (Chris).
            toolbarComponent: bottomTabsPlus

            //! GlideMix: open by default -- the library is where a mix starts.
            //! The header's Music library button toggles it.
            visible: true

            MixHouseLibraryPanel {
                navigationSection: mixHouseLibraryPanel.navigationSection
                onRaiseRequested: mixHouseLibraryPanel.raiseTab()
            }
        },

        // GlideMix: the Project tab beside the library -- MixMeister's
        // playlist window: the mix's songs in order, click to go to one,
        // drag to reorder (docs/mixmeister-vs-glidemix.md).
        DockPanel {
            id: mixHouseProjectPanel

            objectName: "mixHouseProjectPanel"
            title: qsTrc("appshell", "Project")

            navigationSection: root.navigationPanelSec(mixHouseProjectPanel.location)

            dropDestinations: root.horizontalPanelDropDestinations

            height: 260
            minimumHeight: 120
            resizable: true

            location: Location.Bottom
            groupName: root.bottomPanelsGroup

            //! GlideMix: + beside the tabs opens a closed one again (Chris).
            toolbarComponent: bottomTabsPlus

            //! Closed here: ProjectPageModel opens it as a tab of the library
            //! once that is docked (open from the start, both floated).
            visible: false

            MixHouseProjectPanel {
                navigationSection: mixHouseProjectPanel.navigationSection
            }
        },

        // GlideMix: the Related tab -- the full song table showing the songs
        // that go with the song under the playhead (Chris: a tab, not a side
        // panel, so every column is there). Opened by the library's Related
        // button or the + menu.
        // GlideMix: the Samples tab -- the user's own sample and FX folders.
        DockPanel {
            id: mixHouseSamplesPanel

            objectName: "mixHouseSamplesPanel"
            title: qsTrc("appshell", "Samples")

            navigationSection: root.navigationPanelSec(mixHouseSamplesPanel.location)

            dropDestinations: root.horizontalPanelDropDestinations

            height: 260
            minimumHeight: 120
            resizable: true

            location: Location.Bottom
            groupName: root.bottomPanelsGroup

            toolbarComponent: bottomTabsPlus

            // Always there (Chris): Music library, Project, Samples, Related.
            // Not open at load (a fresh install floated the library then):
            // ProjectPageModel opens it once after start (kSamplesTabShipped).
            visible: false

            MixHouseSamplesPanel {
                navigationSection: mixHouseSamplesPanel.navigationSection
                onRaiseRequested: mixHouseSamplesPanel.raiseTab()
            }
        },

        DockPanel {
            id: mixHouseRelatedPanel

            objectName: "mixHouseRelatedPanel"
            title: qsTrc("appshell", "Related")

            navigationSection: root.navigationPanelSec(mixHouseRelatedPanel.location)

            dropDestinations: root.horizontalPanelDropDestinations

            height: 260
            minimumHeight: 120
            resizable: true

            location: Location.Bottom
            groupName: root.bottomPanelsGroup

            toolbarComponent: bottomTabsPlus

            // Closed at the first start, as in 1.2.6: open from the start, it and the
            // library were taken for tabs of each other before either was docked and
            // both floated (fresh 1.2.7 install, 2026-11-11). "Always there" comes with
            // Samples (kSamplesTabShipped) and needs that fixed first.
            visible: false

            MixHouseLibraryPanel {
                relatedMode: true
                navigationSection: mixHouseRelatedPanel.navigationSection
            }
        },


        // GlideMix: the Beatgrid editor, the Beatgrid workspace's main
        // panel; opened with a track from the library or a clip's menu.
        DockPanel {
            id: mixHouseGridEditorPanel

            objectName: "mixHouseGridEditorPanel"
            title: qsTrc("appshell", "Beatgrid editor")

            navigationSection: root.navigationPanelSec(mixHouseGridEditorPanel.location)

            // Full width only: docked under the timeline alone, the lane
            // headers ran on beside it and no longer lined up with the clips.
            dropDestinations: root.horizontalPanelDropDestinations

            height: 460
            minimumHeight: 300
            resizable: true

            location: Location.Bottom

            visible: false

            MixHouseGridEditorPanel {
                navigationSection: mixHouseGridEditorPanel.navigationSection
            }
        }
    ]

    central: TracksItemsView {
        id: tracksItemsView

        navPanels: tracksNavModel.viewItemPanels
        navDefaultControl: tracksNavModel.defaultNavigationControl

        Binding {
            target: tracksNavModel
            property: "trackViewItem"
            value: tracksItemsView.tracksAreaItem
        }

        Connections {
            target: root

            function onExternalDropAreaEntered(drop) {
                tracksItemsView.externalDropAreaEntered(drop)
            }

            function onExternalDropAreaExited() {
                tracksItemsView.externalDropAreaExited()
            }

            function onExternalDropAreaDropped(drop) {
                tracksItemsView.externalDropAreaDropped(drop)
            }
        }
    }

    statusBar: DockStatusBar {
        objectName: pageModel.statusBarName()

        property int thickness: 40

        height: thickness
        minimumHeight: thickness
        maximumHeight: thickness

        navigationSection: content.navigationSection

        ProjectStatusBar {
            id: content
        }
    }
}
