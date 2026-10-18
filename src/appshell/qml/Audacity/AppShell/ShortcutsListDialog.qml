/*
 * GlideMix: Help > Keyboard shortcuts -- every action that has a key, with a
 * search box. Editing happens in Preferences > Shortcuts, one click away.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import Muse.Shortcuts

StyledDialogView {
    id: root

    title: qsTrc("appshell", "Keyboard shortcuts")

    contentWidth: 760
    contentHeight: 620
    resizable: true

    ShortcutsModel {
        id: shortcutsModel
    }

    Component.onCompleted: shortcutsModel.load()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        SearchField {
            id: searchField

            Layout.fillWidth: true
            hint: qsTrc("appshell", "Search actions or keys")
        }

        StyledListView {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true

            // A-Z by action name; actions without a name (keyboard
            // navigation inside panels) are left out.
            model: SortFilterProxyModel {
                sourceModel: shortcutsModel
                sorters: [
                    SorterValue {
                        roleName: "title"
                        sortOrder: Qt.AscendingOrder
                        enabled: true
                    }
                ]
            }
            clip: true

            delegate: Item {
                id: row

                required property string title
                required property string sequence
                required property string searchKey

                readonly property bool shown: row.sequence.length > 0 && row.title.length > 0
                    && (searchField.searchText.length === 0
                        || row.searchKey.toLowerCase().indexOf(searchField.searchText.toLowerCase()) >= 0)

                // "Name — what it does": the name in bold, the rest below.
                readonly property int dash: row.title.indexOf(" \u2014 ")
                readonly property string name: dash >= 0 ? row.title.substring(0, dash) : row.title
                readonly property string what: dash >= 0 ? row.title.substring(dash + 3) : ""

                width: ListView.view.width
                height: shown ? texts.implicitHeight + 16 : 0
                visible: shown

                Column {
                    id: texts

                    anchors.left: parent.left
                    anchors.right: keyLabel.left
                    anchors.rightMargin: 24
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        font: ui.theme.bodyBoldFont
                        text: row.name
                    }

                    StyledTextLabel {
                        width: parent.width
                        visible: row.what.length > 0
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        opacity: 0.75
                        text: row.what
                    }
                }

                StyledTextLabel {
                    id: keyLabel

                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    font: ui.theme.bodyBoldFont
                    text: row.sequence
                }

                SeparatorLine {
                    anchors.bottom: parent.bottom
                    visible: row.shown
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("appshell", "Change any shortcut in Preferences > Shortcuts.")
            }

            FlatButton {
                text: qsTrc("appshell", "Edit shortcuts…")
                onClicked: {
                    root.ret = { errcode: 0, value: "edit" }
                    root.hide()
                }
            }

            FlatButton {
                text: qsTrc("global", "Close")
                accentButton: true
                onClicked: root.hide()
            }
        }
    }
}
