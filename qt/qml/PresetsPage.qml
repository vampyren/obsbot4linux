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

        // Behavior settings (startup preset, AI-return, power/sleep) moved to the
        // Settings page — this page is presets only.
    }
}
