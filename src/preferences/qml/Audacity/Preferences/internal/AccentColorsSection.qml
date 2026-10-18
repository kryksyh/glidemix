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
import QtQuick.Controls 2.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.AppShell
import Audacity.Preferences

Row {
    id: root

    width: parent ? parent.width : implicitWidth

    property alias colors: colorsList.colors
    property alias currentColorIndex: colorsList.currentColorIndex

    property NavigationPanel navigation: NavigationPanel {
        name: titleLabel.text
        enabled: root.enabled && root.visible
        direction: NavigationPanel.Horizontal
        accessible.name: titleLabel.text

        onActiveChanged: function (active) {
            if (active) {
                root.forceActiveFocus()
            }
        }
    }

    property int columnWidth: 0

    signal accentColorChangeRequested(var newColorIndex)

    height: colorsList.height
    spacing: 12

    StyledTextLabel {
        id: titleLabel
        width: root.columnWidth

        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Qt.AlignLeft

        text: qsTrc("appshell/preferences", "Accent color")
    }

    AccentColorsList {
        id: colorsList

        navigationPanel: root.navigation

        // Preserve the large swatches at the new default dialog width, but
        // scale them down on smaller screens so all 24 columns remain visible.
        sampleSize: Math.min(30, Math.max(12,
                                         (root.width - root.columnWidth - root.spacing
                                          - (columns - 1) * sampleSpacing) / columns - 4))

        onAccentColorChangeRequested: function (newColorIndex) {
            root.accentColorChangeRequested(newColorIndex)
        }
    }
}
