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
import QtQuick 2.15

import Muse.Ui 1.0
import Muse.UiComponents
import Muse.UiComponents.LegacyTreeView

Item {
    id: root

    property alias model: treeView.model

    //! GlideMix: set when the dialog opens -- the current page's entry takes
    //! keyboard focus, instead of the first control on the page (which put
    //! the cursor in the temporary folder field with its text selected).
    property bool focusCurrentPage: false

    property NavigationPanel navigation: NavigationPanel {
        name: "PreferencesMenuPanel"
        enabled: root.enabled && root.visible
        direction: NavigationPanel.Both
        onActiveChanged: function (active) {
            if (active) {
                root.forceActiveFocus()
            }
        }

        onNavigationEvent: function (event) {
            if (event.type === NavigationEvent.AboutActive) {
                event.setData("controlIndex", prv.currentItemNavigationIndex)
            }
        }
    }

    QtObject {
        id: prv

        property var currentItemNavigationIndex: []
    }

    Rectangle {
        id: background
        anchors.fill: parent
        color: ui.theme.backgroundPrimaryColor
    }

    LegacyTreeView {
        id: treeView

        anchors.fill: parent
        anchors.topMargin: 12

        alternatingRowColors: false
        headerVisible: false

        TableViewColumn {
            role: "itemRole"
        }

        style: LegacyTreeViewStyle {
            indentation: 0
            branchDelegate: null

            backgroundColor: background.color

            rowDelegate: Item {
                id: rowTreeDelegate

                height: 36
                width: parent.width
            }
        }

        itemDelegate: PageTabButton {
            property bool expanded: Boolean(model) ? model.itemRole.expanded : false
            property int navigationRow: styleData.index.row
            property int navigationColumn: styleData.depth

            orientation: Qt.Horizontal

            spacing: 16
            leftPadding: spacing * (styleData.depth + 1)

            normalStateFont: ui.theme.bodyFont
            selectedStateFont: ui.theme.bodyBoldFont

            title: Boolean(model) ? model.itemRole.title : ""
            checked: Boolean(model) && Boolean(model.itemRole) ? model.itemRole.id === treeView.model.currentPageId : false
            enabled: visible

            navigation.name: "PreferencesMenuItem"
            navigation.panel: root.navigation
            navigation.row: navigationRow
            navigation.column: navigationColumn
            navigation.accessible.name: title
            navigation.accessible.role: MUAccessible.ListItem
            navigation.onActiveChanged: {
                if (navigation.active) {
                    treeView.model.selectRow(styleData.index)
                }
            }

            function takeFocusIfCurrent() {
                if (root.focusCurrentPage && checked) {
                    root.focusCurrentPage = false
                    navigation.requestActive()
                }
            }

            Connections {
                target: root
                function onFocusCurrentPageChanged() { takeFocusIfCurrent() }
            }

            Component.onCompleted: {
                updateExpandedState()
                takeFocusIfCurrent()
            }

            onExpandedChanged: {
                updateExpandedState()
            }

            function updateExpandedState() {
                if (expanded) {
                    treeView.expand(styleData.index)
                } else {
                    treeView.collapse(styleData.index)
                }
            }

            iconComponent: Item {
                width: 24
                height: width

                readonly property string iconPath: Boolean(model) ? model.itemRole.iconPath : ""

                StyledIconLabel {
                    anchors.fill: parent
                    visible: parent.iconPath.length === 0
                    iconCode: Boolean(model) ? model.itemRole.icon : IconCode.NONE
                }

                // GlideMix: pages the icon font has no glyph for carry
                // their own image, tinted like the glyphs around it.
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    visible: parent.iconPath.length > 0
                    source: parent.iconPath
                    sourceSize: Qt.size(36, 36)
                }
            }

            onCheckedChanged: {
                if (checked) {
                    prv.currentItemNavigationIndex = [navigationRow, navigationColumn]
                }
            }

            onClicked: {
                treeView.model.selectRow(styleData.index)
            }
        }
    }
}
