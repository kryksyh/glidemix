/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import Muse.Dock

Rectangle {
    id: root

    //! NOTE: please, don't rename those properties because they are used in c++
    property QtObject frameCpp
    readonly property QtObject titleBarCpp: Boolean(frameCpp) ? frameCpp.actualTitleBar : null
    readonly property int nonContentsHeight: titleBar.height + tabBar.height + stackLayout.anchors.topMargin
                                             + root.cardGap
    property int titleBarNavigationPanelOrder: 1
    //! ---

    readonly property bool hasTitleBar: frameModel.titleBarAllowed && !(prv.tabsModel.numTabs > 1 || frameModel.isHorizontalPanel)
    readonly property bool hasSingleTab: frameModel.titleBarAllowed && prv.tabsModel.numTabs === 1 && frameModel.isHorizontalPanel
    readonly property bool hasTabBar: frameModel.titleBarAllowed && (prv.tabsModel.numTabs > 1 || frameModel.isHorizontalPanel)

    anchors.fill: parent

    //! GlideMix modern look: every panel is a rounded card with a small dark
    //! gap around it. The corners are four small pieces in the gap colour,
    //! not a mask over the whole panel, so nothing extra is drawn per frame.
    readonly property int cardGap: frameModel.isPanel ? 4 : 0
    readonly property int cardRadius: 8
    readonly property color gapColor: ui.theme.isDark ? ui.theme.backgroundQuarternaryColor : ui.theme.backgroundSecondaryColor
    color: frameModel.isPanel ? gapColor : ui.theme.backgroundPrimaryColor

    Rectangle {
        id: card
        anchors.fill: parent
        anchors.margins: root.cardGap
        radius: root.cardRadius
        color: ui.theme.backgroundPrimaryColor
        visible: frameModel.isPanel
    }

    onFrameCppChanged: {
        if (Boolean(frameCpp)) {
            frameCpp.setStackLayout(stackLayout)
        }
    }

    onNonContentsHeightChanged: {
        if (Boolean(frameCpp)) {
            frameCpp.geometryUpdated()
        }
    }

    DockFrameModel {
        id: frameModel
        frame: root.frameCpp
    }

    QtObject {
        id: prv
        readonly property alias tabsModel: frameModel.tabsModel
        readonly property int currentIndex: Boolean(root.frameCpp) && root.frameCpp.currentIndex >= 0 ? root.frameCpp.currentIndex : 0
    }

    NavigationPanel {
        id: navPanel
        name: root.objectName + "PanelTabs"
        enabled: root.enabled && root.visible
        section: frameModel.navigationSection
        order: root.titleBarNavigationPanelOrder

        onNavigationEvent: function(event) {
            if (event.type === NavigationEvent.AboutActive && tabBar.visible) {
                event.setData("controlName", tabBar.currentItemNavigationName)
            }
        }
    }

    DockTitleBar {
        id: titleBar

        anchors.top: parent.top
        anchors.topMargin: root.cardGap
        x: root.cardGap
        width: parent.width - 2 * root.cardGap
        height: visible ? implicitHeight : 0

        visible: root.hasTitleBar

        property alias titleBar: frameModel.titleBar

        titleBarCpp: root.titleBarCpp

        navigationPanel: navPanel
        navigationOrder: 1

        onHandleContextMenuItemRequested: function(itemId) {
            frameModel.handleMenuItem(itemId)
        }

        titleBarItem: frameModel.titleBar

        function updateContextMenu() {
            const idx = prv.tabsModel.index(prv.currentIndex, 0);
            const menuModel = prv.tabsModel.data(idx, DockTabsModel.ContextMenu)
            titleBar.contextMenuModel = menuModel
        }

        Connections {
            target: prv.tabsModel
            function onDataChanged() { titleBar.updateContextMenu() }
            function onModelReset() { titleBar.updateContextMenu() }
        }
    }

    DockTabBar {
        id: tabBar

        anchors.top: parent.top
        anchors.topMargin: root.cardGap
        x: root.cardGap
        width: parent.width - 2 * root.cardGap
        height: visible ? 35 : 0

        visible: root.hasTabBar
        draggingTabsAllowed: root.hasTabBar && !root.hasSingleTab

        tabBarCpp: Boolean(root.frameCpp) ? root.frameCpp.tabWidget.tabBar : null
        tabsModel: prv.tabsModel
        currentIndex: prv.currentIndex

        navigationPanel: navPanel

        function setCurrentDockWidget(index: int) {
            if (root.frameCpp) {
                root.frameCpp.tabWidget.setCurrentDockWidget(index)
            }
        }

        onCurrentIndexChanged: {
            setCurrentDockWidget(currentIndex)
        }

        onTabClicked: function(index) {
            setCurrentDockWidget(index)
        }

        onHandleContextMenuItemRequested: function(itemId) {
            frameModel.handleMenuItem(itemId)
        }
    }

    DockTitleBarMouseArea {
        id: titleBarMouseArea

        readonly property var titleBarComp: tabBar.visible ? tabBar
                                                           : titleBar.visible ? titleBar : null
        anchors.fill: titleBarComp

        enabled: root.hasTitleBar || root.hasSingleTab
        propagateComposedEvents: true

        titleBarCpp: root.titleBarCpp

        onDoubleClicked: function(mouse) {
            if (titleBarComp) {
                let pos = titleBarMouseArea.mapToItem(titleBarComp, mouse.x, mouse.y)
                titleBarComp.doubleClicked(pos)
            }
        }
    }

    StackLayout {
        id: stackLayout

        anchors.top: tabBar.visible ? tabBar.bottom
                                    : titleBar.visible ? titleBar.bottom : parent.top
        anchors.topMargin: tabBar.visible ? 12 : (titleBar.visible ? 0 : root.cardGap)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.cardGap

        x: root.cardGap
        width: parent.width - 2 * root.cardGap

        currentIndex: {
            var currentDockUniqueName = frameModel.currentDockUniqueName

            for (var i in children) {
                if (children[i].uniqueName === currentDockUniqueName) {
                    return i
                }
            }

            return 0
        }
    }

    Rectangle {
        visible: frameModel.highlightingVisible

        x: frameModel.highlightingRect.x
        y: frameModel.highlightingRect.y
        width: frameModel.highlightingRect.width
        height: frameModel.highlightingRect.height

        color: "transparent"

        border.width: 1
        border.color: ui.theme.accentColor

        Rectangle {
            anchors.fill: parent

            color: ui.theme.accentColor
            opacity: 0.3
        }
    }

    //! GlideMix: the card's rounded corners, drawn over the panel's content.
    Repeater {
        model: 4
        delegate: Canvas {
            id: corner
            required property int index
            readonly property bool atRight: index === 1 || index === 3
            readonly property bool atBottom: index >= 2
            width: root.cardRadius
            height: root.cardRadius
            x: atRight ? root.width - root.cardGap - width : root.cardGap
            y: atBottom ? root.height - root.cardGap - height : root.cardGap
            z: 1000
            antialiasing: true
            visible: frameModel.isPanel

            readonly property color gapFill: root.gapColor
            onGapFillChanged: requestPaint()

            onPaint: {
                var g = getContext("2d")
                var r = width
                g.reset()
                g.fillStyle = gapFill
                // the square, minus a circle centred on the card's inner corner
                g.fillRect(0, 0, r, r)
                g.globalCompositeOperation = "destination-out"
                g.beginPath()
                g.arc(atRight ? 0 : r, atBottom ? 0 : r, r, 0, 2 * Math.PI, false)
                g.closePath()
                g.fill()
            }
        }
    }
}
