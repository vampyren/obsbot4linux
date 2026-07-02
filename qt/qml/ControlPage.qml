// Live Control — pinned viewfinder + transport on the left; a status panel and
// an independently-scrolling controls panel on the right (only the right column
// scrolls; the viewfinder is always visible, per the product requirement).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Obsbot

RowLayout {
    id: root
    spacing: Theme.s4

    // ---------------- LEFT: viewfinder (pinned) + transport ----------------
    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 360
        spacing: Theme.s3

        Viewfinder {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: 58
            RowLayout {
                anchors.fill: parent
                anchors.margins: 11
                spacing: 8
                ActionButton {
                    text: "Wake"; variant: "secondary"; action: "wake"
                    enabled: cam.connected
                    active: cam.connected && !cam.asleep; tone: Theme.live
                    onClicked: cam.wake()
                }
                ActionButton {
                    text: "Sleep"; variant: "secondary"; action: "sleep"
                    enabled: cam.connected
                    active: cam.connected && cam.asleep; tone: Theme.degraded
                    onClicked: cam.sleep()
                }
                ActionButton {
                    text: "Center"; variant: "secondary"; action: "center"
                    enabled: cam.connected
                    onClicked: cam.center()
                }
                Item { Layout.fillWidth: true }
                SectionLabel { text: "Zoom"; Layout.alignment: Qt.AlignVCenter }
                Stepper {
                    enabled: cam.connected
                    valueText: cam.zoomValid ? cam.zoom.toFixed(2) + "x" : "—"
                    decAction: "zoom out"; incAction: "zoom in"
                    showReset: true
                    onDecrement: cam.zoomOut()
                    onIncrement: cam.zoomIn()
                    onReset: cam.zoomReset()
                }
            }
        }
    }

    // ---------------- RIGHT: status + controls (scrolls) ----------------
    ColumnLayout {
        Layout.preferredWidth: 300
        Layout.maximumWidth: 320
        Layout.fillHeight: true
        spacing: Theme.s3

        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: statusCol.implicitHeight + 28
            ColumnLayout {
                id: statusCol
                anchors.fill: parent
                anchors.margins: 14
                spacing: 6
                SectionLabel { text: "Status" }
                KeyValue { key: "product"; value: cam.connected ? cam.product + " (enum " + cam.enumId + ")" : "—"; unknown: !cam.connected }
                KeyValue { key: "SN"; value: cam.connected ? cam.sn : "—"; unknown: !cam.connected }
                KeyValue { key: "firmware"; value: cam.connected ? cam.firmware : "—"; unknown: !cam.connected }
                KeyValue { key: "mode"; value: cam.connected ? cam.mode : "—"; unknown: !cam.connected }
                KeyValue {
                    key: "run state"
                    value: cam.connected ? (cam.runState === 1 ? "Active" : cam.runState === 2 ? "Sleep" : "Unknown") : "—"
                    valueColor: cam.runState === 1 ? Theme.live : cam.runState === 2 ? Theme.degraded : Theme.dimmer
                    unknown: !cam.connected
                }
                KeyValue { key: "zoom"; value: cam.zoomValid ? cam.zoom.toFixed(2) + "x  (1.0–2.0)" : "—"; unknown: !cam.zoomValid }
                KeyValue { key: "AI / track"; value: cam.connected ? cam.aiModeName : "—"; unknown: !cam.connected }
                KeyValue { key: "video fps"; value: (cam.connected && cam.fps > 0) ? cam.fps + " fps" : "—"; unknown: !(cam.connected && cam.fps > 0) }
                KeyValue { key: "preview res"; value: cam.previewRes; valueColor: Theme.accentSoft }
            }
        }

        GlassPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Flickable {
                id: controlsFlick
                anchors.fill: parent
                anchors.margins: 14
                contentHeight: ctrlCol.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    id: ctrlScroll
                    policy: ScrollBar.AsNeeded
                    // Auto-hide: only visible while actively scrolling or hovered.
                    opacity: (controlsFlick.moving || ctrlScroll.pressed || ctrlScroll.hovered) ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 250 } }
                }
                ColumnLayout {
                    id: ctrlCol
                    // leave room on the right so controls never sit under the scrollbar
                    width: controlsFlick.width - 10
                    spacing: Theme.s3

                    SectionLabel { text: "Controls" }
                    PtzPad { Layout.alignment: Qt.AlignHCenter; enabled: cam.connected }

                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        SectionLabel { text: "Step"; Layout.preferredWidth: 46 }
                        Segmented {
                            Layout.fillWidth: true
                            options: ["2°", "5°", "10°"]
                            currentIndex: cam.moveStepDeg === 2 ? 0 : cam.moveStepDeg === 10 ? 2 : 1
                            onActivated: (i) => cam.moveStepDeg = (i === 0 ? 2 : i === 2 ? 10 : 5)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        SectionLabel { text: "Speed"; Layout.preferredWidth: 46 }
                        Segmented {
                            Layout.fillWidth: true
                            options: ["Slow", "Medium"]
                            currentIndex: cam.speedMode
                            onActivated: (i) => cam.speedMode = i
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        SectionLabel { text: "FOV"; Layout.preferredWidth: 46 }
                        Segmented {
                            Layout.fillWidth: true
                            enabled: cam.connected && cam.capFov
                            options: ["Wide", "Med", "Narrow"]
                            currentIndex: cam.fovIndex
                            onActivated: (i) => cam.fovIndex = i
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        ToggleChip {
                            Layout.fillWidth: true
                            text: "AI Track"; tone: Theme.busy
                            enabled: cam.connected && cam.capAi
                            busy: cam.aiPending
                            checked: cam.aiTracking
                            onToggled: (c) => cam.setAiTracking(c)
                        }
                        ToggleChip {
                            Layout.fillWidth: true
                            text: "Face focus"; tone: Theme.busy
                            enabled: cam.connected && cam.capFace
                            busy: cam.aiPending
                            checked: cam.faceFocus
                            onToggled: (c) => cam.setFaceFocus(c)
                        }
                    }

                    // Preview resolution — label on its own line so the four wide
                    // options get the full panel width (a side label overlapped
                    // them). Sets the UVC mode both the embedded preview and the
                    // ffplay fallback request from the camera.
                    SectionLabel { text: "Preview resolution" }
                    Segmented {
                        Layout.fillWidth: true
                        options: ["1080p30", "1080p60", "720p60", "4K30"]
                        currentIndex: cam.previewResIndex
                        onActivated: (i) => cam.previewResIndex = i
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                    // Quick preset recall. Save/rename/clear live on the Presets page.
                    SectionLabel { text: "Presets" }
                    Repeater {
                        model: cam.presets
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: "P" + (modelData.index + 1)
                                color: modelData.set ? Theme.accentSoft : Theme.dim
                                font.family: Theme.mono; font.pixelSize: 12
                                Layout.preferredWidth: 20
                            }
                            Text {
                                text: modelData.set ? modelData.summary : "empty"
                                color: Theme.dim
                                font.family: Theme.mono; font.pixelSize: 11
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            ActionButton { text: "Go"; variant: "secondary"; enabled: cam.connected && modelData.set; onClicked: cam.goPreset(modelData.index) }
                        }
                    }
                }
            }
        }
    }
}
