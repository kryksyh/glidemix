/*
 * Audacity: A Digital Audio Editor
 */
import QtQuick 2.15
import QtQuick.Window 2.15

import Muse.Ui 1.0
import Muse.UiComponents
import Audacity.AppShell

Page {
    title: qsTrc("appshell/gettingstarted", "Beta")
    explanation: qsTrc("appshell/gettingstarted", "GlideMix is in beta: new features and fixes come out almost every week. Found a bug or have an idea? Tell us on Discord (discord.gg/EvWwBAvfuE).")

    titleContentSpacing: 12

    Image {
        id: image
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        source: "resources/UnderConstruction.png"
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
    }
}
