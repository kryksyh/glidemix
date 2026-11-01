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
                text: "**GlideMix v1.1.22**\n\n"
                      + "**Big mixes stay usable**\n"
                      + "- With 30 songs or more, lanes, track headers and sliders no longer drift apart when you change the track view, zoom or scroll, and you keep your place while the lanes change height.\n"
                      + "- A new **Reset view** button in front of the zoom sliders puts the view back: lanes at their default height, the first songs readable.\n\n"
                      + "**Load a whole range from the library**\n"
                      + "- Click a song, Shift-click another, right-click and choose Load to > Project: they join the end of the mix in the same order, each one blended into the next, and the view shows the first of them.\n\n"
                      + "**The Project tab looks like the library**\n"
                      + "- Cover art, waveform, year, genre, key, energy, duration and rating for every song in your mix. Scroll sideways for more; the transition stays pinned on the right.\n\n"
                      + "**This window**\n"
                      + "- After every update GlideMix now tells you what changed."
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
