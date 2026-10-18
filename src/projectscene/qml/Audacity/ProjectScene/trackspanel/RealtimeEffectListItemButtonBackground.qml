import QtQuick

import Muse.Ui
import Muse.UiComponents

Rectangle {
    id: root

    property var mouseArea: null
    property NavigationControl navigationCtrl: null
    property bool active: false
    property color activeColor: ui.theme.accentColor

    color: active ? activeColor : ui.theme.backgroundPrimaryColor
    radius: 3
    border.width: 1
    border.color: active ? activeColor : ui.theme.strokeColor

    NavigationFocusBorder {
        navigationCtrl: root.navigationCtrl
    }

    states: [
        State {
            name: "HOVERED"
            when: mouseArea.containsMouse && !mouseArea.pressed

            PropertyChanges {
                target: root
                color: root.active ? root.activeColor : ui.theme.buttonColor
                opacity: root.active ? 1.0 : 0.4
            }
        },
        State {
            name: "PRESSED"
            when: mouseArea.pressed

            PropertyChanges {
                target: root
                color: root.active ? root.activeColor : ui.theme.buttonColor
                opacity: root.active ? 0.8 : 0.7
            }
        }
    ]
}
