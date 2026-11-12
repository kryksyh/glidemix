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
                text: "**GlideMix v1.2.8**\n\n"
                      + "- Samples tab: add your sample folders, find samples by category, pack, BPM and key, and preview loops at the tempo of your mix.\n"
                      + "- SAMPLES button on every song: opens a sample line right under it. Drag samples onto it: loops land on the bar and follow the song tempo, one-shots land where you drop them.\n"
                      + "- Select part of a song and right-click: Save as sample, Stems as samples (drums, bass, vocals, other or all four).\n"
                      + "- Right-click a selection > Stems: Remove drums, bass, vocals or other from just that part, or Split it into stem lanes.\n"
                      + "- Stem lanes are part of their song: the SONG counter no longer counts them.\n"
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
