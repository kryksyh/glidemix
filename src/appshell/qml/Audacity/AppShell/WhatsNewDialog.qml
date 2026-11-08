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
                text: "**GlideMix v1.1.28**\n\n"
                      + "- **Ableton Link:** the LINK button shares your mix's tempo, beat and play/stop with Resolume, TouchDesigner and other Link apps.\n"
                      + "- **Your other DJ apps:** Engine DJ, Traktor, Serato, VirtualDJ and djay show in the library, and their beat grids and keys are used instead of analysing again.\n"
                      + "- **Mixed In Key:** key and energy are read from the comment. Comments are kept, and Edit Tags has Key, Energy and Rating.\n"
                      + "- **Low, Mid, High and Color** buttons in the track header show each line over the whole song.\n"
                      + "- **Library:** scroll past 2,000 songs, counts on smart playlists, Duplicates and Analyze, Back after Show in Library, save a search as a smart playlist, columns for any file tag (Mood, Label…), arrow keys in the song list, a preview volume.\n"
                      + "- **Playlists:** right-click > Export as M3U8 Playlist.\n"
                      + "- **Automation:** right-click a point > Set value… (Set tempo… on the tempo line). Shift-click several points to move them together.\n"
                      + "- **Small fixes:** the key shows in the clip header, a library scan no longer makes GlideMix stall, the + tab menu no longer overlaps.\n"
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
