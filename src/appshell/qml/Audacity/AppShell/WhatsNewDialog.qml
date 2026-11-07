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
                text: "**GlideMix v1.1.27**\n\n"
                      + "- **Adding a song no longer resets your transitions** or moves your songs: only the new song moves.\n"
                      + "- **No more sudden quits** when the project can't be backed up: GlideMix warns you instead.\n"
                      + "- **Volume and automation lines:** easier to grab, no clusters of dots, press and drag adds a point. All lines work like the volume line.\n"
                      + "- **Library:** click a song's waveform to preview it, in time with your mix while it plays.\n"
                      + "- **Analysis** continues by itself after GlideMix closes or stops, and skips a file that stopped it twice.\n"
                      + "- **Preferences > Waveforms:** mouse wheel zoom, and the phrase ribbon on or off.\n"
                      + "- **Space** plays the song in the Beat grid editor. Your own transitions come first in the menus.\n"
                      + "- **A +** beside the bottom tabs opens a closed tab again. The vertical ruler shows the volume in dB.\n"
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
