/*
* Audacity: A Digital Audio Editor
*/
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.UiComponents
import Audacity.ProjectScene

import "internal"

StyledToolBarView {
    id: root

    property bool isCompactMode: false

    navigationPanel.name: "WorkspacesToolBar"
    navigationPanel.accessible.name: qsTrc("projectscene", "Workspace selection")

    spacing: 0
    // GlideMix: room for the "Workspace" label above the picker.
    rowHeight: root.isCompactMode ? 28 : 46

    WorkspacesToolBarModel {
        id: workspacesModel
    }

    model: workspacesModel

    sourceComponentCallback: function (type) {
        switch (type) {
        case ToolBarItemType.ACTION:
            return controlComp
        }

        return null
    }

    Component {
        id: controlComp

        Loader {
            id: loader

            property var itemData: null

            sourceComponent: !root.isCompactMode ? dropdownComp : buttonComp

            Component {
                id: dropdownComp

                // GlideMix modern look: the label sits above the picker, so
                // the picker fits inside its rounded card.
                Column {
                    property var itemData: loader.itemData
                    spacing: 2

                    StyledTextLabel {
                        x: 2
                        text: qsTrc("projectscene", "Workspace")
                        font.pixelSize: 10
                        opacity: 0.7
                    }

                    DropdownWithTitle {
                        property var itemData: loader.itemData

                        width: 170
                        height: 28

                        title: ""
                        model: Boolean(itemData) ? itemData.menuItems : null
                        current: Boolean(itemData) ? itemData.title : ""

                        allowOptionToggle: false

                        navigation.panel: root.navigationPanel
                        navigation.order: 1

                        drawFocusBorderInsideRect: true

                        onHandleMenuItem: function (itemId) {
                            Qt.callLater(root.model.handleWorkspacesMenuItem, itemId)
                        }
                    }
                }
            }

            Component {
                id: buttonComp

                StyledToolBarItem {
                    itemData: loader.itemData

                    icon: IconCode.WORKSPACE
                    enabled: true

                    navigation.panel: root.navigationPanel
                    navigation.order: 2
                }
            }
        }
    }
}
