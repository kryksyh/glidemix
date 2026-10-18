/*
 * Audacity: A Digital Audio Editor
 */

/*
 * Waveforms preferences -- how clips are drawn on the timeline.
 *
 * WIRED: ArrangementPreferencesModel over IProjectSceneUiState, the lane look
 * saved with the workspace. View > Color waveform and View > Show beat grid
 * flip the same settings.
 *
 * This was the "Arrangement Mode" page, carried over from MixHouse DJ's
 * DlgPrefAbleMixStyle. Its other sections -- a right-click menu toggle, a beat
 * detection plugin picker, a default transition list, a plugin scan button --
 * were never connected to anything and duplicated real pages (Transitions,
 * Plugins), so they went. The Mixxx "Waveforms" and "Colors" pages that used
 * to sit beside it were unwired skeletons for deck waveforms this app does not
 * have; this is the one Waveforms page now.
 */
import QtQuick

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Preferences
import Audacity.UiComponents 1.0

PreferencesPage {
    id: root

    ArrangementPreferencesModel {
        id: arrangementModel
    }

    Column {
        width: parent.width
        spacing: root.sectionsSpacing

        BaseSection {
            id: waveformStyleSection

            title: qsTrc("preferences", "Waveform style")

            navigation.section: root.navigationSection
            navigation.order: root.navigationOrderStart

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "How clips are drawn on the arrangement timeline. These are saved with the workspace, so each workspace keeps its own look; View > Color waveform and View > Show beat grid flip the same settings.")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 3
            }

            ComboBoxWithTitle {
                title: qsTrc("preferences", "Waveform style:")

                columnWidth: 280
                currentIndex: arrangementModel.waveformStyle
                model: [
                    { text: qsTrc("preferences", "Classic — waveform in the clip's colour"), value: 0 },
                    { text: qsTrc("preferences", "DJ style — RGB spectral on black"), value: 1 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    arrangementModel.waveformStyle = newValue
                }

                navigation.name: "ArrModeWaveformStyleCombo"
                navigation.panel: waveformStyleSection.navigation
                navigation.row: 0
            }

            ComboBoxWithTitle {
                title: qsTrc("preferences", "Lane background:")

                columnWidth: 280
                currentIndex: arrangementModel.laneBackground
                model: [
                    { text: qsTrc("preferences", "Follow the waveform style"), value: 0 },
                    { text: qsTrc("preferences", "Black"), value: 1 },
                    { text: qsTrc("preferences", "Theme"), value: 2 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    arrangementModel.laneBackground = newValue
                }

                navigation.name: "ArrModeLaneBackgroundCombo"
                navigation.panel: waveformStyleSection.navigation
                navigation.row: 1
            }

            CheckBox {
                width: parent.width

                text: qsTrc("preferences", "Draw the bar and beat lines behind the tracks")
                checked: arrangementModel.beatGridVisible

                navigation.name: "ArrModeBeatGridCheck"
                navigation.panel: waveformStyleSection.navigation
                navigation.row: 2

                onClicked: {
                    arrangementModel.beatGridVisible = !checked
                }
            }

            CheckBox {
                width: parent.width

                text: qsTrc("preferences", "Stereo color waveform: left and right channels as two lanes")
                checked: arrangementModel.stereoWaveform
                enabled: arrangementModel.waveformStyle === 1

                navigation.name: "ArrModeStereoWaveformCheck"
                navigation.panel: waveformStyleSection.navigation
                navigation.row: 3

                onClicked: {
                    arrangementModel.stereoWaveform = !checked
                }
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTrc("preferences", "Each song is analysed once more, per channel, the first time it is shown. Most club tracks look the same on both sides; one lane is the DJ-app default.")
            }
        }
    }
}
