/*
 * GlideMix
 */
/*
 * Stems: stem separation (Demucs) -- where it runs, which model, and the
 * cache of separated songs. Separate a track from its right-click menu.
 */
import QtQuick

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Preferences
import Audacity.UiComponents 1.0

PreferencesPage {
    id: root

    StemsPreferencesModel {
        id: stemsModel
    }

    Column {
        width: parent.width
        spacing: root.sectionsSpacing

        BaseSection {
            id: sectionAbout

            title: qsTrc("preferences", "Stem separation")

            navigation.section: root.navigationSection
            navigation.order: root.navigationOrderStart

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                text: stemsModel.available
                      ? qsTrc("preferences", "Splits a track into drums, bass, vocals and other, each on its own track under it, in sync with the original. Right-click a track and choose Separate stems.")
                      : qsTrc("preferences", "The stems engine is not installed yet. It is free and downloads once, about 1 GB, into GlideMix's own folder.")
            }

            Row {
                spacing: 8
                visible: !stemsModel.available || stemsModel.installing || stemsModel.engineInHelpers

                FlatButton {
                    visible: !stemsModel.available && !stemsModel.installing
                    accentButton: true
                    text: qsTrc("preferences", "Install stems engine")
                    onClicked: stemsModel.installEngine()
                }
                FlatButton {
                    visible: stemsModel.installing
                    text: qsTrc("global", "Cancel")
                    onClicked: stemsModel.cancelInstall()
                }
                FlatButton {
                    visible: stemsModel.available && stemsModel.engineInHelpers && !stemsModel.installing
                    text: qsTrc("preferences", "Uninstall stems engine")
                    onClicked: stemsModel.uninstallEngine()
                }
                StyledTextLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: stemsModel.installStatus !== ""
                    text: stemsModel.installStatus
                    opacity: 0.8
                }
            }
        }

        SeparatorLine {}

        BaseSection {
            id: sectionProcessing

            title: qsTrc("preferences", "Processing")

            navigation.section: root.navigationSection
            navigation.order: sectionAbout.navigationOrderEnd + 1

            ComboBoxWithTitle {
                title: qsTrc("preferences", "Run on:")
                columnWidth: 260

                currentIndex: stemsModel.device
                model: [
                    { text: qsTrc("preferences", "Automatic (GPU when available)"), value: 0 },
                    { text: qsTrc("preferences", "GPU (Metal)"), value: 1 },
                    { text: qsTrc("preferences", "CPU"), value: 2 }
                ]
                textRole: "text"
                valueRole: "value"

                navigation.name: "StemsDevice"
                navigation.panel: sectionProcessing.navigation
                navigation.row: 0

                onValueEdited: function(newIndex, newValue) {
                    stemsModel.device = newValue
                }
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTrc("preferences", "The GPU is many times faster. Automatic falls back to the CPU if the GPU cannot be used.")
            }

            StyledTextLabel {
                text: qsTrc("preferences", "Quality:")
            }

            RoundedRadioButton {
                text: qsTrc("preferences", "Standard (fast)")
                checked: !stemsModel.highQuality

                navigation.name: "StemsStandard"
                navigation.panel: sectionProcessing.navigation
                navigation.row: 1

                onToggled: stemsModel.highQuality = false
            }

            RoundedRadioButton {
                text: qsTrc("preferences", "High (cleaner, about 4 times slower)")
                checked: stemsModel.highQuality

                navigation.name: "StemsHigh"
                navigation.panel: sectionProcessing.navigation
                navigation.row: 2

                onToggled: stemsModel.highQuality = true
            }
        }

        SeparatorLine {}

        BaseSection {
            id: sectionCache

            title: qsTrc("preferences", "Stems cache")

            navigation.section: root.navigationSection
            navigation.order: sectionProcessing.navigationOrderEnd + 1

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                text: qsTrc("preferences", "Separated songs are kept, so separating one again is instant. Past the size limit the songs used longest ago are removed.")
            }

            ComboBoxWithTitle {
                title: qsTrc("preferences", "Cache size:")
                columnWidth: 260

                currentIndex: Math.max(0, indexOfValue(stemsModel.cacheLimitGb))
                model: [
                    { text: qsTrc("preferences", "2 GB"), value: 2 },
                    { text: qsTrc("preferences", "5 GB"), value: 5 },
                    { text: qsTrc("preferences", "10 GB"), value: 10 },
                    { text: qsTrc("preferences", "20 GB"), value: 20 },
                    { text: qsTrc("preferences", "50 GB"), value: 50 }
                ]
                textRole: "text"
                valueRole: "value"

                navigation.name: "StemsCacheSize"
                navigation.panel: sectionCache.navigation
                navigation.row: 0

                onValueEdited: function(newIndex, newValue) {
                    stemsModel.cacheLimitGb = newValue
                }
            }

            Row {
                spacing: 12

                FlatButton {
                    text: qsTrc("preferences", "Clear cache")

                    navigation.name: "StemsClearCache"
                    navigation.panel: sectionCache.navigation
                    navigation.row: 1

                    onClicked: stemsModel.clearCache()
                }

                StyledTextLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: 0.7
                    text: stemsModel.cacheUsed
                }
            }
        }
    }
}
