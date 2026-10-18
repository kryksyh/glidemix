/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 */
import QtQuick 2.15

import Muse.Ui 1.0
import Muse.UiComponents

Item {
    id: root

    property var colors: []
    property int currentColorIndex: -1
    property NavigationPanel navigationPanel: NavigationPanel {
        name: "AccentColorsList"
        enabled: root.enabled && root.visible
        direction: NavigationPanel.Horizontal
    }

    property int navigationRow: -1
    property int navigationColumnStart: 0
    property int navigationColumnEnd: navigationColumnStart + colors.length

    // The supplied Logic Pro palette is a 24-column, four-row grid.
    property int columns: 24
    property real sampleSize: 24
    property real sampleSpacing: 4

    signal accentColorChangeRequested(var newColorIndex)

    implicitWidth: palette.implicitWidth
    implicitHeight: palette.implicitHeight

    Grid {
        id: palette
        columns: root.columns
        columnSpacing: root.sampleSpacing
        rowSpacing: 6

        Repeater {
            model: root.colors

            delegate: Item {
                id: swatchButton
                width: root.sampleSize + 4
                height: root.sampleSize + 4

                readonly property bool selected: root.currentColorIndex === index

                Rectangle {
                    id: swatch
                    anchors.centerIn: parent
                    width: root.sampleSize
                    height: root.sampleSize
                    radius: 5
                    color: modelData
                    border.color: swatchButton.selected ? ui.theme.fontPrimaryColor : ui.theme.strokeColor
                    border.width: swatchButton.selected ? 3 : 1
                }

                HoverHandler { id: hoverHandler }

                states: State {
                    when: hoverHandler.hovered && !swatchButton.selected
                    PropertyChanges { target: swatch; scale: 1.12 }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.accentColorChangeRequested(index)
                }
            }
        }
    }
}
