/*
 * GlideMix
 */
import QtQuick

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Preferences
import Audacity.UiComponents 1.0

BaseSection {
    id: root

    title: qsTrc("preferences", "Headphone cue")

    CueOutputPreferencesModel {
        id: cueModel
    }

    Component.onCompleted: cueModel.init()

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Qt.AlignLeft
        wrapMode: Text.WordWrap
        text: qsTrc("preferences", "While Perform is playing the mix to the room, Play is your headphone cue. This is where that cue goes.")
    }

    ComboBoxWithTitle {
        title: qsTrc("preferences", "Cue output:")

        columnWidth: 320

        currentIndex: cueModel.currentIndex
        model: cueModel.outputs

        navigation.name: "CueOutputBox"
        navigation.panel: root.navigation
        navigation.row: 0

        onValueEdited: function (newIndex, _) {
            cueModel.currentIndex = newIndex
        }
    }

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Qt.AlignLeft
        wrapMode: Text.WordWrap
        opacity: 0.7
        //! It is on the Perform button too, because mid-set is exactly when you
        //! want to move the cue without opening a dialog.
        text: qsTrc("preferences", "Outputs 3-4 need an interface with four or more outputs. You can also change this from the menu on the Perform button.")
    }
}
