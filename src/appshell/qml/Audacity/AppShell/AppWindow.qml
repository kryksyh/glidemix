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
import QtQuick.Controls

import Muse.Ui 1.0
import Muse.Shortcuts 1.0

import Audacity.AppShell 1.0
import Muse.Toast 1.0

ApplicationWindow {
    id: root

    default property alias windowContent: windowContentItem.data

    objectName: "ApplicationWindow"

    title: titleProvider.title

    width: 1150
    height: 800

    minimumWidth: 500
    minimumHeight: 500

    visible: false

    color: ui.theme.backgroundPrimaryColor

    // GlideMix: since Qt 6.9 the window pads its content by the screen's
    // safe area. On a MacBook with a notch that left an empty band above
    // the toolbars, though the window sits below the menu bar. In full
    // screen the content does reach the notch, and needs the room.
    topPadding: root.visibility === Window.FullScreen ? root.fullScreenTopInset : 0

    // Measured once full screen has settled: while the menu bar hides it is
    // 0, and showing it on hover then overlays, as in other Mac apps.
    property real fullScreenTopInset: 0
    onVisibilityChanged: {
        fullScreenTopInset = 0
        if (visibility === Window.FullScreen) {
            fullScreenInsetTimer.restart()
        }
    }
    Timer {
        id: fullScreenInsetTimer
        interval: 1200
        onTriggered: root.fullScreenTopInset = bridge.fullScreenTopInset()
    }
    leftPadding: 0
    rightPadding: 0
    bottomPadding: 0

    Component.onCompleted: {
        ui.rootItem = root.contentItem
        titleProvider.load()
    }

    MainWindowTitleProvider {
        id: titleProvider
    }

    MainWindowBridge {
        id: bridge

        window: root

        //! NOTE These properties of QWindow (of which ApplicationWindow is derived)
        //!      are not available in QML, so we access them via MainWindowBridge
        filePath: titleProvider.filePath
        fileModified: titleProvider.fileModified
    }

    ToolTipProvider {}

    //! NOTE Need only create
    Shortcuts {}

    Item {
        id: windowContentItem
        anchors.fill: parent
    }

    WindowDropArea {
        anchors.fill: parent
    }

    ToastProvider {}

    function showMinimizedWithSavePreviousState() {
        bridge.showMinimizedWithSavePreviousState()
    }
}
