/*
 * GlideMix
 *
 * A StyledSlider in the DJ-fader look (mock A, chosen by Chris 2026-10-28):
 * a dark slot with the value in the accent colour, ticks along it, and a
 * ridged silver cap with a dark centre line. (A dark cap, like the transition
 * window's VOL fader, vanished against the dark track headers.) Behaviour
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

    readonly property real capWidth: 12
    readonly property real capHeight: Math.max(12, Math.min(26, availableHeight))

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
                    color: "#6e727c"
                }
                Rectangle {
                    x: tx - 0.5
                    y: parent.parent.height - height
                    width: 1
                    height: index % 5 === 0 ? 4 : 2
                    color: "#6e727c"
                }
            }
        }

        // The slot.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: root.capWidth / 2 - 2
            width: parent.travel + 4
            height: 5
            radius: 1
            color: "#0e0f12"
            border.color: "#1f2126"
            border.width: 1
        }
        // The value in it.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.fillBackground
            x: Math.min(parent.originX, parent.valueX)
            width: Math.abs(parent.valueX - parent.originX)
            height: 3
            color: root.enabled ? ui.theme.accentColor : "#5a5d66"
        }
    }

    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + (root.availableHeight - height) / 2
        width: root.capWidth
        height: root.capHeight
        radius: 2
        color: root.pressed ? "#d4d6db" : (root.hovered ? "#c8cad0" : "#b4b7be")
        border.color: "#111214"
        border.width: 1

        // Lit from above: the top half a shade lighter.
        Rectangle {
            x: 1
            y: 1
            width: parent.width - 2
            height: parent.height / 2 - 1
            radius: 1
            color: "#ffffff"
            opacity: 0.18
        }

        // Ridges, then the centre line.
        Column {
            anchors.centerIn: parent
            spacing: Math.max(2, (parent.height - 10) / 6)
            Repeater {
                model: 4
                delegate: Rectangle {
                    width: root.capWidth - 4
                    height: 1
                    color: "#7d8089"
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: root.capWidth - 2
            height: 2
            color: "#16171a"
        }
    }
}
