// The pinned viewfinder surface. It is a PLACEHOLDER — there is no embedded
// video in this pass and no fake feed. The region is a single resizable surface
// with overlays layered on top, so a real VideoOutput / GStreamer sink can drop
// in later without a redesign. The ffplay affordance opens REAL frames in a
// separate external window.
//
// Four connection states are rendered: connected (rule-of-thirds grid + LIVE
// pill + readout), asleep (moon + Wake), discovering (scan rings), disconnected
// (video-off + Rescan).
import QtQuick
import QtQuick.Layouts
import Obsbot

Rectangle {
    id: root
    radius: Theme.rSurface
    color: "#050506"
    border.width: 1
    border.color: Theme.border
    clip: true

    readonly property bool live: cam.connected && !cam.asleep

    // faint accent glow behind the (notional) subject
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.7; height: width; radius: width / 2
        color: Theme.accent
        opacity: root.live ? 0.05 : 0.0
        Behavior on opacity { NumberAnimation { duration: Theme.base } }
    }

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

    // ffplay affordance (bottom-right)
    ActionButton {
        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 12
        text: "ffplay ↗"
        variant: "ghost"
        enabled: cam.previewAvailable
        onClicked: cam.launchPreview()
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
