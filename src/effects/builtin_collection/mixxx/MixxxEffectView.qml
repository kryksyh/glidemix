/*
 * GlideMix
 *
 * The view for every Mixxx effect: one knob per parameter (a switch for
 * on/off ones), with Mixxx's parameter names and knob curves.
 */
import QtQuick 2.15
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.Effects
import Audacity.BuiltinEffects
import Audacity.BuiltinEffectsCollection
import Audacity.ProjectScene

BuiltinEffectBase {
    id: root

    property string title: mixxxEffect.title
    property bool isApplyAllowed: true

    implicitWidth: Math.max(360, grid.implicitWidth + 2 * prv.padding)
    implicitHeight: grid.implicitHeight + 2 * prv.padding

    builtinEffectModel: MixxxEffectViewModelFactory.createModel(root, root.instanceId)
    numNavigationPanels: 1
    property alias mixxxEffect: root.builtinEffectModel

    property NavigationPanel knobsNavigationPanel: NavigationPanel {
        name: "MixxxEffectKnobs"
        enabled: root.enabled && root.visible
        direction: NavigationPanel.Horizontal
        section: root.dialogView ? root.dialogView.navigationSection : null
        order: 1
    }

    QtObject {
        id: prv

        readonly property int padding: 16
        readonly property int knobRadius: 24
        readonly property int cellWidth: 96
    }

    RoundedRectangle {
        anchors.fill: parent
        anchors.margins: prv.padding / 2

        color: ui.theme.backgroundSecondaryColor
        border.color: ui.theme.strokeColor
        border.width: 1
        radius: 4
    }

    GridLayout {
        id: grid

        anchors.centerIn: parent
        columns: Math.min(4, Math.max(1, repeater.count))
        columnSpacing: 16
        rowSpacing: 16

        Repeater {
            id: repeater

            model: mixxxEffect.parameterCount

            delegate: Column {
                required property int index
                property var modelData: mixxxEffect.parameter(index)

                Connections {
                    target: mixxxEffect
                    function onValuesChanged() {
                        modelData = mixxxEffect.parameter(index)
                    }
                }

                Layout.preferredWidth: prv.cellWidth
                spacing: 6

                StyledTextLabel {
                    width: prv.cellWidth
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: modelData.title
                }

                KnobControl {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !modelData.isToggle
                    // The meta knob stands out; knobs it moves are marked.
                    accentControl: modelData.isMeta || modelData.isLinked
                    radius: prv.knobRadius
                    from: 0
                    to: 1
                    stepSize: 0.001
                    value: modelData.normalized

                    navigation.panel: root.knobsNavigationPanel
                    navigation.order: modelData.index
                    navigation.accessible.name: modelData.title

                    onNewValueRequested: function (newValue) {
                        mixxxEffect.setNormalized(modelData.index, newValue)
                    }
                    mouseArea.onReleased: mixxxEffect.commitSettings()
                    mouseArea.onDoubleClicked: {
                        mixxxEffect.resetToDefault(modelData.index)
                        mixxxEffect.commitSettings()
                    }
                }

                FlatButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: modelData.isToggle
                    width: prv.knobRadius * 2 + 16
                    text: modelData.valueText
                    accentButton: modelData.normalized > 0.5
                    navigation.panel: root.knobsNavigationPanel
                    navigation.order: modelData.index
                    onClicked: {
                        mixxxEffect.setNormalized(modelData.index, modelData.normalized > 0.5 ? 0 : 1)
                        mixxxEffect.commitSettings()
                    }
                }

                StyledTextLabel {
                    width: prv.cellWidth
                    horizontalAlignment: Text.AlignHCenter
                    visible: !modelData.isToggle
                    text: modelData.valueText
                }
            }
        }
    }
}
