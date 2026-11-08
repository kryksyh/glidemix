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
                text: "**GlideMix v1.1.32: a faster library**\n\n"
                      + "- **Every song, at once:** the song list holds your whole collection, not 2000 songs at a time. Scrolling to the end, sorting and searching no longer stall, even with 40,000 songs. The same for rekordbox XML, Lexicon and your other DJ apps.\n"
                      + "- **Select All means all:** Analyze and the other actions now reach every song in the list.\n"
                      + "- **Analyze:** songs never analysed go first, and the Analyze count goes down as they finish.\n"
                      + "- **Import from Rekordbox XML** shows a small window while it reads, and what it found.\n"
                      + "- Quitting while GlideMix was still reading your DJ apps could crash it on the Mac. Fixed.\n"
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
