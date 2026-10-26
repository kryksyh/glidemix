/*
 * GlideMix
 *
 * A DJM-style rotary selector: a knob with its choices printed around it.
 * Click a name, or drag / scroll the knob to step. Choices GlideMix does not
 * have yet ({ enabled: false }) are printed dim and are skipped.
 *
 * choices: [{ id, title, enabled }]
 */
import QtQuick

Item {
    id: root

    property var choices: []
    property string current: ""
    property color accent: "#3fa9ff"
    property int knobSize: 46
    // The arc the names sit on, in degrees from the top (clockwise).
    property real fromAngle: -140
    property real toAngle: 140
    property int labelRadius: knobSize / 2 + 16
    // Names sit on an ellipse, wider than tall, so long ones do not overlap.
    property int labelRadiusX: labelRadius
    property int labelRadiusY: labelRadius
    property bool interactive: true
    // Room kept beside the ellipse for the longest name.
    property int labelPad: 34

    signal chosen(string id)

    implicitWidth: (labelRadiusX + labelPad) * 2
    implicitHeight: labelRadiusY + Math.max(knobSize / 2, -Math.cos(fromAngle * Math.PI / 180) * labelRadiusY) + 22

    readonly property int index: {
        for (var i = 0; i < choices.length; ++i) {
            if (choices[i].id === current) return i
        }
        return 0
    }
    function angleOf(i) {
        return choices.length < 2 ? 0 : fromAngle + i * (toAngle - fromAngle) / (choices.length - 1)
    }
    function step(d) {
        for (var i = index + d; i >= 0 && i < choices.length; i += d) {
            if (choices[i].enabled !== false) {
                chosen(choices[i].id)
                return
            }
        }
    }

    readonly property real cx: width / 2
    readonly property real cy: labelRadiusY + 10

    Repeater {
        model: root.choices
        delegate: Text {
            required property var modelData
            required property int index
            readonly property real a: root.angleOf(index) * Math.PI / 180
            readonly property bool on: modelData.id === root.current
            x: root.cx + Math.sin(a) * root.labelRadiusX - width / 2
            y: root.cy - Math.cos(a) * root.labelRadiusY - height / 2
            text: modelData.title
            font.pixelSize: 8
            font.bold: true
            color: on ? root.accent : (modelData.enabled === false ? "#55534e" : "#c9c6be")
            MouseArea {
                anchors.fill: parent
                anchors.margins: -2
                enabled: root.interactive && modelData.enabled !== false
                cursorShape: Qt.PointingHandCursor
                onClicked: root.chosen(modelData.id)
            }
        }
    }

    DjKnob {
        x: root.cx - width / 2
        y: root.cy - root.knobSize / 2
        size: root.knobSize
        big: true
        accent: "transparent"
        enabled: root.interactive
        // The pointer points at the chosen name: map its angle onto the knob's -135..135.
        value: (Math.max(-135, Math.min(135, root.angleOf(root.index))) + 135) / 270
        property real acc: 0
        onGrabbed: function(down) { acc = 0 }
        onMoved: function(v) {
            // A drag of about a sixth of the travel steps once.
            var d = v - value
            if (Math.abs(d) > 0.06) {
                root.step(d > 0 ? 1 : -1)
                local = value
            }
        }
    }
}
