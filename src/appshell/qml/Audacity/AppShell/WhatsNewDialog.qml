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
                text: "**GlideMix v1.1.33: Match, Related and Charcoal**\n\n"
                      + "- **Match:** a new button in the library. The Key cell turns green for songs that mix with the song under the playhead.\n"
                      + "- **Related:** a tab with the songs that go with the song under the playhead, every column there.\n"
                      + "- **The gear next to Match** sets the rules for both: BPM, key, energy, year, history, artist, genre, label, rating stars, tags and more.\n"
                      + "- **Charcoal:** a darker look. A dark Mac or PC gets it by itself; the old dark theme is now called Grey (Preferences > Appearance).\n"
                      + "- **Sorting:** a third click on a column header goes back to the list's own order.\n"
                      + "- **New projects open on the Music library tab.**\n"
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
