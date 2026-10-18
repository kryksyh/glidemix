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

import Muse.Ui

Rectangle {
    id: root

    anchors.fill: parent

    // Same hover treatment as the MixHouse library panel's own sidebar
    // splitter (SplitHandle.hovered there): a quiet strokeColor hairline at
    // rest, brightening to the theme's accent color under the pointer or
    // while actually being dragged -- applied here to EVERY dock-panel
    // resize separator in the app (this is the one shared component all of
    // them render through), not a MixHouse-only copy.
    readonly property bool hovered: mouseArea.containsMouse || mouseArea.pressed
    color: hovered ? ui.theme.accentColor : ui.theme.strokeColor
    Behavior on color { ColorAnimation { duration: 80 } }

    readonly property QtObject separatorCpp: parent

    visible: separatorCpp && separatorCpp.isSeparatorVisible

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        anchors.margins: -5 //! NOTE: extra space for user convenience
        hoverEnabled: true

        cursorShape: (root.separatorCpp && separatorCpp.showResizeCursor)
                     ? (root.separatorCpp.isVertical ? Qt.SizeVerCursor : Qt.SizeHorCursor)
                     : Qt.ArrowCursor

        onPressed: {
            root.separatorCpp.onMousePressed()
        }

        onReleased: {
            root.separatorCpp.onMouseReleased()
        }

        onPositionChanged: function(mouse) {
            root.separatorCpp.onMouseMoved(Qt.point(mouse.x, mouse.y))
        }

        onDoubleClicked: {
            root.separatorCpp.onMouseDoubleClicked()
        }
    }
}
