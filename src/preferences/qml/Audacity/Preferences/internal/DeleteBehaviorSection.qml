/*
 * Audacity: A Digital Audio Editor
 */
import QtQuick 2.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.AppShell
import Audacity.UiComponents 1.0
import Audacity.TrackEdit 1.0

BaseSection {
    id: root

    title: deleteBehaviorPanel.title

    navigationOrderEnd: root.navigation.order

    required property var editPreferencesModel
    property alias parentBackgroundColor: deleteBehaviorPanel.parentBackgroundColor

    DeleteBehaviorPanel {
        id: deleteBehaviorPanel

        width: parent.width

        navigation: root.navigation

        deleteBehavior: editPreferencesModel.deleteBehavior
        closeGapBehavior: editPreferencesModel.closeGapBehavior

        onNewDeleteBehaviorRequested: function (deleteBehavior) {
            editPreferencesModel.setDeleteBehavior(deleteBehavior)
        }

        onNewCloseGapBehaviorRequested: function (closeGapBehavior) {
            editPreferencesModel.setCloseGapBehavior(closeGapBehavior)
        }
    }

    //! Until the choice has been made, neither button is on -- which reads as
    //! a broken control rather than a question nobody has asked yet. The panel
    //! itself is shared with the dialog that does the asking, so the
    //! explanation belongs here, on the preferences page, and only while it is
    //! still true.
    StyledTextLabel {
        width: parent.width
        visible: editPreferencesModel.deleteBehavior < 0

        horizontalAlignment: Qt.AlignLeft
        wrapMode: Text.WordWrap
        opacity: 0.7
        text: qsTrc("preferences", "Not set yet. The app asks the first time you delete part of a clip, and remembers the answer here. You can pick one now instead.")
    }
}
