/*
 * Audacity: A Digital Audio Editor
 */

/*
 * Effects preferences, after Mixxx's DlgPrefEffects (dlgprefeffectsdlg.ui).
 *
 * WIRED: Visible Effects -- the Visible / Hidden lists order and filter the
 * Add effect > DJ Effects menu (DjEffectsPreferencesModel, settings
 * effects/djVisibleEffects + djHiddenEffects; taken over from Mixxx's
 * effects.xml once). Drag and drop between and within the lists, or use
 * the arrow buttons.
 *
 * WIRED: Effect Chain Presets -- DjChainPresetsPreferencesModel over
 * IEffectChainPresets (Mixxx's chain preset XML files, taken over from
 * Mixxx once): drag to reorder, Import, Export, Rename, Delete.
 *
 * WIRED: Effect load behavior -- effects/djKeepMetaknobPosition, used by
 * RealtimeEffectService::replaceRealtimeEffect.
 *
 * WIRED: DJ EQ crossover frequencies -- DjMixerPreferencesModel
 * (effects/djLowEqFrequency, djHighEqFrequency; taken over from Mixxx's
 * [Mixer Profile] once), the crossovers of the DJ EQ effects (Bessel,
 * LinkwitzRiley, Biquad isolators and EQs), Mixxx's x^4 slider curve. Moved
 * here from the Mixer page, which was otherwise all Mixxx deck settings.
 *
 * Below them, what was the Plugins page (PluginPreferencesModel): the Add
 * effect menu's layout and the custom VST3 (LV2 on Linux) plugin folders.
 *
 * The chrome is Audacity's own — do not restyle it.
 */
import QtQuick
import QtQuick.Controls 2.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Preferences
import Audacity.UiComponents 1.0

import "internal"

PreferencesPage {
    id: root

    DjEffectsPreferencesModel {
        id: djEffectsModel
    }

    DjChainPresetsPreferencesModel {
        id: chainPresetsModel
    }

    DjMixerPreferencesModel {
        id: djMixerModel
    }

    PluginPreferencesModel {
        id: pluginModel
    }

    Component.onCompleted: {
        djEffectsModel.load()
        djMixerModel.load()
        chainPresetsModel.load()
        pluginModel.init()
    }

    Column {
        id: sectionsColumn

        width: parent.width
        spacing: root.sectionsSpacing

        BaseSection {
            id: section0

            title: qsTrc("preferences", "Effect Chain Presets")

            navigation.section: root.navigationSection
            navigation.order: root.navigationOrderStart

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "Drag and drop to rearrange the list. Save a track's effects as a chain preset from Add effect > Chain presets, and load a preset onto a track from the same menu.")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 4
            }

            Item {
                id: chainTables

                property string selectedName: ""
                property string dragName: ""
                property int dropIndex: -1

                readonly property int rowHeight: 24
                readonly property real listWidth: (width - 24) / 2
                readonly property var selectedEffects: selectedName !== "" ? chainPresetsModel.effectsIn(selectedName) : []

                width: parent.width
                height: 300 + 8 + 2 * 28 + 8

                Connections {
                    target: chainPresetsModel
                    function onPresetsChanged() {
                        // A deleted preset leaves nothing selected, so a
                        // second Delete can't hit another preset.
                        if (chainPresetsModel.presets.indexOf(chainTables.selectedName) < 0) {
                            chainTables.selectedName = ""
                        }
                        // Effects of the (possibly renamed) selection.
                        chainTables.selectedNameChanged()
                    }
                }

                Rectangle {
                    y: 0
                    width: chainTables.listWidth
                    height: 300
                    color: "transparent"
                    border.color: ui.theme.strokeColor
                    border.width: 1
                    radius: 3

                    StyledListView {
                        id: chainList

                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        ScrollBar.vertical: StyledScrollBar {}
                        model: chainPresetsModel.presets

                        delegate: ListItemBlank {
                            id: chainRow

                            required property string modelData
                            required property int index

                            width: chainList.width
                            height: chainTables.rowHeight
                            isSelected: modelData === chainTables.selectedName

                            StyledTextLabel {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                horizontalAlignment: Qt.AlignLeft
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                text: chainRow.modelData
                            }

                            Rectangle {
                                width: parent.width
                                height: 2
                                y: chainTables.dropIndex === chainList.count && chainRow.index === chainList.count - 1 ? parent.height - 1 : -1
                                color: ui.theme.accentColor
                                visible: chainTables.dragName !== ""
                                         && (chainTables.dropIndex === chainRow.index
                                             || (chainTables.dropIndex === chainList.count && chainRow.index === chainList.count - 1))
                            }

                            MouseArea {
                                property point pressPoint

                                anchors.fill: parent
                                preventStealing: true

                                onPressed: function(mouse) {
                                    pressPoint = Qt.point(mouse.x, mouse.y)
                                    chainTables.selectedName = chainRow.modelData
                                }

                                onPositionChanged: function(mouse) {
                                    if (chainTables.dragName === "") {
                                        if (Math.abs(mouse.y - pressPoint.y) < 6) {
                                            return
                                        }
                                        chainTables.dragName = chainRow.modelData
                                    }
                                    const p = mapToItem(chainList, mouse.x, mouse.y)
                                    chainTables.dropIndex = Math.max(0, Math.min(chainList.count,
                                                                                 Math.round((p.y + chainList.contentY) / chainTables.rowHeight)))
                                }

                                onReleased: {
                                    // Clear the drag first: the move rebuilds the list.
                                    const name = chainTables.dragName
                                    const index = chainTables.dropIndex
                                    chainTables.dragName = ""
                                    chainTables.dropIndex = -1
                                    if (name !== "" && index >= 0) {
                                        chainPresetsModel.movePreset(name, index)
                                    }
                                }

                                onCanceled: {
                                    chainTables.dragName = ""
                                    chainTables.dropIndex = -1
                                }
                            }
                        }
                    }

                    StyledTextLabel {
                        anchors.centerIn: parent
                        visible: chainList.count === 0
                        opacity: 0.6
                        text: qsTrc("preferences", "No chain presets")
                    }
                }

                Column {
                    x: chainTables.listWidth + 24
                    y: 0
                    width: chainTables.listWidth
                    spacing: 6

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Qt.AlignLeft
                        text: qsTrc("preferences", "Effects in this chain preset:")
                    }

                    Repeater {
                        model: chainTables.selectedEffects

                        delegate: StyledTextLabel {
                            required property var modelData

                            width: parent.width
                            horizontalAlignment: Qt.AlignLeft
                            elide: Text.ElideRight
                            opacity: modelData.available ? 1.0 : 0.5
                            text: modelData.available ? modelData.title
                                                      : qsTrc("preferences", "%1 (not installed)").arg(modelData.title)
                        }
                    }

                    StyledTextLabel {
                        width: parent.width
                        horizontalAlignment: Qt.AlignLeft
                        visible: chainTables.selectedName !== "" && chainTables.selectedEffects.length === 0
                        opacity: 0.6
                        text: qsTrc("preferences", "No effects")
                    }
                }

                Grid {
                    y: 300 + 8
                    columns: 2
                    spacing: 8

                    FlatButton {
                        width: 120
                        text: qsTrc("preferences", "Import")

                        navigation.name: "ChainPresetImport"
                        navigation.panel: section0.navigation
                        navigation.row: 0

                        onClicked: {
                            const name = chainPresetsModel.importPresets()
                            if (name !== "") {
                                chainTables.selectedName = name
                            }
                        }
                    }

                    FlatButton {
                        width: 120
                        text: qsTrc("preferences", "Rename")
                        enabled: chainTables.selectedName !== ""

                        navigation.name: "ChainPresetRename"
                        navigation.panel: section0.navigation
                        navigation.row: 1

                        onClicked: {
                            const name = chainPresetsModel.renamePreset(chainTables.selectedName)
                            if (name !== "") {
                                chainTables.selectedName = name
                            }
                        }
                    }

                    FlatButton {
                        width: 120
                        text: qsTrc("preferences", "Export")
                        enabled: chainTables.selectedName !== ""

                        navigation.name: "ChainPresetExport"
                        navigation.panel: section0.navigation
                        navigation.row: 2

                        onClicked: chainPresetsModel.exportPreset(chainTables.selectedName)
                    }

                    FlatButton {
                        width: 120
                        text: qsTrc("preferences", "Delete")
                        enabled: chainTables.selectedName !== ""

                        navigation.name: "ChainPresetDelete"
                        navigation.panel: section0.navigation
                        navigation.row: 3

                        onClicked: chainPresetsModel.deletePreset(chainTables.selectedName)
                    }
                }
            }
        }

        SeparatorLine {}

        BaseSection {
            id: section1

            title: qsTrc("preferences", "Visible Effects")

            navigation.section: root.navigationSection
            navigation.order: section0.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            StyledTextLabel {
                width: parent.width

                text: qsTrc("preferences", "Drag and drop to rearrange lists and show or hide effects.")

                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
            }

            Item {
                id: effectTables

                // The effect selected in either list; Effect Info shows it.
                property string selectedId: ""
                // Drag state: the effect being dragged and the drop marker.
                property string dragId: ""
                property var dropList: null
                property int dropIndex: -1

                readonly property int rowHeight: 24
                readonly property int buttonsWidth: 40
                readonly property real tableWidth: (width - buttonsWidth - 24) / 2

                width: parent.width
                height: 24 + 8 + 360

                function selectedIsHidden() {
                    const hidden = djEffectsModel.hiddenEffects
                    for (let i = 0; i < hidden.length; ++i) {
                        if (hidden[i].id === selectedId) {
                            return true
                        }
                    }
                    return false
                }

                function selectedExists() {
                    return selectedId !== "" && (selectedIsHidden() || indexIn(djEffectsModel.visibleEffects, selectedId) >= 0)
                }

                function indexIn(list, id) {
                    for (let i = 0; i < list.length; ++i) {
                        if (list[i].id === id) {
                            return i
                        }
                    }
                    return -1
                }

                // Where a drop at `point` (in effectTables coordinates) lands:
                // the list under it and the row index to insert at.
                function dropTarget(point) {
                    for (const list of [visibleList, hiddenList]) {
                        const p = effectTables.mapToItem(list, point.x, point.y)
                        if (p.x < 0 || p.x > list.width || p.y < -rowHeight / 2 || p.y > list.height + rowHeight / 2) {
                            continue
                        }
                        const contentY = p.y + list.contentY
                        const index = Math.max(0, Math.min(list.count, Math.round(contentY / rowHeight)))
                        return { list: list, index: index }
                    }
                    return { list: null, index: -1 }
                }

                Component {
                    id: effectRow

                    ListItemBlank {
                        id: rowItem

                        required property var modelData
                        required property int index
                        property ListView listView: ListView.view

                        width: ListView.view.width
                        height: effectTables.rowHeight
                        isSelected: modelData.id === effectTables.selectedId

                        StyledTextLabel {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            horizontalAlignment: Qt.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            text: rowItem.modelData.title
                        }

                        // Drop marker above this row.
                        Rectangle {
                            width: parent.width
                            height: 2
                            y: -1
                            color: ui.theme.accentColor
                            visible: effectTables.dragId !== "" && effectTables.dropList === rowItem.listView
                                     && effectTables.dropIndex === rowItem.index
                        }

                        // Drop marker below the last row.
                        Rectangle {
                            width: parent.width
                            height: 2
                            y: parent.height - 1
                            color: ui.theme.accentColor
                            visible: effectTables.dragId !== "" && effectTables.dropList === rowItem.listView
                                     && effectTables.dropIndex === rowItem.listView.count
                                     && rowItem.index === rowItem.listView.count - 1
                        }

                        MouseArea {
                            id: dragArea

                            property point pressPoint

                            anchors.fill: parent
                            preventStealing: true

                            onPressed: function(mouse) {
                                pressPoint = Qt.point(mouse.x, mouse.y)
                                effectTables.selectedId = rowItem.modelData.id
                            }

                            onPositionChanged: function(mouse) {
                                const p = mapToItem(effectTables, mouse.x, mouse.y)
                                if (effectTables.dragId === "") {
                                    if (Math.abs(mouse.y - pressPoint.y) + Math.abs(mouse.x - pressPoint.x) < 6) {
                                        return
                                    }
                                    effectTables.dragId = rowItem.modelData.id
                                    dragProxy.text = rowItem.modelData.title
                                }
                                dragProxy.x = p.x + 8
                                dragProxy.y = p.y - dragProxy.height / 2
                                const target = effectTables.dropTarget(p)
                                effectTables.dropList = target.list
                                effectTables.dropIndex = target.index
                            }

                            onReleased: {
                                // Clear the drag first: the move rebuilds the
                                // lists, which destroys this row mid-handler.
                                const id = effectTables.dragId
                                const toHidden = effectTables.dropList === hiddenList
                                const hasTarget = effectTables.dropList !== null
                                const index = effectTables.dropIndex
                                effectTables.dragId = ""
                                effectTables.dropList = null
                                effectTables.dropIndex = -1
                                if (id !== "" && hasTarget) {
                                    djEffectsModel.moveEffect(id, toHidden, index)
                                }
                            }

                            onCanceled: {
                                effectTables.dragId = ""
                                effectTables.dropList = null
                                effectTables.dropIndex = -1
                            }
                        }
                    }
                }

                StyledTextLabel {
                    x: 0
                    width: effectTables.tableWidth
                    horizontalAlignment: Qt.AlignLeft
                    font: ui.theme.bodyBoldFont
                    text: qsTrc("preferences", "Visible Effects")
                }

                StyledTextLabel {
                    x: effectTables.tableWidth + effectTables.buttonsWidth + 24
                    width: effectTables.tableWidth
                    horizontalAlignment: Qt.AlignLeft
                    font: ui.theme.bodyBoldFont
                    text: qsTrc("preferences", "Hidden Effects")
                }

                Rectangle {
                    id: visibleFrame

                    y: 32
                    width: effectTables.tableWidth
                    height: 360
                    color: "transparent"
                    border.color: ui.theme.strokeColor
                    border.width: 1
                    radius: 3

                    StyledListView {
                        id: visibleList

                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        ScrollBar.vertical: StyledScrollBar {}
                        model: djEffectsModel.visibleEffects
                        delegate: effectRow
                    }
                }

                Column {
                    x: effectTables.tableWidth + 12
                    y: 32 + (360 - height) / 2
                    width: effectTables.buttonsWidth
                    spacing: 8

                    FlatButton {
                        width: parent.width
                        text: "❯"
                        toolTipTitle: qsTrc("preferences", "Hide")
                        enabled: effectTables.selectedExists() && !effectTables.selectedIsHidden()

                        navigation.name: "EffectsHideButton"
                        navigation.panel: section1.navigation
                        navigation.row: 0

                        onClicked: djEffectsModel.moveEffect(effectTables.selectedId, true, -1)
                    }

                    FlatButton {
                        width: parent.width
                        text: "❮"
                        toolTipTitle: qsTrc("preferences", "Show")
                        enabled: effectTables.selectedExists() && effectTables.selectedIsHidden()

                        navigation.name: "EffectsShowButton"
                        navigation.panel: section1.navigation
                        navigation.row: 1

                        onClicked: djEffectsModel.moveEffect(effectTables.selectedId, false, -1)
                    }
                }

                Rectangle {
                    y: 32
                    x: effectTables.tableWidth + effectTables.buttonsWidth + 24
                    width: effectTables.tableWidth
                    height: 360
                    color: "transparent"
                    border.color: ui.theme.strokeColor
                    border.width: 1
                    radius: 3

                    StyledListView {
                        id: hiddenList

                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        ScrollBar.vertical: StyledScrollBar {}
                        model: djEffectsModel.hiddenEffects
                        delegate: effectRow
                    }

                    StyledTextLabel {
                        anchors.centerIn: parent
                        visible: hiddenList.count === 0
                        opacity: 0.6
                        text: qsTrc("preferences", "No hidden effects")
                    }
                }

                Rectangle {
                    id: dragProxy

                    property alias text: dragProxyLabel.text

                    visible: effectTables.dragId !== ""
                    z: 10
                    width: dragProxyLabel.implicitWidth + 12
                    height: effectTables.rowHeight
                    color: ui.theme.popupBackgroundColor
                    border.color: ui.theme.accentColor
                    radius: 3

                    StyledTextLabel {
                        id: dragProxyLabel
                        anchors.centerIn: parent
                        font: ui.theme.bodyBoldFont
                    }
                }
            }

            // Effect Info: the selected effect, as in Mixxx.
            Column {
                id: effectInfo

                readonly property var info: effectTables.selectedExists() ? djEffectsModel.effectInfo(effectTables.selectedId) : ({})

                width: parent.width
                spacing: 6

                StyledTextLabel {
                    width: parent.width
                    horizontalAlignment: Qt.AlignLeft
                    font: ui.theme.bodyBoldFont
                    text: qsTrc("preferences", "Effect Info")
                }

                Repeater {
                    model: [
                        { label: qsTrc("preferences", "Name:"), key: "name" },
                        { label: qsTrc("preferences", "Version:"), key: "version" },
                        { label: qsTrc("preferences", "Type:"), key: "type" },
                        { label: qsTrc("preferences", "Author:"), key: "author" },
                        { label: qsTrc("preferences", "Description:"), key: "description" }
                    ]

                    delegate: Row {
                        required property var modelData

                        width: effectInfo.width
                        spacing: 12

                        StyledTextLabel {
                            width: 96
                            horizontalAlignment: Qt.AlignRight
                            opacity: 0.7
                            text: modelData.label
                        }

                        StyledTextLabel {
                            width: parent.width - 108
                            horizontalAlignment: Qt.AlignLeft
                            wrapMode: Text.WordWrap
                            maximumLineCount: 4
                            text: effectInfo.info[modelData.key] || ""
                        }
                    }
                }
            }
        }

        SeparatorLine {}

        BaseSection {
            id: section2

            title: qsTrc("preferences", "Effect load behavior")

            navigation.section: root.navigationSection
            navigation.order: section1.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            // Mixxx's [Effects] AdoptMetaknobValue: used when an effect in a
            // track's list is replaced by another DJ Effect.
            RoundedRadioButton {
                text: qsTrc("preferences", "Keep metaknob position")
                checked: djEffectsModel.keepMetaknobPosition

                navigation.name: "KeepMetaknobPosition"
                navigation.panel: section2.navigation
                navigation.row: 0

                onToggled: djEffectsModel.keepMetaknobPosition = true
            }

            RoundedRadioButton {
                text: qsTrc("preferences", "Reset metaknob to effect default")
                checked: !djEffectsModel.keepMetaknobPosition

                navigation.name: "ResetMetaknobPosition"
                navigation.panel: section2.navigation
                navigation.row: 1

                onToggled: djEffectsModel.keepMetaknobPosition = false
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                opacity: 0.6
                text: qsTrc("preferences", "Applies when a DJ Effect in a track's effects is replaced by another one.")
            }
        }

        SeparatorLine {}

        BaseSection {
            id: sectionShelves

            title: qsTrc("preferences", "DJ EQ crossover frequencies")

            navigation.section: root.navigationSection
            navigation.order: section2.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            StyledTextLabel {
                width: parent.width
                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                text: qsTrc("preferences", "Where the low, mid and high bands split in the DJ EQ and isolator effects. Changes are heard straight away.")
            }

            Repeater {
                model: [
                    { title: qsTrc("preferences", "High EQ"), high: true },
                    { title: qsTrc("preferences", "Low EQ"), high: false }
                ]

                delegate: Row {
                    required property var modelData
                    required property int index

                    width: parent.width
                    spacing: 12

                    StyledTextLabel {
                        width: 80
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Qt.AlignLeft
                        text: modelData.title
                    }

                    StyledTextLabel {
                        width: 48
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Qt.AlignRight
                        opacity: 0.6
                        text: "16 Hz"
                    }

                    StyledSlider {
                        id: shelfSlider

                        readonly property real modelPosition: modelData.high ? djMixerModel.highEqPosition : djMixerModel.lowEqPosition

                        width: 320
                        anchors.verticalCenter: parent.verticalCenter
                        from: 0
                        to: 1
                        stepSize: 0.001
                        value: modelPosition
                        // Scrolling the page must not move a slider that
                        // passes under the pointer.
                        wheelEnabled: false

                        // A shelf held back by the other one (low stays below
                        // high) snaps to where it really is on release.
                        onPressedChanged: {
                            if (!pressed) {
                                value = Qt.binding(function() { return shelfSlider.modelPosition })
                            }
                        }

                        navigation.name: modelData.high ? "HighEqShelf" : "LowEqShelf"
                        navigation.panel: sectionShelves.navigation
                        navigation.row: index

                        onMoved: {
                            if (modelData.high) {
                                djMixerModel.setHighEqPosition(value)
                            } else {
                                djMixerModel.setLowEqPosition(value)
                            }
                        }
                    }

                    StyledTextLabel {
                        width: 72
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Qt.AlignLeft
                        opacity: 0.6
                        text: "20.05 kHz"
                    }

                    StyledTextLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Qt.AlignLeft
                        font: ui.theme.bodyBoldFont
                        text: modelData.high ? djMixerModel.highEqText : djMixerModel.lowEqText
                    }
                }
            }
        }

        SeparatorLine {}

        EffectOptionsSection {
            id: effectBehaviorSection

            pluginPreferencesModel: pluginModel

            navigation.section: root.navigationSection
            navigation.order: sectionShelves.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }
        }

        SeparatorLine {
            visible: lv2Section.visible
        }

        PluginLocationsSection {
            id: lv2Section

            visible: pluginModel.lv2Supported

            title: qsTrc("preferences", "Custom LV2 plugins location")
            dialogTitle: qsTrc("preferences", "Choose custom LV2 plugins location")

            paths: pluginModel.lv2CustomPaths
            pathValidator: function (p) {
                return pluginModel.pathExists(p)
            }

            navigation.section: root.navigationSection
            navigation.order: effectBehaviorSection.navigation.order + 1

            onAddPathRequested: pluginModel.addLv2Path()
            onPathChanged: function (index, newPath) {
                pluginModel.setLv2Path(index, newPath)
            }
            onRemovePathRequested: function (index) {
                pluginModel.removeLv2Path(index)
            }

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }
        }

        SeparatorLine {
            visible: vst3Section.visible
        }

        PluginLocationsSection {
            id: vst3Section

            visible: pluginModel.vst3Supported

            title: qsTrc("preferences", "Custom VST3 plugins location")
            dialogTitle: qsTrc("preferences", "Choose custom VST3 plugins location")

            paths: pluginModel.vst3CustomPaths
            pathValidator: function (p) {
                return pluginModel.pathExists(p)
            }

            navigation.section: root.navigationSection
            navigation.order: lv2Section.navigation.order + 1

            onAddPathRequested: pluginModel.addVst3Path()
            onPathChanged: function (index, newPath) {
                pluginModel.setVst3Path(index, newPath)
            }
            onRemovePathRequested: function (index) {
                pluginModel.removeVst3Path(index)
            }

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }
        }

        Repeater {
            model: pluginModel.extensionPreferences

            delegate: Column {
                id: extensionPreferences

                required property var modelData
                required property int index

                width: parent.width
                spacing: root.sectionsSpacing

                SeparatorLine {
                    width: parent.width
                }

                ExtensionPreferencesSection {
                    id: extensionSection

                    width: parent.width
                    preferenceGroup: extensionPreferences.modelData
                    pluginPreferencesModel: pluginModel

                    navigation.section: root.navigationSection
                    navigation.order: vst3Section.navigation.order + 1 + extensionPreferences.index

                    onFocusChanged: {
                        if (activeFocus) {
                            const position = extensionSection.mapToItem(sectionsColumn, 0, 0)
                            root.ensureContentVisibleRequested(Qt.rect(position.x, position.y, width, height))
                        }
                    }
                }
            }
        }
    }
}
