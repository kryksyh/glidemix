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
                text: "**GlideMix v1.2.7**\n\n"
                      + "- Volume Fade is the new default transition (the MixMeister fade): the playing song slides to half volume over 8 bars after its OUT and cuts, the next one starts on its IN at half volume and rises to full.\n"
                      + "- Move an OUT or IN anchor and the songs after it move along; the fade is redrawn from the new spot.\n"
                      + "- Right-click an IN or OUT flag to switch between Volume Fade and the GlideMix transition, or turn it off.\n"
                      + "- Before the IN and after the cut the song now shows faintly, so you see where you are when you move the IN.\n"
                      + "- Sounds like is a column in the main library now: sort by it to find the songs that sound most like the one under the playhead.\n"
                      + "- Del / Backspace delete a song every time and take its empty lane with it; new lanes come in at the same height.\n"
                      + "- Picked a transition by hand before? It stays: choose Volume Fade in Preferences > Transitions.\n"
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
