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
                text: "**GlideMix v1.2.3: no more freezes, plus a few wishes**\n\n"
                      + "- **No more long freezes** after editing a beat grid, adding songs or starting up, with a big library and many smart playlists. The smart playlist counts are worked out in the background now.\n"
                      + "- **Set IN and OUT anchors from the keyboard:** Alt+I and Alt+O put them at the playhead on the selected song (change the keys in Preferences > Shortcuts).\n"
                      + "- **Rename a project** from its … menu on the Home page.\n"
                      + "- **The Related tab's search box works:** type a name, genre or BPM to narrow the related songs.\n"
                      + "- **Windows:** Task Manager shows the real version, not \"GlideMix 1.1 beta dev\".\n"
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
