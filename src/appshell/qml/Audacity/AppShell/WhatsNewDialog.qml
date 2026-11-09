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
                text: "**GlideMix v1.2.1: your own toolbar**\n\n"
                      + "- **Customize the toolbar** like Safari: right-click the toolbar (or its gear) and drag buttons in, out and along it; Space and Flexible Space; show icons, text or both (new GlideMix icons). The default set puts it back.\n"
                      + "- **Key column your way:** plain text, coloured text or coloured badges (Preferences > Music library).\n"
                      + "- **Tooltips show their text again**, and submenus open as you point at them.\n"
                      + "- **Transition dots:** the dot at the clip's edge moves the line's start or end, no more extra dots.\n"
                      + "- **Track headers line up** with their rows in the timeline again.\n"
                      + "- **No more flashing** in the library while songs are analysed.\n"
                      + "- **Buttons are pills** everywhere, Color > Intensity works from the track header too.\n"
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
