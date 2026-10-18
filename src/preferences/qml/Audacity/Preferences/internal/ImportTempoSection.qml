/*
 * GlideMix
 *
 * Tempo detection for audio files dropped on the timeline that are not in the
 * music library. Library tracks never go through it: they already have an
 * analysed BPM and beat grid, which the drop applies instead. This used to be
 * a page of its own called "Music"; it lives with the library now, because it
 * is the other half of the same question -- where a track's tempo comes from.
 */
import QtQuick

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Preferences
import Audacity.UiComponents 1.0

BaseSection {
    id: root

    title: qsTrc("preferences", "Audio files that aren't in your library")

    navigation.direction: NavigationPanel.Both
    navigationOrderEnd: workspaceSection.navigation.order

    rowSpacing: 16

    MusicPreferencesModel {
        id: musicPreferencesModel
    }

    Component.onCompleted: musicPreferencesModel.init()

    StyledTextLabel {
        width: parent.width
        horizontalAlignment: Qt.AlignLeft
        wrapMode: Text.WordWrap
        text: qsTrc("preferences", "Library tracks always use their analysed BPM and beat grid. These settings are only for other audio files you drop on the timeline, which have no tempo until one is detected.")
    }

    StyledTextLabel {
        text: qsTrc("preferences", "Detect their tempo:")
    }

    RadioButtonGroup {
        width: parent.width
        //! Sized from what is in it, not a guessed constant: the old fixed
        //! height left the checkbox below sitting on top of "Never".
        height: radioColumn.implicitHeight

        spacing: root.rowSpacing
        orientation: Qt.Vertical

        Column {
            id: radioColumn

            width: parent.width
            spacing: root.columnSpacing

            RoundedRadioButton {
                checked: musicPreferencesModel.tempoDetectionPref === TempoDetection.ALWAYS
                text: qsTrc("preferences", "Always")

                navigation.name: "TempoDetectionAlwaysRadioBtn"
                navigation.panel: root.navigation
                navigation.row: 0

                onToggled: {
                    musicPreferencesModel.setTempoDetectionPref(TempoDetection.ALWAYS)
                }
            }

            RoundedRadioButton {
                id: workspaceRadioBtn

                checked: musicPreferencesModel.tempoDetectionPref === TempoDetection.WORKSPACE_DEPENDENT
                text: qsTrc("preferences", "Depending on workspace")

                navigation.name: "TempoDetectionWorkspaceDependentRadioBtn"
                navigation.panel: root.navigation
                navigation.row: 1

                onToggled: {
                    musicPreferencesModel.setTempoDetectionPref(TempoDetection.WORKSPACE_DEPENDENT)
                }
            }

            WorkspacesTempoDetectionSection {
                id: workspaceSection

                x: 30

                visible: workspaceRadioBtn.checked

                musicPreferencesModel: musicPreferencesModel

                navigation.section: root.navigation.section
                navigation.order: root.navigation.order + 1
            }

            RoundedRadioButton {
                checked: musicPreferencesModel.tempoDetectionPref === TempoDetection.NEVER
                text: qsTrc("preferences", "Never")

                navigation.name: "TempoDetectionNeverRadioBtn"
                navigation.panel: root.navigation
                navigation.row: 3

                onToggled: {
                    musicPreferencesModel.setTempoDetectionPref(TempoDetection.NEVER)
                }
            }
        }
    }

    //! Off means the detected tempo is applied without asking: the file is
    //! stretched to the project tempo straight away.
    CheckBox {
        width: parent.width

        text: qsTrc("preferences", "Ask before stretching it to the project tempo")
        checked: musicPreferencesModel.askBeforeSubsequentImport

        navigation.name: "AskBeforeSubsequentImportCheckBox"
        navigation.panel: root.navigation
        navigation.row: 4

        onClicked: {
            musicPreferencesModel.askBeforeSubsequentImport = !checked
        }
    }
}
