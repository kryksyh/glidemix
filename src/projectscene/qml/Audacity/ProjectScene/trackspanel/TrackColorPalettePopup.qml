import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

StyledDialogView {
    id: root

    property var colors: []
    property int currentColorIndex: 1
    property bool showReset: false

    readonly property int swatchSize: 28
    readonly property int paletteRows: 4
    readonly property int paletteRowSpacing: 6
    readonly property int paletteHeight: paletteRows * swatchSize
                                         + (paletteRows - 1) * paletteRowSpacing

    signal colorSelected(int colorIndex)
    signal resetRequested

    contentWidth: 24 * swatchSize + 23 * 4 + (showReset ? 36 : 0)
    // Grid's implicit height is not stable while the Repeater is being
    // populated.  DialogView reads the size as it opens, so relying on it can
    // lock the window at three rows even though all 96 delegates are present.
    contentHeight: titleLabel.implicitHeight + 12 + paletteHeight
    margins: 14
    modal: false
    frameless: true
    resizable: false
    alwaysAboveApp: true
    activateParentOnClose: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            id: titleLabel
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            font.bold: true
            font.pixelSize: 18
            text: qsTrc("projectscene", "Color")
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: root.paletteHeight
            Layout.minimumHeight: root.paletteHeight
            spacing: 8

            FlatButton {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                Layout.alignment: Qt.AlignTop
                visible: root.showReset
                icon: IconCode.UNDO
                toolTipTitle: qsTrc("projectscene", "Same as track color")
                onClicked: {
                    root.resetRequested()
                    root.close()
                }
            }

            Grid {
                id: palette
                columns: 24
                columnSpacing: 4
                rowSpacing: root.paletteRowSpacing

                Repeater {
                model: root.colors

                delegate: Item {
                    required property var modelData
                    readonly property bool selected: modelData.index === root.currentColorIndex

                    width: root.swatchSize
                    height: root.swatchSize

                    Rectangle {
                        id: swatch
                        anchors.centerIn: parent
                        width: parent.width - (parent.selected ? 0 : 4)
                        height: width
                        radius: 5
                        color: modelData.color
                        border.color: parent.selected ? ui.theme.fontPrimaryColor : Qt.rgba(0, 0, 0, 0.28)
                        border.width: parent.selected ? 3 : 1

                        Behavior on scale {
                            NumberAnimation { duration: 90 }
                        }
                    }

                    HoverHandler { id: hoverHandler }

                    states: State {
                        when: hoverHandler.hovered && !selected
                        PropertyChanges { target: swatch; scale: 1.12 }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.colorSelected(parent.modelData.index)
                            root.close()
                        }
                    }
                }
                }
            }
        }
    }

    // These are overlays, not ColumnLayout children. Putting them inside the
    // layout made Qt reserve two extra rows and pushed palette row four below
    // the dialog's clipping boundary.
    MouseArea {
        id: paletteDragArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: titleLabel.height
        z: 10
        cursorShape: Qt.OpenHandCursor

        onPressed: {
            cursorShape = Qt.ClosedHandCursor
            if (root.window) {
                root.window.startSystemMove()
            }
        }
        onReleased: cursorShape = Qt.OpenHandCursor
        onCanceled: cursorShape = Qt.OpenHandCursor
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 1
        anchors.topMargin: 1
        width: 12
        height: 12
        radius: 6
        color: "#FF5F57"
        z: 20

        MouseArea {
            anchors.fill: parent
            anchors.margins: -4
            cursorShape: Qt.PointingHandCursor
            onClicked: root.close()
        }
    }
}
