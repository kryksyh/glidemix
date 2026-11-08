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
                text: "**GlideMix v1.1.29**\n\n"
                      + "- **Waveforms like the Beatgrid editor:** the timeline and the library now draw every song with the same colours and shape as the Beatgrid editor (pink and red kicks, blue hi-hats). Each song's picture is redone once, in the background, the first time it is shown.\n"
                      + "- **Your DJ apps on every drive:** Engine DJ, Serato and VirtualDJ libraries on an SD card or USB drive are read too, and a drive plugged in later shows up by itself.\n"
                      + "- **Preferences > Library > Other DJ Apps:** show or hide each DJ app, and add a folder if an app keeps its library somewhere else.\n"
                      + "- **Stems:** a song split into stems has one transition pill; every change on it reaches the song and all its stems, one Undo.\n"
                      + "- **Low, Mid, High and Color lines** from the track header glow in their colour, like the transition pill's lines.\n"
                      + "- **Small fixes:** an empty library says how to add music; picking a key no longer moves the sidebar; on Windows, stems always run on the processor instead of failing.\n"
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
