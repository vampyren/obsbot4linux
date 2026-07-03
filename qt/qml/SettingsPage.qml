// Settings — app & camera behavior that isn't a live control: startup/AI
// preset automation, power/sleep management, and experimental options.
// (These panels accumulated on the Presets page until Rex rightly called it
// out — Presets now holds only the presets themselves.)
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

        SectionLabel { text: "Automation" }

        // Load a preset automatically on startup (explicit opt-in).
        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: startupCol.implicitHeight + 24
            ColumnLayout {
                id: startupCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Text { text: "Load preset on startup"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13 }
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

        SectionLabel { text: "Power & sleep" }

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

        // Auto-sleep timer (issue #9). "Device" = don't manage.
        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: autoSleepCol.implicitHeight + 24
            ColumnLayout {
                id: autoSleepCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Text { text: "Auto sleep"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13 }
                Text {
                    text: "How long the camera idles before sleeping on its own. \"Never\" disables auto-sleep. "
                        + "\"Device\" means this app won't change the camera's current setting (it does not restore "
                        + "a previous one)."
                    color: Theme.dimmer; font.family: Theme.sans; font.pixelSize: 12
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Segmented {
                    Layout.alignment: Qt.AlignLeft
                    options: ["Device", "Never", "2 min", "5 min", "10 min", "20 min"]
                    currentIndex: cam.autoSleepIndex
                    onActivated: (i) => cam.autoSleepIndex = i
                }
                // Live device readback — full-width row so it never squeezes
                // against the selector (Rex's "fix the text" screenshot).
                KeyValue {
                    Layout.fillWidth: true
                    keyWidth: 118
                    key: "device reports"
                    value: cam.autoSleepDevice > 0 ? ("sleeps after " + Math.round(cam.autoSleepDevice / 60) + " min")
                         : cam.autoSleepDevice === 0 ? "never auto-sleeps" : "—"
                    unknown: cam.autoSleepDevice < 0
                }
            }
        }

        // Microphone during sleep (issue #10).
        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: micSleepCol.implicitHeight + 24
            ColumnLayout {
                id: micSleepCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Text { text: "Microphone during sleep"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13 }
                Text {
                    text: "The camera mutes its microphones while asleep by default — mic-only calls go silent when it "
                        + "dozes off. \"On\" keeps the mic live during sleep. \"Device\" means this app won't "
                        + "change the camera's current setting (it does not restore a previous one)."
                    color: Theme.dimmer; font.family: Theme.sans; font.pixelSize: 12
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Segmented {
                    Layout.alignment: Qt.AlignLeft
                    options: ["Device", "Muted", "On"]
                    currentIndex: cam.micSleepIndex
                    onActivated: (i) => cam.micSleepIndex = i
                }
                KeyValue {
                    Layout.fillWidth: true
                    keyWidth: 118
                    key: "device reports"
                    value: cam.micSleepDevice === 1 ? "mic on in sleep"
                         : cam.micSleepDevice === 0 ? "mic muted in sleep" : "—"
                    unknown: cam.micSleepDevice < 0
                }
            }
        }
    }
}
