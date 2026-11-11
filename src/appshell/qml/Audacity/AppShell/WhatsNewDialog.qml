/*
 * GlideMix: shown once, on the first start after an update
 * (StartupScenario::showStartupDialogsIfNeed). Rewrite the notes for every
 * release, from the same text as the GitHub release.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

StyledDialogView {
    id: root

    title: qsTrc("appshell", "What's new in GlideMix")

    contentWidth: 560
    contentHeight: 460

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        StyledFlickable {
            Layout.fillWidth: true
            Layout.fillHeight: true

            contentHeight: notes.implicitHeight

            StyledTextLabel {
                id: notes

                width: parent.width
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                textFormat: Text.MarkdownText
                text: "**GlideMix v1.2.5**\n\n"
                      + "- Edit like in Ableton: Z zooms to the selection, X goes back, ⌘E splits, ⌘L loops, R reverses, ⌘1–⌘4 set the grid.\n"
                      + "- Some keys moved: Split is now ⌘E (was ⌘I), Record is F9, Rename is ⌘R.\n"
                      + "- Tidier right-click menus, with Cut / Paste / Delete time and a Grid section.\n"
                      + "- Thin automation lines with the value beside each point (dB, %, BPM).\n"
                      + "- Toolbar: icons only, grouped; hover a button to see what it does.\n"
                      + "- ⌘A selects all songs in the library, or the whole timeline.\n"
                      + "- Fixed a crash when zoomed in very close.\n"
            }
        }

        FlatButton {
            Layout.alignment: Qt.AlignRight
            Layout.preferredWidth: 100
            text: qsTrc("global", "OK")
            accentButton: true
            onClicked: root.close()
        }
    }
}
