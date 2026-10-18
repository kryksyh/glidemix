/*
 * Audacity: A Digital Audio Editor
 */

/*
 * Controllers preferences — SKELETON, NOT WIRED.
 *
 * Generated 1:1 from MixHouse DJ's own preference dialog (dlgprefcontrollersdlg.ui) by walking that
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

            title: qsTrc("preferences", "Controllers")

            navigation.section: root.navigationSection
            navigation.order: root.navigationOrderStart

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "Controllers are physical devices that send MIDI or HID signals to your computer over a USB connection. These allow you to control GlideMix in a more hands-on way than a keyboard and mouse. Attached controllers that GlideMix recognizes are shown in the \"Controllers\" section in the sidebar.")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 4
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "GlideMix did not detect any controllers. If you connected the controller while GlideMix was running, restart GlideMix first.")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 4
            }

            CheckBox {
                width: parent.width

                text: qsTrc("preferences", "Enable MIDI Through Port")
                checked: false

                navigation.name: "ControllersCheck0"
                navigation.panel: section0.navigation
                navigation.row: 0
            }
        }

        SeparatorLine {}

        BaseSection {
            id: section1

            title: qsTrc("preferences", "Mappings")

            navigation.section: root.navigationSection
            navigation.order: section0.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            FlatButton {
                text: qsTrc("preferences", "Open User Mapping Folder")

                navigation.name: "ControllersButton0"
                navigation.panel: section1.navigation
                navigation.row: 0
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "You can create your own mapping by using the MIDI Learning Wizard when you select your controller in the sidebar. You can edit mappings by selecting the \"Input Mappings\" and \"Output Mappings\" tabs in the preference page for your controller. See the Resources below for more details on making mappings.")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 4
            }
        }
    }
}
