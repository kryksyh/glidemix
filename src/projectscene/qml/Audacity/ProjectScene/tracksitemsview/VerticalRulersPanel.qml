import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents

import Audacity.ProjectScene

Rectangle {
    id: root

    property ViewTracksListModel model: null
    property var context: null

    readonly property int listHeaderHeight: 2

    width: 32
    color: ui.theme.backgroundQuarternaryColor

    visible: model.isVerticalRulersVisible

    Rectangle {
        id: leftBorder

        width: 1
        height: parent.height
        color: ui.theme.strokeColor
        opacity: 0.1
    }

    StyledListView {
        id: verticalRulersListView

        anchors.fill: parent

        clip: false

        ScrollBar.vertical: null

        //! NOTE Sync with TracksItemsView
        TracksViewStateModel {
            id: tracksViewState
            onTracksVerticalOffsetChanged: {
                verticalRulersListView.contentY = tracksViewState.tracksVerticalOffset - root.listHeaderHeight
            }
        }

        Component.onCompleted: {
            tracksViewState.init()
        }

        header: Rectangle {
            height: root.listHeaderHeight
            width: parent.width
            color: "transparent"
        }

        footer: Item {
            height: tracksViewState.tracksVerticalScrollPadding
        }

        property real lockedVerticalScrollPosition
        property bool verticalScrollLocked: tracksViewState.tracksVerticalScrollLocked

        onVerticalScrollLockedChanged: {
            lockedVerticalScrollPosition = contentY
        }

        onContentYChanged: {
            if (verticalScrollLocked) {
                verticalRulersListView.contentY = lockedVerticalScrollPosition
            } else {
                tracksViewState.changeTracksVerticalOffset(verticalRulersListView.contentY + root.listHeaderHeight)
            }
        }

        interactive: false

        model: root.model

        delegate: Loader {
            width: root.width
            height: trackViewState.trackHeight

            sourceComponent: model.trackType !== TrackType.LABEL ? waveComp : emptyComp

            TrackViewStateModel {
                id: trackViewState
                trackId: model.trackId
            }

            Component.onCompleted: {
                trackViewState.init()
            }

            Component {
                id: emptyComp

                Item {}
            }

            Component {
                id: waveComp

                Rectangle {
                    color: ui.theme.backgroundQuarternaryColor

                    MouseArea {
                        id: mouseClickBlocker // to prevent clicks from reaching and modifying the viewport
                        anchors.fill: parent
                    }

                    Rectangle {
                        id: leftBorder

                        anchors.left: parent.left
                        anchors.top: parent.top

                        width: 1
                        height: parent.height
                        color: ui.theme.strokeColor
                        opacity: 0.1
                    }

                    Rectangle {
                        id: header

                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right

                        width: parent.width
                        height: trackViewState.isTrackCollapsed ? 0 : 20
                        color: ui.theme.extra["black_color"]
                        opacity: 0.20
                    }

                    Rectangle {
                        id: topBorder

                        anchors.top: parent.top
                        anchors.left: header.left
                        anchors.right: header.right
                        anchors.topMargin: -border.width

                        height: border.width

                        visible: model.isTrackFocused

                        color: "transparent"

                        border.color: ui.theme.extra["focus_state_color"]
                        border.width: 1
                    }

                    Rectangle {
                        id: bottomBorder

                        anchors.bottom: parent.bottom
                        anchors.left: header.left
                        anchors.right: header.right

                        height: 1

                        color: ui.theme.extra["focus_state_color"]

                        visible: model.isTrackFocused
                    }

                    SeparatorLine {
                        id: sep

                        color: ui.theme.extra["black_color"]
                        opacity: 0.20
                        anchors.bottom: parent.bottom
                        thickness: 2
                    }

                    ColumnLayout {

                        anchors.top: header.bottom
                        anchors.bottom: sep.top
                        anchors.left: leftBorder.right
                        anchors.right: parent.right
                        anchors.bottomMargin: 1

                        Loader {
                            id: waveformRulerLoader

                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            active: false
                            visible: model.isWaveformViewVisible

                            // Defer ruler creation, to speed up track loading
                            Component.onCompleted: Qt.callLater(function () {
                                waveformRulerLoader.active = Qt.binding(function () {
                                    return model.isWaveformViewVisible
                                })
                            })

                            // GlideMix (Naveen G, Chris): the scale of the clip's volume
                            // line, not Audacity's two-channel waveform scale -- one
                            // scale per track, each mark where the line puts that level
                            // (0 dB at 70%, boost above; BezierAutomationLane's insets).
                            sourceComponent: Item {
                                id: volumeRuler
                                visible: !trackViewState.isTrackCollapsed
                                readonly property real topInset: 10
                                readonly property real bottomInset: 10 + Math.min(16, Math.max(4, height * 0.14))
                                function yFor(gain) {
                                    const view = gain <= 1 ? 0.7 * gain : 0.7 + 0.3 * (gain - 1) / 3
                                    return topInset + (1 - view) * Math.max(1, height - topInset - bottomInset)
                                }
                                Repeater {
                                    model: [ { text: "+12", gain: 4.0 }, { text: "+6", gain: 2.0 }, { text: "0 dB", gain: 1.0 },
                                             { text: "-6", gain: 0.5012 }, { text: "-12", gain: 0.2512 }, { text: "-\u221E", gain: 0.0 } ]
                                    Item {
                                        required property var modelData
                                        width: volumeRuler.width
                                        height: 1
                                        y: volumeRuler.yFor(modelData.gain)
                                        // Room for at least two marks between labels: hide the
                                        // in-between ones on a short track.
                                        visible: volumeRuler.height > 90 || modelData.gain === 1.0 || modelData.gain === 0.0
                                        Rectangle {
                                            anchors.left: parent.left
                                            width: modelData.gain === 1.0 ? 8 : 5
                                            height: 1
                                            color: ui.theme.fontPrimaryColor
                                            opacity: 0.6
                                        }
                                        StyledTextLabel {
                                            anchors.left: parent.left
                                            anchors.leftMargin: 10
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.text
                                            font.pixelSize: 10
                                            font.bold: modelData.gain === 1.0
                                            opacity: 0.8
                                        }
                                    }
                                }
                            }
                        }

                        SeparatorLine {
                            color: ui.theme.extra["waveform_ruler_tick_extension_color"]
                        }

                        Loader {
                            id: spectrogramRulerLoader

                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            active: false
                            visible: model.isSpectrogramViewVisible

                            // Defer ruler creation, to speed up track loading
                            Component.onCompleted: Qt.callLater(function () {
                                spectrogramRulerLoader.active = Qt.binding(function () {
                                    return model.isSpectrogramViewVisible
                                })
                            })

                            sourceComponent: SpectrogramTrackRulers {
                                trackId: model.trackId
                                isStereo: model.isStereo
                                channelHeightRatio: trackViewState.channelHeightRatio
                            }
                        }
                    }
                }
            }
        }
    }
}
