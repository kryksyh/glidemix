/*
 * GlideMix: Match and Related settings. Which rules a song must meet to match
 * the song under the playhead: Match lights its key, Related lists it. Rows in
 * the order DJs know from the usual DJ software, our own wording and code.
 * Saved, applied on OK.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

Popup {
    id: root

    property var rules: ({})
    property var tagNames: []
    signal applied(var rules)

    function openWith(current, names) {
        // A copy: Cancel leaves the saved rules as they were.
        var copy = {}
        for (var k in current) {
            copy[k] = current[k]
        }
        root.rules = copy
        root.tagNames = names || []
        open()
    }

    function setRule(key, value) {
        var copy = {}
        for (var k in root.rules) {
            copy[k] = root.rules[k]
        }
        copy[key] = value
        root.rules = copy
    }

    function numberOr(key, fallback) {
        return root.rules[key] !== undefined ? root.rules[key] : fallback
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 20
    width: 640
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: ui.theme.popupBackgroundColor
        border.width: 1
        border.color: ui.theme.strokeColor
        radius: 6
    }

    // The rule's on/off box, first column of every row.
    component RuleCheck: CheckBox {
        property string ruleKey
        Layout.preferredWidth: 210
        checked: root.rules[ruleKey] === true
        onClicked: root.setRule(ruleKey, !checked)
    }

    component Hint: StyledTextLabel {
        opacity: 0.7
        horizontalAlignment: Text.AlignLeft
    }

    contentItem: ColumnLayout {
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            font: ui.theme.bodyBoldFont
            text: qsTrc("mixhouse", "Match and Related")
        }
        Hint {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTrc("mixhouse", "Songs that meet every ticked rule match the song under the playhead.")
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            columns: 2
            columnSpacing: 12
            rowSpacing: 7

            // BPM
            RuleCheck { ruleKey: "bpmOn"; text: qsTrc("mixhouse", "BPM") }
            RowLayout {
                StyledTextLabel { text: "±" }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: root.numberOr("bpmPercent", 5)
                    step: 1
                    decimals: 0
                    minValue: 1
                    maxValue: 20
                    measureUnitsSymbol: "%"
                    onValueEdited: function(newValue) { root.setRule("bpmPercent", newValue) }
                }
                CheckBox {
                    text: qsTrc("mixhouse", "Half and double tempo too")
                    checked: root.rules.halfDouble === true
                    onClicked: root.setRule("halfDouble", !checked)
                }
            }

            // Key
            RuleCheck { ruleKey: "keyOn"; text: qsTrc("mixhouse", "Key") }
            StyledDropdown {
                Layout.preferredWidth: 320
                model: [
                    { text: qsTrc("mixhouse", "Same key only (8A)"), value: 0 },
                    { text: qsTrc("mixhouse", "Same and relative (8A, 8B)"), value: 1 },
                    { text: qsTrc("mixhouse", "One step around (8A, 8B, 7A, 9A)"), value: 2 },
                    { text: qsTrc("mixhouse", "Wider (also 7B, 9B)"), value: 3 }
                ]
                currentIndex: root.numberOr("keyWidth", 2)
                onActivated: function(index, value) { root.setRule("keyWidth", value) }
            }

            // Energy (GlideMix's own)
            RuleCheck { ruleKey: "energyOn"; text: qsTrc("mixhouse", "Energy") }
            Hint { text: qsTrc("mixhouse", "The same or one up") }

            // Year
            RuleCheck { ruleKey: "yearOn"; text: qsTrc("mixhouse", "Year") }
            RowLayout {
                StyledTextLabel { text: "±" }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: root.numberOr("yearRange", 0)
                    step: 1
                    decimals: 0
                    minValue: 0
                    maxValue: 30
                    onValueEdited: function(newValue) { root.setRule("yearRange", newValue) }
                }
                StyledTextLabel { text: qsTrc("mixhouse", "years") }
            }

            // Date added
            RuleCheck { ruleKey: "addedOn"; text: qsTrc("mixhouse", "Date Added") }
            RowLayout {
                StyledTextLabel { text: qsTrc("mixhouse", "In the last") }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: root.numberOr("addedDays", 30)
                    step: 1
                    decimals: 0
                    minValue: 1
                    maxValue: 3650
                    onValueEdited: function(newValue) { root.setRule("addedDays", newValue) }
                }
                StyledTextLabel { text: qsTrc("mixhouse", "days") }
            }

            // History
            RuleCheck { ruleKey: "historyOn"; text: qsTrc("mixhouse", "History") }
            RowLayout {
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: root.numberOr("historyRange", 3)
                    step: 1
                    decimals: 0
                    minValue: 1
                    maxValue: 20
                    onValueEdited: function(newValue) { root.setRule("historyRange", newValue) }
                }
                StyledTextLabel { text: qsTrc("mixhouse", "songs played before and after it") }
            }

            // Same ...
            RuleCheck { ruleKey: "artistOn"; text: qsTrc("mixhouse", "Same artist") }
            Hint { text: qsTrc("mixhouse", "Also when the title names the artist (feat.)") }

            RuleCheck { ruleKey: "genreOn"; text: qsTrc("mixhouse", "Same genre") }
            Item { width: 1; height: 1 }

            RuleCheck { ruleKey: "composerOn"; text: qsTrc("mixhouse", "Same composer") }
            Item { width: 1; height: 1 }

            RuleCheck { ruleKey: "remixerOn"; text: qsTrc("mixhouse", "Same remixer") }
            Item { width: 1; height: 1 }

            RuleCheck { ruleKey: "labelOn"; text: qsTrc("mixhouse", "Same label") }
            Item { width: 1; height: 1 }

            RuleCheck { ruleKey: "colorOn"; text: qsTrc("mixhouse", "Same colour") }
            Item { width: 1; height: 1 }

            // Rating: click a star
            RuleCheck { ruleKey: "ratingOn"; text: qsTrc("mixhouse", "Rating") }
            RowLayout {
                spacing: 2
                StyledTextLabel { text: qsTrc("mixhouse", "At least"); Layout.rightMargin: 6 }
                Repeater {
                    model: 5
                    delegate: Text {
                        required property int index
                        text: index < root.numberOr("minRating", 1) ? "★" : "☆"
                        color: ui.theme.fontPrimaryColor
                        font.pixelSize: 18
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            // Click the star that is already set to clear it (no minimum).
                            onClicked: root.setRule("minRating",
                                                    root.numberOr("minRating", 1) === index + 1 ? 0 : index + 1)
                        }
                    }
                }
            }

            // Sounds like (the song under the playhead)
            RuleCheck { ruleKey: "soundsLikeOn"; text: qsTrc("mixhouse", "Sounds like") }
            RowLayout {
                StyledTextLabel { text: qsTrc("mixhouse", "At least") }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: root.numberOr("minSoundsLike", 50)
                    step: 5
                    decimals: 0
                    minValue: 0
                    maxValue: 100
                    onValueEdited: function(newValue) { root.setRule("minSoundsLike", newValue) }
                }
                StyledTextLabel { text: "%" }
            }

            // Comment
            RuleCheck { ruleKey: "commentOn"; text: qsTrc("mixhouse", "Comment") }
            RowLayout {
                StyledTextLabel { text: qsTrc("mixhouse", "Contains") }
                TextInputField {
                    Layout.preferredWidth: 260
                    currentText: root.rules.commentText !== undefined ? root.rules.commentText : ""
                    onTextChanged: function(newTextValue) { root.setRule("commentText", newTextValue) }
                }
            }

            // Tag: any tag column from the user's files
            RuleCheck { ruleKey: "tagOn"; text: qsTrc("mixhouse", "Tag") }
            RowLayout {
                StyledDropdown {
                    Layout.preferredWidth: 220
                    model: root.tagNames.map(function(n) { return { text: n, value: n } })
                    currentIndex: root.tagNames.indexOf(root.rules.tagName || "")
                    indeterminateText: root.tagNames.length > 0 ? qsTrc("mixhouse", "Choose a tag")
                                                                : qsTrc("mixhouse", "No extra tags in the library")
                    onActivated: function(index, value) { root.setRule("tagName", value) }
                }
                Hint { text: qsTrc("mixhouse", "Shares any of its values") }
            }

            // File format
            RuleCheck { ruleKey: "formatOn"; text: qsTrc("mixhouse", "Same file format") }
            Hint { text: qsTrc("mixhouse", "MP3 with MP3, FLAC with FLAC...") }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: 8
            spacing: 8
            FlatButton {
                text: qsTrc("global", "Cancel")
                onClicked: root.close()
            }
            FlatButton {
                text: qsTrc("global", "OK")
                accentButton: true
                onClicked: {
                    root.applied(root.rules)
                    root.close()
                }
            }
        }
    }
}
