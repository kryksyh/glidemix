/*
 * Audacity: A Digital Audio Editor
 */

/*
 * Controller mapping preferences — SKELETON, NOT WIRED.
 *
 * Generated 1:1 from MixHouse DJ's own preference dialog (dlgprefcontrollerdlg.ui) by walking that
 * .ui file in document order, so the control list here matches the real page
 * rather than a hand-picked subset. Combo options come from the harvested
 * addItem() calls in the matching .cpp where they are built in code.
 *
 * Nothing is read or persisted yet; there is no model behind this page. The chrome
 * is Audacity's own — do not restyle it.
 */
import QtQuick

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Preferences
import Audacity.UiComponents 1.0

PreferencesPage {
    id: root

    Column {
        width: parent.width
        spacing: root.sectionsSpacing

        BaseSection {
            id: section0

            title: qsTrc("preferences", "Controller mapping")

            navigation.section: root.navigationSection
            navigation.order: root.navigationOrderStart

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            FlatButton {
                text: qsTrc("preferences", "Learning Wizard (MIDI Only)")

                navigation.name: "CtrlMapButton0"
                navigation.panel: section0.navigation
                navigation.row: 0
            }

            CheckBox {
                width: parent.width

                text: qsTrc("preferences", "Enabled")
                checked: false

                navigation.name: "CtrlMapCheck1"
                navigation.panel: section0.navigation
                navigation.row: 1
            }

            ComboBoxWithTitle {
                title: qsTrc("preferences", "Controller Name:")

                columnWidth: 208
                currentIndex: 0
                model: [
                    { text: qsTrc("preferences", "(populated at runtime)"), value: 0 }
                ]
                textRole: "text"
                valueRole: "value"

                navigation.name: "CtrlMapCombo2"
                navigation.panel: section0.navigation
                navigation.row: 2
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "(mapping description goes here) even when it is a damn long text which should wrap but does not")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 4
            }

            FlatButton {
                text: qsTrc("preferences", "Search")

                navigation.name: "CtrlMapButton3"
                navigation.panel: section0.navigation
                navigation.row: 3
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "This list needs a real data model and is not built yet.")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 4
            }

            FlatButton {
                text: qsTrc("preferences", "Add")

                navigation.name: "CtrlMapButton4"
                navigation.panel: section0.navigation
                navigation.row: 4
            }

            FlatButton {
                text: qsTrc("preferences", "Remove")

                navigation.name: "CtrlMapButton5"
                navigation.panel: section0.navigation
                navigation.row: 5
            }

            FlatButton {
                text: qsTrc("preferences", "Clear All")

                navigation.name: "CtrlMapButton6"
                navigation.panel: section0.navigation
                navigation.row: 6
            }

            FlatButton {
                text: qsTrc("preferences", "Search")

                navigation.name: "CtrlMapButton7"
                navigation.panel: section0.navigation
                navigation.row: 7
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "This list needs a real data model and is not built yet.")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 4
            }

            FlatButton {
                text: qsTrc("preferences", "Add")

                navigation.name: "CtrlMapButton8"
                navigation.panel: section0.navigation
                navigation.row: 8
            }

            FlatButton {
                text: qsTrc("preferences", "Remove")

                navigation.name: "CtrlMapButton9"
                navigation.panel: section0.navigation
                navigation.row: 9
            }

            FlatButton {
                text: qsTrc("preferences", "Clear All")

                navigation.name: "CtrlMapButton10"
                navigation.panel: section0.navigation
                navigation.row: 10
            }
        }
    }
}
