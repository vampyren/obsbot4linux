// The pinned viewfinder surface, now with the EMBEDDED live preview (issue #1):
// a QtMultimedia VideoOutput fed real UVC frames by PreviewEngine (context
// property `preview`). No fake feed, ever — when the stream is not running the
// surface is the plain dark panel, and capture errors stop the stream with the
// reason in the log rather than freezing a frame.
//
// Four connection states are rendered: connected (rule-of-thirds grid + LIVE
// pill + readout), asleep (moon + Wake), discovering (scan rings), disconnected
// (video-off + Rescan).
import QtQuick
import QtQuick.Layouts
import QtMultimedia
import Obsbot

Rectangle {
    id: root
    radius: Theme.rSurface
    color: "#050506"
    border.width: 1
    border.color: Theme.border
    clip: true

    readonly property bool live: cam.connected && !cam.asleep

    // faint accent glow behind the (notional) subject — hidden once real video runs
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.7; height: width; radius: width / 2
        color: Theme.accent
        opacity: (root.live && !preview.active) ? 0.05 : 0.0
        Behavior on opacity { NumberAnimation { duration: Theme.base } }
    }

    // the embedded live stream (real frames from PreviewEngine; overlays sit on top)
    VideoOutput {
        id: vout
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
        visible: preview.active
    }
    Component.onCompleted: preview.videoSink = vout.videoSink
    // Stop the stream before this item's VideoOutput/sink is destroyed (the QML
    // engine is torn down before PreviewEngine at shutdown) — don't leave the
    // capture session pointing at a dead sink.
    Component.onDestruction: preview.stop()

    // dim wash while asleep (below the overlays so the Wake button stays bright)
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: (cam.connected && cam.asleep) ? 0.35 : 0.0
        Behavior on opacity { NumberAnimation { duration: Theme.base } }
    }

    // rule-of-thirds grid (only meaningful when live)
    Item {
        anchors.fill: parent
        anchors.margins: 14
        visible: root.live
        Repeater {
            model: 2
            Rectangle {
                required property int index
                width: 1; height: parent.height
                x: parent.width * (index + 1) / 3
                color: Qt.rgba(1, 1, 1, 0.06)
            }
        }
        Repeater {
            model: 2
            Rectangle {
                required property int index
                height: 1; width: parent.width
                y: parent.height * (index + 1) / 3
                color: Qt.rgba(1, 1, 1, 0.06)
            }
        }
    }

    // LIVE pill (top-left)
    Row {
        visible: root.live
        anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 12
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 7; height: 7; radius: 3.5; color: Theme.offline
            SequentialAnimation on opacity {
                running: root.live; loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.3; duration: 700 }
                NumberAnimation { from: 0.3; to: 1.0; duration: 700 }
            }
        }
        Text { text: "LIVE"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 11; font.letterSpacing: 1 }
    }

    // ZOOM readout (bottom-left)
    Text {
        visible: root.live
        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
        text: "ZOOM " + (cam.zoomValid ? cam.zoom.toFixed(2) + "x" : "—")
        color: Theme.dim
        font.family: Theme.mono
        font.pixelSize: 11
    }

    // preview controls (bottom-right). Embedded is the primary path; ffplay is
    // the external-window fallback. Both capture the same UVC node, so starting
    // one stops the other (mutual exclusion kept explicit here in QML).
    Row {
        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 12
        spacing: 8
        // ffplay fallback (external window) — for boxes where the embedded
        // QtMultimedia path misbehaves.
        ActionButton {
            text: "ffplay ↗"
            variant: "ghost"
            enabled: cam.previewAvailable && root.live
            onClicked: { preview.stop(); cam.launchPreview() }
        }
        // embedded preview toggle; stays enabled while active so a running
        // stream can always be stopped, even mid-sleep/disconnect.
        ActionButton {
            text: preview.active ? "stop preview ■" : "preview ▶"
            variant: preview.active ? "secondary" : "ghost"
            enabled: (preview.available && root.live) || preview.active
            onClicked: { if (!preview.active) cam.stopPreview(); preview.toggle() }
        }
    }

    // ----- overlays for non-live states -----

    // Discovering: concentric scan rings
    Item {
        anchors.centerIn: parent
        visible: cam.discovering
        width: 160; height: 200
        Repeater {
            model: 3
            Rectangle {
                required property int index
                anchors.horizontalCenter: parent.horizontalCenter
                y: 40
                width: 20; height: 20; radius: 10
                color: "transparent"
                border.width: 2; border.color: Theme.degraded
                SequentialAnimation on scale {
                    running: cam.discovering; loops: Animation.Infinite
                    PauseAnimation { duration: index * 500 }
                    NumberAnimation { from: 0.4; to: 4.5; duration: 1500; easing.type: Easing.OutCubic }
                }
                SequentialAnimation on opacity {
                    running: cam.discovering; loops: Animation.Infinite
                    PauseAnimation { duration: index * 500 }
                    NumberAnimation { from: 0.6; to: 0.0; duration: 1500 }
                }
            }
        }
        Column {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 4
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Discovering…"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13 }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "polling /dev/video0 · USB"; color: Theme.dimmer; font.family: Theme.mono; font.pixelSize: 11 }
        }
    }

    // Disconnected: video-off glyph + Rescan
    Column {
        anchors.centerIn: parent
        visible: cam.connState === 0
        spacing: 12
        RingGlyph { anchors.horizontalCenter: parent.horizontalCenter; size: 34; color: Theme.offline }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "No device found"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13 }
        ActionButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Rescan"; variant: "secondary"
            onClicked: cam.rescan()
        }
    }

    // Asleep: moon glyph + Wake
    Column {
        anchors.centerIn: parent
        visible: cam.connected && cam.asleep
        spacing: 12
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "☾"; color: Theme.degraded; font.pixelSize: 34 }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Camera asleep"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13 }
        ActionButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Wake"; variant: "primary"; action: "wake"
            onClicked: cam.wake()
        }
    }
}
