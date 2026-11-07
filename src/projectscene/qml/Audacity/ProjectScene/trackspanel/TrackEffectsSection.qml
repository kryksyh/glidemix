/*
 * Audacity: A Digital Audio Editor
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents

import Audacity.Effects
import Audacity.ProjectScene

Rectangle {
    id: root

    property alias isMasterTrack: effectList.isMasterTrack
    readonly property int minimumHeight: prv.headerHeight + prv.addEffectButtonHeight + 2 * prv.addEffectButtonMargin + 2

    property NavigationSection navigationSection: null
    property NavigationPanel navigationPanel: null
    property int navigationOrderStart: 0
    readonly property int navigationOrderEnd: navigationOrderStart + effectList.count + 1

    signal automationFocusRequested

    color: ui.theme.backgroundPrimaryColor

    // A click on unused space in the effects pane restores the last selected
    // effect parameter. Controls above this handler retain their own clicks.
    // focusFirstAutomatableParameter() is a safe no-op when a real focus
    // already exists (never overrides a parameter the user picked by hand)
    // -- it only bootstraps one the very first time, so a fresh session (or
    // an effect whose own controls never trigger the plugin-gesture
    // detection automationFocusRequested otherwise relies on) still has
    // something to show instead of silently doing nothing.
    MouseArea {
        anchors.fill: parent
        onPressed: {
            effectList.focusFirstAutomatableParameter()
            root.automationFocusRequested()
        }
    }

    Component.onCompleted: {
        menuModel.init()
    }

    QtObject {
        id: prv
        property bool enabled: effectList.trackName !== ""
        readonly property int addEffectButtonHeight: 24
        readonly property int addEffectButtonMargin: 12
        readonly property int headerHeight: 40
        readonly property int itemSpacing: 12
    }

    ColumnLayout {
        id: trackEffects
        z: 1
        spacing: 0
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right

        RowLayout {
            id: trackEffectsHeader

            spacing: prv.itemSpacing
            enabled: prv.enabled

            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVTop

            BypassEffectButton {
                id: trackEffectsPowerButton
                navigation.panel: root.navigationPanel
                navigation.order: root.navigationOrderStart
                navigation.name: "Toggle all track effects"
                navigation.accessible.name: root.isMasterTrack ? qsTrc("projectscene", "Toggle all master effects") : qsTrc("projectscene", "Toggle all effects")
                toolTipTitle: root.isMasterTrack ? qsTrc("projectscene", "Toggle all master effects") : qsTrc("projectscene", "Toggle all effects")

                Layout.alignment: Qt.AlignVCenter | Qt.AlignLeft
                Layout.margins: 8
                Layout.preferredWidth: prv.headerHeight - Layout.margins * 2
                Layout.preferredHeight: Layout.preferredWidth

                accentButton: effectList.trackEffectsActive
                isMasterEffect: root.isMasterTrack

                onClicked: {
                    accentButton = !accentButton
                    effectList.trackEffectsActive = accentButton
                }
            }

            StyledTextLabel {
                id: trackNameLabel
                enabled: prv.enabled
                text: prv.enabled ? effectList.trackName : ""
                Layout.fillWidth: true
                Layout.preferredHeight: prv.headerHeight
                Layout.maximumWidth: root.width - trackEffectsPowerButton.width - 2 * prv.itemSpacing
                horizontalAlignment: Text.AlignLeft
            }
        }

        SeparatorLine {
            id: separator
            enabled: prv.enabled
        }

        Rectangle {
            id: effectListContainer
            visible: prv.enabled
            color: ui.theme.backgroundTertiaryColor
            Layout.preferredHeight: effectList.empty ? 0 : Math.max(0, Math.min(effectList.implicitHeight, root.height - prv.addEffectButtonHeight - 2 * prv.addEffectButtonMargin - prv.headerHeight - 2 * separator.height))
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVTop
            Layout.fillWidth: true
            TrackEffectList {
                id: effectList
                navigationPanel: root.navigationPanel
                navigationOrderStart: trackEffectsPowerButton.navigation.order + 1
                color: "transparent"
                anchors.fill: parent
                anchors.leftMargin: 4
                onTrackEffectsActiveChanged: {
                    trackEffectsPowerButton.accentButton = trackEffectsActive
                }
            }
        }

        SeparatorLine {
            visible: prv.enabled && !effectList.empty
        }

        FlatButton {
            id: addEffectButton
            // The selected track is already the source of truth for the
            // realtime-effect service.  Keeping this button gated by the
            // asynchronously refreshed helper property made it appear
            // enabled while silently dropping the click when the selection
            // model had not caught up yet.
            enabled: prv.enabled
            Layout.fillWidth: true
            Layout.minimumHeight: prv.addEffectButtonHeight
            Layout.maximumHeight: prv.addEffectButtonHeight
            Layout.margins: prv.addEffectButtonMargin
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVTop

            navigation.panel: root.navigationPanel
            navigation.order: root.navigationOrderEnd
            navigation.name: "Add effect"
            navigation.accessible.name: root.isMasterTrack ? qsTrc("projectscene", "Add master effect") : qsTrc("projectscene", "Add effect")

            text: qsTrc("projectscene", "Add effect")

            AddEffectMenuModel {
                id: menuModel
                isMasterTrack: effectList.isMasterTrack
            }

            onClicked: function () {
                menuModel.validatePendingPlugins()
                menuModel.load()
                effectMenuLoader.toggleOpened(menuModel)
            }

            StyledMenuLoader {
                id: effectMenuLoader

                // Match ArrowMenuButton/MenuButton behavior: the root item
                // gives PopupView the full window height for menu sizing and
                // placement. Anchoring to the 24px button constrained the
                // effect menu to the button itself.
                menuAnchorItem: ui.rootItem

                onHandleMenuItem: function (itemId) {
                    menuModel.handleMenuItem(itemId)
                }
            }
        }

        StyledTextLabel {
            id: infoText
            visible: !isMasterTrack && effectList.empty
            // GlideMix: no "Watch video" link (Audacity's tutorial).
            text: qsTrc("projectscene", "Realtime effects are non-destructive and can be changed at any time.")

            Layout.fillWidth: true
            Layout.fillHeight: true
            wrapMode: Text.Wrap
            font: ui.theme.bodyFont
            color: ui.theme.fontPrimaryColor
            leftPadding: 16
            rightPadding: 16
            elide: Text.ElideNone
            verticalAlignment: Text.AlignTop
            horizontalAlignment: Text.AlignLeft
        }
    }
}
