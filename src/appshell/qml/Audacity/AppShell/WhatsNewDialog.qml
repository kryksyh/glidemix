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
                text: "**GlideMix v1.2.9**\n\n"
                      + "- Beat: press BEAT on a sample line and build a drum loop from your own one-shots - 16 steps a bar, up to 16 bars, swing, a graph for velocity, pitch, pan and more. Beat starter fills it in 18 genres. Put on sample line renders it as one loop that follows the mix tempo; Edit beat opens it again.\n"
                      + "- The SAMPLES button holds the eye: hide a sample line completely, nothing left on screen.\n"
                      + "- Right-click a sample or song: Show similar sounds, Add compatible sound, View pack, Resample, Send to Beat, Create variations, Split into stems, Show in Finder, Export audio, Consolidate, Disable.\n"
                      + "- Library: every Lexicon column (remixer, label, danceability, popularity, happiness...) and the file's own dates and size on the drive.\n"
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
