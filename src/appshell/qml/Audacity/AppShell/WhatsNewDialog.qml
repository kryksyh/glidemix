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
                text: "**GlideMix v1.2.6**\n\n"
                      + "- Sounds like: every song now gets a small sound fingerprint when it's analysed (its tone, brightness, bass and groove).\n"
                      + "- Related compares the song under the playhead with all the others: among the songs whose key and BPM mix, the ones that sound most alike come first. The new \"Sounds like\" column says how alike (85 % = more alike than 85 % of your library). Move the playhead to another song and the list follows.\n"
                      + "- Songs analysed before get their fingerprint once in the background (a few hours for a big library). It waits while music plays.\n"
                      + "- A light show above the clock: LED dots that dance with the mix. Right-click to hide it.\n"
                      + "- Audio setup and Music library moved to the top right, beside the Workspace picker.\n"
                      + "- Loops live under the ruler now: drag in the strip under the time numbers to loop, click the loop to turn it on or off. A click on the numbers just moves the playhead.\n"
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
