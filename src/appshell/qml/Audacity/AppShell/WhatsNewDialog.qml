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
                text: "**GlideMix v1.1.26**\n\n"
                      + "- **Export is much faster.** A 16-minute mix now takes under 30 seconds on our Mac.\n"
                      + "- **Plug-ins:** fixed a bug that could slow the app down or stop it working.\n"
                      + "- **Stems:** fixed an install failure on some Macs.\n"
                      + "- **MP3 exports** no longer clip at normal bitrates.\n"
                      + "- **Starts cleaner:** you get to your music with fewer questions, and closing the window quits on the Mac too.\n"
                      + "- **Home** shows how many songs each project holds. The File menu says **Save project**.\n"
                      + "- **Transitions:** the pill shows the name, and one Undo fully reverses Mix Now.\n"
                      + "- **Library:** a dot marks songs already in the project, the list shows its song count and length, and hover the search box for its filters. Timeline songs get Show in Library.\n"
                      + "- **Backups:** the library now keeps its last four backups.\n"
                      + "- macOS may ask for permission one last time after this update.\n"
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
