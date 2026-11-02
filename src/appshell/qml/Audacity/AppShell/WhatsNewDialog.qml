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
                text: "**GlideMix v1.1.25**\n\n"
                      + "- **Tempo line:** BPM markers at every transition, so you can see the tempo without scrolling back. Points and markers follow your theme color.\n"
                      + "- **Reset** on the tempo line now returns every track to its real BPM, and the Project tab shows the tempo each song really plays at.\n"
                      + "- **Import From File Tags** works: right-click songs > Metadata. Changed a tag in another app? Pull it into your library.\n"
                      + "- **ReplayGain:** tags are read when songs are added, and Import From File Tags fills in songs already in your library.\n"
                      + "- **Show in Library:** right-click a Project tab song to find the track, its album or its artist. Playlist songs get Show in Library too.\n"
                      + "- **Search related Tracks:** no more stuck filters. The search shows on the key wheel or in the search box, and one click clears it.\n"
                      + "- **Drag preview:** dragging a song onto the timeline now shows artist and title.\n"
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
