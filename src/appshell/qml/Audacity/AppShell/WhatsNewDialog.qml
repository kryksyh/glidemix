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
                text: "**GlideMix v1.2.0: a new look**\n\n"
                      + "- **Modern look:** rounded buttons, menus and panels; buttons are pills, purple only when on.\n"
                      + "- **Display at the top:** time, key, energy, song 2 of 4, processor and disk use, and a play light.\n"
                      + "- **Toolbar in groups**, each in its own capsule.\n"
                      + "- **New knobs and faders:** a thin coloured arc on each knob (hover to see the value), see-through glass caps on the faders.\n"
                      + "- **See-through menus** with the window behind them softly blurred (Mac, Windows 11).\n"
                      + "- **Library buttons on top** in one row with the Key Wheel; the search sits below.\n"
                      + "- **Light theme in GlideMix purple.**\n"
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
