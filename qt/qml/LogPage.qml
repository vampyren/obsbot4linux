// Activity Log — wide mono log panel + a compact STATUS panel, per the handoff.
import QtQuick
import QtQuick.Layouts
import Obsbot

RowLayout {
    id: root
    property var logModel
    spacing: Theme.s4

    GlassPanel {
        Layout.fillWidth: true
        Layout.fillHeight: true
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: Theme.s2
            RowLayout {
                Layout.fillWidth: true
                SectionLabel { text: "Activity Log" }
                Item { Layout.fillWidth: true }
                ActionButton { text: "Rescan"; variant: "ghost"; onClicked: cam.rescan() }
            }
            LogView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                logModel: root.logModel
            }
        }
    }

    GlassPanel {
        Layout.preferredWidth: 300
        Layout.maximumWidth: 320
        Layout.fillHeight: true
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 6
            SectionLabel { text: "Status" }
            KeyValue { key: "product"; value: cam.connected ? cam.product : "—"; unknown: !cam.connected }
            KeyValue { key: "SN"; value: cam.connected ? cam.sn : "—"; unknown: !cam.connected }
            KeyValue { key: "firmware"; value: cam.connected ? cam.firmware : "—"; unknown: !cam.connected }
            KeyValue { key: "mode"; value: cam.connected ? cam.mode : "—"; unknown: !cam.connected }
            KeyValue {
                key: "run state"
                value: cam.connected ? (cam.runState === 1 ? "Active" : cam.runState === 2 ? "Sleep" : "Unknown") : "—"
                valueColor: cam.runState === 1 ? Theme.live : cam.runState === 2 ? Theme.degraded : Theme.dimmer
                unknown: !cam.connected
            }
            KeyValue { key: "zoom"; value: cam.zoomValid ? cam.zoom.toFixed(2) + "x" : "—"; unknown: !cam.zoomValid }
            KeyValue { key: "AI / track"; value: cam.connected ? cam.aiModeName : "—"; unknown: !cam.connected }
            KeyValue { key: "video fps"; value: (cam.connected && cam.fps > 0) ? cam.fps + " fps" : "—"; unknown: !(cam.connected && cam.fps > 0) }
            KeyValue { key: "resolution"; value: cam.connected ? "not reported by SDK" : "—"; unknown: true }
            Item { Layout.fillHeight: true }
        }
    }
}
