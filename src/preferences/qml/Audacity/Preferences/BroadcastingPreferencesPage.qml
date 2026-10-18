/*
 * Audacity: A Digital Audio Editor
 */

/*
 * Live broadcasting: the one server the app streams to, and what the listeners
 * see. WIRED to BroadcastPreferencesModel over IBroadcastConfiguration; the
 * radio tower in the header puts this on air, and so does Connect below.
 *
 * The chrome is Audacity's own — do not restyle it.
 */
import QtQuick

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.Preferences
import Audacity.UiComponents 1.0

PreferencesPage {
    id: root

    // Not `model`: inside a ComboBox or a Repeater that name is taken by the
    // delegate's own model, and the bindings silently read the wrong object.
    BroadcastPreferencesModel {
        id: broadcast
    }

    Component.onCompleted: broadcast.init()

    Column {
        width: parent.width
        spacing: root.sectionsSpacing

        BaseSection {
            id: serverSection

            title: qsTrc("preferences", "Server")

            navigation.section: root.navigationSection
            navigation.order: root.navigationOrderStart

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            StyledTextLabel {
                width: parent.width
                text: qsTrc("preferences", "Where the stream goes: an Icecast or Shoutcast server. The header's broadcast button streams the output named on it.")
                horizontalAlignment: Qt.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 3
            }

            Row {
                width: parent.width
                spacing: 40

                Column {
                    spacing: 12

                    ComboBoxWithTitle {
                        id: protocolBox

                        title: qsTrc("preferences", "Server type:")

                        //! The values are what gets stored, so they are listed
                        //! explicitly rather than taken from the row order.
                        readonly property var protocols: [
                            { text: qsTrc("preferences", "Icecast 2"), value: 0 },
                            { text: qsTrc("preferences", "Icecast 1"), value: 2 },
                            { text: qsTrc("preferences", "Shoutcast 2 (DNAS 2)"), value: 3 },
                            { text: qsTrc("preferences", "Shoutcast 1"), value: 1 }
                        ]

                        columnWidth: 240
                        model: protocols
                        currentIndex: {
                            for (var i = 0; i < protocolBox.protocols.length; ++i) {
                                if (protocolBox.protocols[i].value === broadcast.protocol) {
                                    return i
                                }
                            }
                            return 0
                        }
                        textRole: "text"
                        valueRole: "value"

                        onValueEdited: function(newIndex, newValue) {
                            broadcast.protocol = newValue
                        }

                        navigation.name: "BroadcastProtocol"
                        navigation.panel: serverSection.navigation
                        navigation.row: 0
                    }

                    Column {
                        spacing: 6

                        StyledTextLabel {
                            horizontalAlignment: Qt.AlignLeft
                            text: qsTrc("preferences", "Address:")
                        }

                        TextInputField {
                            width: 240
                            currentText: broadcast.host
                            onTextEditingFinished: function(text) { broadcast.host = text }

                            navigation.name: "BroadcastHost"
                            navigation.panel: serverSection.navigation
                            navigation.row: 1
                        }
                    }

                    //! A port is an identifier, not a quantity: a spin box formats it with
                    //! the locale's thousands separator ("6,708"), so it gets a plain
                    //! field that only takes digits.
                    Column {
                        spacing: 6

                        StyledTextLabel {
                            horizontalAlignment: Qt.AlignLeft
                            text: qsTrc("preferences", "Port:")
                        }

                        TextInputField {
                            width: 240
                            currentText: String(broadcast.port)
                            validator: IntValidator { bottom: 1; top: 65535 }
                            onTextEditingFinished: function(text) {
                                var port = parseInt(text, 10)
                                if (!isNaN(port) && port > 0 && port <= 65535) {
                                    broadcast.port = port
                                }
                            }

                            navigation.name: "BroadcastPort"
                            navigation.panel: serverSection.navigation
                            navigation.row: 2
                        }
                    }

                    //! Icecast addresses each stream by a mount point;
                    //! Shoutcast does not have them.
                    Column {
                        spacing: 6
                        visible: broadcast.protocol === 0 || broadcast.protocol === 2

                        StyledTextLabel {
                            horizontalAlignment: Qt.AlignLeft
                            text: qsTrc("preferences", "Mount point:")
                        }

                        TextInputField {
                            width: 240
                            currentText: broadcast.mount
                            onTextEditingFinished: function(text) { broadcast.mount = text }

                            navigation.name: "BroadcastMount"
                            navigation.panel: serverSection.navigation
                            navigation.row: 3
                        }
                    }

                    //! Icecast 2 authenticates a user, usually "source".
                    //! Icecast 1 and Shoutcast 1 take the password alone.
                    Column {
                        spacing: 6
                        visible: broadcast.protocol === 0

                        StyledTextLabel {
                            horizontalAlignment: Qt.AlignLeft
                            text: qsTrc("preferences", "Login:")
                        }

                        TextInputField {
                            width: 240
                            currentText: broadcast.login
                            onTextEditingFinished: function(text) { broadcast.login = text }

                            navigation.name: "BroadcastLogin"
                            navigation.panel: serverSection.navigation
                            navigation.row: 4
                        }
                    }

                    Column {
                        spacing: 6

                        StyledTextLabel {
                            horizontalAlignment: Qt.AlignLeft
                            text: qsTrc("preferences", "Password:")
                        }

                        TextInputField {
                            width: 240
                            currentText: broadcast.password
                            onTextEditingFinished: function(text) { broadcast.password = text }

                            navigation.name: "BroadcastPassword"
                            navigation.panel: serverSection.navigation
                            navigation.row: 5
                        }
                    }

                    //! DNAS 2 can carry several streams on one server, so it
                    //! needs to be told which one this is. Hosts that run a
                    //! single stream still call it stream 1.
                    Column {
                        spacing: 6
                        visible: broadcast.protocol === 3

                        StyledTextLabel {
                            horizontalAlignment: Qt.AlignLeft
                            text: qsTrc("preferences", "Stream ID:")
                        }

                        TextInputField {
                            width: 240
                            currentText: String(broadcast.streamId)
                            validator: IntValidator { bottom: 1; top: 999 }
                            onTextEditingFinished: function(text) {
                                var sid = parseInt(text, 10)
                                if (!isNaN(sid) && sid > 0) {
                                    broadcast.streamId = sid
                                }
                            }

                            navigation.name: "BroadcastStreamId"
                            navigation.panel: serverSection.navigation
                            navigation.row: 6
                        }
                    }

                    //! Optional on DNAS 2: it goes out with the password so the
                    //! server can log who was at the desk.
                    Column {
                        spacing: 6
                        visible: broadcast.protocol === 3

                        StyledTextLabel {
                            horizontalAlignment: Qt.AlignLeft
                            text: qsTrc("preferences", "DJ name (optional):")
                        }

                        TextInputField {
                            width: 240
                            currentText: broadcast.login === "source" ? "" : broadcast.login
                            onTextEditingFinished: function(text) {
                                broadcast.login = text.length > 0 ? text : "source"
                            }

                            navigation.name: "BroadcastDjName"
                            navigation.panel: serverSection.navigation
                            navigation.row: 7
                        }
                    }
                }

                //! What a listener needs: the player address for this server,
                //! built from the settings on the left. It is the player port,
                //! never the source port the app connects on.
                Rectangle {
                    width: Math.max(260, serverSection.width - 300)
                    height: linkColumn.height + 28
                    radius: 4
                    color: ui.theme.backgroundSecondaryColor
                    border.width: 1
                    border.color: ui.theme.strokeColor

                    Column {
                        id: linkColumn

                        x: 14
                        y: 14
                        width: parent.width - 28
                        spacing: 8

                        StyledTextLabel {
                            width: parent.width
                            horizontalAlignment: Qt.AlignLeft
                            font: ui.theme.bodyBoldFont
                            text: qsTrc("preferences", "Listen link")
                        }

                        StyledTextLabel {
                            width: parent.width
                            horizontalAlignment: Qt.AlignLeft
                            wrapMode: Text.WordWrap
                            opacity: 0.7
                            text: qsTrc("preferences", "Give this to listeners, or paste it into a player to check the stream.")
                        }

                        TextInputField {
                            width: parent.width
                            readOnly: true
                            currentText: broadcast.listenUrl.length > 0 ? broadcast.listenUrl
                                         : qsTrc("preferences", "Fill in the address and port")
                        }

                        FlatButton {
                            text: qsTrc("preferences", "Copy link")
                            enabled: broadcast.listenUrl.length > 0
                            onClicked: broadcast.copyListenUrl()
                        }
                    }
                }
            }
        }

        SeparatorLine {}

        BaseSection {
            id: streamSection

            title: qsTrc("preferences", "What the listeners see")

            navigation.section: root.navigationSection
            navigation.order: serverSection.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            Column {
                spacing: 6

                StyledTextLabel {
                    horizontalAlignment: Qt.AlignLeft
                    text: qsTrc("preferences", "Stream name:")
                }

                TextInputField {
                    width: 240
                    currentText: broadcast.streamName
                    onTextEditingFinished: function(text) { broadcast.streamName = text }

                    navigation.name: "BroadcastName"
                    navigation.panel: streamSection.navigation
                    navigation.row: 0
                }
            }

            Column {
                spacing: 6

                StyledTextLabel {
                    horizontalAlignment: Qt.AlignLeft
                    text: qsTrc("preferences", "Description:")
                }

                TextInputField {
                    width: 240
                    currentText: broadcast.description
                    onTextEditingFinished: function(text) { broadcast.description = text }

                    navigation.name: "BroadcastDescription"
                    navigation.panel: streamSection.navigation
                    navigation.row: 1
                }
            }

            Column {
                spacing: 6

                StyledTextLabel {
                    horizontalAlignment: Qt.AlignLeft
                    text: qsTrc("preferences", "Genre:")
                }

                TextInputField {
                    width: 240
                    currentText: broadcast.genre
                    onTextEditingFinished: function(text) { broadcast.genre = text }

                    navigation.name: "BroadcastGenre"
                    navigation.panel: streamSection.navigation
                    navigation.row: 2
                }
            }

            Column {
                spacing: 6

                StyledTextLabel {
                    horizontalAlignment: Qt.AlignLeft
                    text: qsTrc("preferences", "Website:")
                }

                TextInputField {
                    width: 240
                    currentText: broadcast.website
                    onTextEditingFinished: function(text) { broadcast.website = text }

                    navigation.name: "BroadcastWebsite"
                    navigation.panel: streamSection.navigation
                    navigation.row: 3
                }
            }

            CheckBox {
                width: parent.width
                text: qsTrc("preferences", "List the stream in the server's public directory")
                checked: broadcast.publicStream

                navigation.name: "BroadcastPublic"
                navigation.panel: streamSection.navigation
                navigation.row: 4

                onClicked: { broadcast.publicStream = !checked }
            }

            CheckBox {
                width: parent.width
                text: qsTrc("preferences", "Send the playing track's name to the server")
                checked: broadcast.sendMetadata

                navigation.name: "BroadcastMetadata"
                navigation.panel: streamSection.navigation
                navigation.row: 5

                onClicked: { broadcast.sendMetadata = !checked }
            }

            ComboBoxWithTitle {
                title: qsTrc("preferences", "Track name as:")

                columnWidth: 240
                enabled: broadcast.sendMetadata
                currentIndex: broadcast.titleFirst ? 1 : 0
                model: [
                    { text: qsTrc("preferences", "Artist - Title"), value: 0 },
                    { text: qsTrc("preferences", "Title - Artist"), value: 1 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    broadcast.titleFirst = newValue === 1
                }

                navigation.name: "BroadcastTitleOrder"
                navigation.panel: streamSection.navigation
                navigation.row: 6
            }
        }

        SeparatorLine {}

        BaseSection {
            id: streamAudioSection

            title: qsTrc("preferences", "Audio")

            navigation.section: root.navigationSection
            navigation.order: streamSection.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            ComboBoxWithTitle {
                title: qsTrc("preferences", "Broadcast:")

                columnWidth: 240
                currentIndex: broadcast.source
                model: [
                    { text: qsTrc("preferences", "The Perform output (what the audience hears)"), value: 0 },
                    { text: qsTrc("preferences", "The headphone cue"), value: 1 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    broadcast.source = newValue
                }

                navigation.name: "BroadcastSource"
                navigation.panel: streamAudioSection.navigation
                navigation.row: 0
            }

            ComboBoxWithTitle {
                title: qsTrc("preferences", "MP3 bitrate:")

                columnWidth: 240
                currentIndex: [64, 96, 128, 192, 256, 320].indexOf(broadcast.bitrate)
                model: [
                    { text: "64 kbps", value: 64 },
                    { text: "96 kbps", value: 96 },
                    { text: "128 kbps", value: 128 },
                    { text: "192 kbps", value: 192 },
                    { text: "256 kbps", value: 256 },
                    { text: "320 kbps", value: 320 }
                ]
                textRole: "text"
                valueRole: "value"

                onValueEdited: function(newIndex, newValue) {
                    broadcast.bitrate = newValue
                }

                navigation.name: "BroadcastBitrate"
                navigation.panel: streamAudioSection.navigation
                navigation.row: 1
            }
        }

        SeparatorLine {}

        BaseSection {
            id: statusSection

            title: qsTrc("preferences", "Connection")

            navigation.section: root.navigationSection
            navigation.order: streamAudioSection.navigationOrderEnd + 1

            onFocusChanged: {
                if (activeFocus) {
                    root.ensureContentVisibleRequested(Qt.rect(x, y, width, height))
                }
            }

            Row {
                spacing: 12

                FlatButton {
                    text: broadcast.state === 0 || broadcast.state === 3
                          ? qsTrc("preferences", "Connect") : qsTrc("preferences", "Disconnect")
                    accentButton: broadcast.state === 2

                    navigation.name: "BroadcastConnect"
                    navigation.panel: statusSection.navigation
                    navigation.row: 0

                    onClicked: broadcast.toggle()
                }

                StyledTextLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: broadcast.statusText.length > 0 ? broadcast.statusText
                          : qsTrc("preferences", "Not connected")
                    horizontalAlignment: Qt.AlignLeft
                    color: broadcast.state === 3 ? "#E5484D"
                           : broadcast.state === 2 ? ui.theme.accentColor : ui.theme.fontPrimaryColor
                }
            }
        }
    }
}
