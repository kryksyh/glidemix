/*
 * GlideMix
 *
 * A StyledSlider in the DJ-fader look, modern version (2026-11-09, after
 * mock A of 2026-10-28): a thin rounded slot with the value in the accent
 * colour, faint ticks, and a light rounded cap with a dark centre line. (A
 * dark cap vanished against the dark track headers.) Behaviour
 * (drag, wheel, keys, accessibility) is StyledSlider's.
 */
import QtQuick
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents

StyledSlider {
    id: root

    // Ticks above and below the slot (off for short sliders).
    property bool ticks: availableWidth >= 120
    // Where the slot's value colour starts: the left end, or a value (the
    // middle of a bipolar control).
    property real fillOrigin: from

    readonly property real capWidth: 24
    readonly property color tickColor: Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g,
                                               ui.theme.fontPrimaryColor.b, 0.25)
    readonly property real capHeight: Math.max(12, Math.min(16, availableHeight))

    implicitHeight: 22

    background: Item {
        x: root.leftPadding
        y: root.topPadding
        width: root.availableWidth
        height: root.availableHeight

        NavigationFocusBorder {
            navigationCtrl: root.navigation
        }

        readonly property real travel: width - root.capWidth
        readonly property real originX: root.capWidth / 2
                                        + travel * Math.max(0, Math.min(1, (root.fillOrigin - root.from) / (root.to - root.from)))
        readonly property real valueX: root.capWidth / 2 + travel * root.visualPosition

        Repeater {
            model: root.ticks ? 11 : 0
            delegate: Item {
                required property int index
                readonly property real tx: root.capWidth / 2 + parent.travel * index / 10
                Rectangle {
                    x: tx - 0.5
                    y: 0
                    width: 1
                    height: index % 5 === 0 ? 4 : 2
                    color: root.tickColor
                }
                Rectangle {
                    x: tx - 0.5
                    y: parent.parent.height - height
                    width: 1
                    height: index % 5 === 0 ? 4 : 2
                    color: root.tickColor
                }
            }
        }

        // The slot: thin and rounded.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: root.capWidth / 2 - 2
            width: parent.travel + 4
            height: 4
            radius: 2
            color: Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g,
                           ui.theme.fontPrimaryColor.b, 0.15)
        }
        // The value in it.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.fillBackground
            x: Math.min(parent.originX, parent.valueX)
            width: Math.abs(parent.valueX - parent.originX)
            height: 4
            radius: 2
            color: root.enabled ? ui.theme.accentColor : Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g,
                                                                ui.theme.fontPrimaryColor.b, 0.3)
        }
    }

    // GlideMix modern look: a glass capsule -- see-through, so the value
    // colour shows through it, with a light rim and a sheen on top.
    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + (root.availableHeight - height) / 2
        width: root.capWidth
        height: root.capHeight
        radius: height / 2
        // Stays clear glass while held (Chris): only the rim brightens.
        color: ui.theme.isDark
               ? Qt.rgba(1, 1, 1, (root.hovered || root.pressed) ? 0.20 : 0.16)
               : Qt.rgba(1, 1, 1, (root.hovered || root.pressed) ? 0.6 : 0.5)
        border.color: ui.theme.isDark ? Qt.rgba(1, 1, 1, root.pressed ? 0.8 : 0.55)
                                      : Qt.rgba(0, 0, 0, root.pressed ? 0.6 : 0.45)
        border.width: 1

        // the sheen on the top half
        Rectangle {
            x: 1
            y: 1
            width: parent.width - 2
            height: parent.height / 2 - 1
            radius: height / 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.45) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.05) }
            }
        }
    }
}
