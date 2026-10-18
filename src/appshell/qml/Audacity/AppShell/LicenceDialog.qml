/*
 * GlideMix: Help > Licence… -- the demo's limits, Buy, and the key to
 * unlock GlideMix (keys from polar.sh/glidemix). Also shown at start in
 * the demo version.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import Audacity.MixHouse

StyledDialogView {
    id: root

    title: qsTrc("appshell", "GlideMix licence")

    contentWidth: 520
    contentHeight: column.implicitHeight + 32

    MixHouseLicenceModel {
        id: licenceModel
    }

    ColumnLayout {
        id: column

        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.headerBoldFont
            text: licenceModel.isDemo ? qsTrc("appshell", "GlideMix demo version")
                                      : qsTrc("appshell", "%1 is active on this computer").arg(licenceModel.tierName)
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            visible: licenceModel.isDemo
            text: qsTrc("appshell", "Everything works, with three limits: every 2 minutes the sound goes quiet for a few seconds, "
                                   + "exports get a short noise in those gaps, and an export stops after 20 minutes. "
                                   + "A licence removes them for good.")
        }

        RowLayout {
            Layout.fillWidth: true
            visible: licenceModel.isDemo
            spacing: 12

            FlatButton {
                Layout.preferredWidth: 200
                text: qsTrc("appshell", "Buy GlideMix – €49")
                accentButton: true
                onClicked: licenceModel.buy(true)
            }
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                text: qsTrc("appshell", "Everything unlocked, stems included. One-time payment, 3 computers, all 1.x updates.")
            }
        }

        SeparatorLine { visible: licenceModel.isDemo }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            visible: licenceModel.isDemo
            text: qsTrc("appshell", "Already bought? Enter your licence key (it is in your order email):")
        }

        RowLayout {
            Layout.fillWidth: true
            visible: licenceModel.isDemo
            spacing: 8

            TextInputField {
                id: keyField
                Layout.fillWidth: true
                hint: "GLIDEMIX-…"
                onAccepted: licenceModel.activate(currentText)
                onTextChanged: function(newTextValue) { keyField.currentText = newTextValue }
            }

            FlatButton {
                text: qsTrc("appshell", "Activate")
                enabled: !licenceModel.busy && keyField.currentText.length > 0
                onClicked: licenceModel.activate(keyField.currentText)
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            visible: !licenceModel.isDemo
            text: qsTrc("appshell", "Licence key: %1").arg(licenceModel.key)
            elide: Text.ElideMiddle
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            visible: licenceModel.message.length > 0
            color: licenceModel.messageIsError ? "#F84C4C" : ui.theme.fontPrimaryColor
            text: licenceModel.message
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatButton {
                visible: !licenceModel.isDemo
                enabled: !licenceModel.busy
                text: qsTrc("appshell", "Deactivate this computer")
                onClicked: licenceModel.deactivate()
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: licenceModel.isDemo ? qsTrc("appshell", "Continue with the demo") : qsTrc("appshell", "Close")
                onClicked: root.hide()
            }
        }
    }
}
