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

pragma ComponentBehavior: Bound

import QtQuick

import Muse.Ui
import Muse.UiComponents

Item {
    id: root

    property alias model: repeater.model

    property alias spacing: content.spacing

    property int leftPadding: 0
    property int rightPadding: 0
    property int topPadding: 0
    property int bottomPadding: 0

    property int rowHeight: 32
    property int separatorHeight: rowHeight

    property bool useSpacersAsSeparators: false
    property int spacerWidth: 12

    //! GlideMix modern look: a faint rounded capsule behind each group of
    //! items between separators (static rectangles, redrawn only on layout).
    property bool capsuleGroups: false
    property var groupRects: []

    function updateGroupRects() {
        if (!root.capsuleGroups) {
            return
        }
        var rects = []
        var cur = null
        var kids = content.children
        for (var i = 0; i < kids.length; ++i) {
            var c = kids[i]
            if (!c || c.item === undefined || !c.visible || c.width <= 0) {
                continue
            }
            var isSep = Boolean(c.item) && c.item.type === ToolBarItemType.SEPARATOR
            if (isSep || (cur && Math.abs(c.y - cur.y) > 1)) {
                if (cur) { rects.push(cur) }
                cur = null
                if (isSep) { continue }
            }
            if (!cur) {
                cur = { x: c.x, y: c.y, r: c.x + c.width }
            } else {
                cur.r = c.x + c.width
            }
        }
        if (cur) { rects.push(cur) }
        root.groupRects = rects
    }

    Timer {
        id: groupTimer
        interval: 0
        onTriggered: root.updateGroupRects()
    }

    Repeater {
        model: root.capsuleGroups ? root.groupRects : []
        delegate: Rectangle {
            required property var modelData
            readonly property int pad: 4
            x: content.x + modelData.x - pad
            y: content.y + modelData.y + (root.rowHeight - height) / 2
            width: modelData.r - modelData.x + 2 * pad
            height: Math.min(root.rowHeight, 34)
            radius: height / 2
            color: ui.theme.isDark ? Qt.rgba(1, 1, 1, 0.035) : Qt.rgba(0, 0, 0, 0.035)
            border.width: 1
            border.color: Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g,
                                  ui.theme.fontPrimaryColor.b, 0.10)
        }
    }

    property int maximumWidth: -1
    property bool isMultiline: height > rowHeight

    property NavigationPanel navigationPanel: NavigationPanel {
        name: root.objectName !== "" ? root.objectName : "ToolBarView"
        enabled: root.enabled && root.visible

        accessible.name: "ToolBar"
        accessible.visualItem: root
    }

    property var sourceComponentCallback

    width: content.width + leftPadding + rightPadding
    height: content.height + topPadding + bottomPadding

    Component.onCompleted: {
        if (root.model) {
            root.model.load()
        }
    }

    Flow {
        id: content

        anchors.left: root.left
        anchors.leftMargin: root.leftPadding
        anchors.top: root.top
        anchors.topMargin: root.topPadding

        width: {
            if (root.model.length === 0) {
                return 0
            }

            var result = 0
            var children = content.children

            for (var i = 0; i < children.length; ++i) {
                result += children[i].width + spacing
            }

            if (result > 0) {
                result -= spacing
            }

            return root.maximumWidth !== -1 ? Math.min(result, root.maximumWidth) : result
        }
        height: childrenRect.height

        onWidthChanged: groupTimer.restart()
        onHeightChanged: groupTimer.restart()
        onPositioningComplete: groupTimer.restart()

        clip: true
        spacing: 4

        Repeater {
            id: repeater

            Item {
                id: delegateItem

                required property ToolBarItem item
                required property int index

                width: loader.width
                height: root.rowHeight

                Loader {
                    id: loader

                    property int itemIndex: delegateItem.index

                    anchors.verticalCenter: parent.verticalCenter

                    sourceComponent: {
                        if (!Boolean(delegateItem.item)) {
                            return null
                        }

                        var type = delegateItem.item.type
                        var comp = Boolean(root.sourceComponentCallback) ? root.sourceComponentCallback(type) : null
                        if (Boolean(comp)) {
                            return comp
                        }

                        switch(type) {
                        case ToolBarItemType.ACTION: return actionComp
                        case ToolBarItemType.SEPARATOR: return root.useSpacersAsSeparators ? spacerComp : separatorComp
                        }

                        return null
                    }

                    onLoaded: {
                        loader.item.itemData = delegateItem.item

                        if (Boolean(loader.item.navigation)) {
                            loader.item.navigation.panel = root.navigationPanel
                            loader.item.navigation.row = -1
                            loader.item.navigation.column = delegateItem.index * 100
                        }
                    }

                    Component {
                        id: separatorComp

                        SeparatorLine {
                            property var itemData: delegateItem.item

                            width: 1
                            height: root.separatorHeight

                            orientation: Qt.Vertical
                        }
                    }

                    Component {
                        id: spacerComp

                        Item {
                            property var itemData: delegateItem.item

                            width: root.spacerWidth
                            height: root.separatorHeight
                        }
                    }

                    Component {
                        id: actionComp

                        StyledToolBarItem {
                            itemData: delegateItem.item
                        }
                    }
                }
            }
        }
    }
}
