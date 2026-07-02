// Presets — manage the three app-local saved positions. Names are editable
// inline; positions persist to ./config/obsbot4linux.json and survive a
// restart. "Save current position" fills the next empty slot.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Obsbot

Item {
    id: root

    ColumnLayout {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width, 720)
        spacing: Theme.s3

        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: "Presets" }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "+ Save current position"
                variant: "primary"
                enabled: cam.connected
                onClicked: cam.saveCurrentToNextEmpty()
            }
        }

        Repeater {
            model: cam.presets
            delegate: GlassPanel {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 64
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 40; Layout.preferredHeight: 40
                        radius: Theme.rControl
                        color: modelData.set ? Theme.accentTint : Qt.rgba(1, 1, 1, 0.03)
                        border.width: 1
                        border.color: modelData.set ? Theme.accentRing : Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: "P" + (modelData.index + 1)
                            color: modelData.set ? Theme.accentSoft : Theme.dim
                            font.family: Theme.mono; font.pixelSize: 13
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        TextField {
                            Layout.fillWidth: true
                            text: modelData.name
                            enabled: modelData.set
                            color: Theme.fg
                            font.family: Theme.mono; font.pixelSize: 13
                            background: Rectangle { color: "transparent" }
                            onEditingFinished: cam.renamePreset(modelData.index, text)
                        }
                        Text {
                            text: modelData.summary
                            color: Theme.dimmer
                            font.family: Theme.mono; font.pixelSize: 11
                        }
                    }

                    ActionButton { text: "Go"; variant: "secondary"; enabled: cam.connected && modelData.set; onClicked: cam.goPreset(modelData.index) }
                    ActionButton { text: modelData.set ? "Overwrite" : "Save"; variant: "secondary"; enabled: cam.connected; onClicked: cam.savePreset(modelData.index) }
                    ActionButton { text: "Clear"; variant: "ghost"; enabled: modelData.set; onClicked: cam.clearPreset(modelData.index) }
                }
            }
        }

        // Load a preset automatically on startup (explicit opt-in).
        // NOTE: height is COMPUTED from content (title + wrapped description +
        // control row), not a hardcoded constant — a fixed implicitHeight here
        // previously clipped/overlapped the wrapped description text against the
        // segmented control whenever the window was narrower than the text's
        // single-line width.
        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: startupCol.implicitHeight + 24
            ColumnLayout {
                id: startupCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Text { text: "Load on startup"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13 }
                Text {
                    text: "Move to this preset automatically when the camera connects or wakes from sleep. Only slots you've saved can be chosen."
                    color: Theme.dimmer; font.family: Theme.sans; font.pixelSize: 12
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Segmented {
                    Layout.alignment: Qt.AlignLeft
                    options: ["None", "P1", "P2", "P3"]
                    currentIndex: cam.startupPreset
                    onActivated: (i) => cam.startupPreset = i
                }
            }
        }

        // Go to a chosen preset automatically when AI Track is turned off.
        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: returnCol.implicitHeight + 24
            ColumnLayout {
                id: returnCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Text { text: "After AI off, go to"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13 }
                Text {
                    text: "When you turn AI Track off, move to this preset automatically. Off by default. Only slots you've saved can be chosen."
                    color: Theme.dimmer; font.family: Theme.sans; font.pixelSize: 12
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Segmented {
                    Layout.alignment: Qt.AlignLeft
                    options: ["None", "P1", "P2", "P3"]
                    currentIndex: cam.aiReturnPreset
                    onActivated: (i) => cam.aiReturnPreset = i
                }
            }
        }

        // Put the camera to sleep automatically when the app is closed.
        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: sleepRow.implicitHeight + 24
            RowLayout {
                id: sleepRow
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { text: "Sleep camera on exit"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13 }
                    Text {
                        text: "Put the camera to sleep automatically when you close the app."
                        color: Theme.dimmer; font.family: Theme.sans; font.pixelSize: 12
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
                ToggleChip {
                    text: cam.sleepOnExit ? "On" : "Off"
                    tone: Theme.degraded
                    checked: cam.sleepOnExit
                    onToggled: (c) => cam.sleepOnExit = c
                }
            }
        }
    }
}
