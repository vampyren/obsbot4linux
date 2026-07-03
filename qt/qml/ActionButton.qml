// A command button with the full feedback set Rex asked for: hover, pressed
// (scale + darken), loading (pulsing dot while a command is in flight), and
// success/error (border flashes emerald/rose). If `action` is set it auto-binds
// to cam.commandResult so the busy/result states are driven by the real SDK rc.
import QtQuick
import Obsbot

Item {
    id: root

    property string text: ""
    property string variant: "primary"   // primary | secondary | ghost
    property string action: ""            // matches CameraController.commandResult action
    property bool active: false           // persistent "on" cue (e.g. Wake when awake)
    property color tone: Theme.accent     // tint used when active
    property int hpad: 14
    property alias font: label.font
    // Note: Item has no built-in 'enabled' visual; we shade via opacity below.

    signal clicked()

    implicitHeight: 34
    implicitWidth: Math.max(40, content.implicitWidth + hpad * 2)
    opacity: enabled ? 1.0 : 0.45

    property bool _busy: false
    property string _result: "none"       // none | ok | err

    // Primary uses a real two-stop gradient (lighter sheen on top, deep coral at
    // the bottom) instead of a flat fill — a flat accentSoft (light pink) behind
    // white text read as washed-out/low-contrast, so the base tones here are
    // deliberately darker/more saturated (accent → accentDeep, pressed shifts
    // one step darker still).
    Gradient {
        id: primaryGradient
        orientation: Gradient.Vertical
        GradientStop { position: 0.0; color: tap.pressed ? Theme.accentDeep : Theme.accent }
        GradientStop { position: 1.0; color: tap.pressed ? "#9c3940" : Theme.accentDeep }
    }

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: Theme.rControl
        gradient: root.variant === "primary" ? primaryGradient : null
        color: {
            if (root.variant === "primary") return Theme.accentDeep   // fallback if gradient unsupported
            if (root.active)
                return Qt.rgba(root.tone.r, root.tone.g, root.tone.b, tap.pressed ? 0.22 : 0.15)
            if (root.variant === "secondary")
                return Qt.rgba(1, 1, 1, tap.pressed ? 0.10 : (hover.hovered ? 0.09 : 0.055))
            // ghost: subtle but ALWAYS visible — a fully transparent resting
            // state read as plain text, not a button (Rex).
            return Qt.rgba(1, 1, 1, tap.pressed ? 0.08 : (hover.hovered ? 0.065 : 0.035))
        }
        border.width: 1
        border.color: {
            if (root._result === "ok") return Theme.live
            if (root._result === "err") return Theme.offline
            if (root.active) return root.tone
            if (root.variant === "primary") return "#9c3940"
            if (root.variant === "secondary") return Qt.rgba(1, 1, 1, 0.14)
            return Qt.rgba(1, 1, 1, hover.hovered ? 0.20 : 0.12)   // ghost: hairline, brightens on hover
        }
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
    }

    scale: tap.pressed ? 0.97 : 1.0
    Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: root._busy
            width: 9; height: 9; radius: 4.5
            color: root.variant === "primary" ? "#ffffff" : Theme.accentSoft
            SequentialAnimation on opacity {
                running: root._busy; loops: Animation.Infinite
                NumberAnimation { from: 0.3; to: 1.0; duration: 450 }
                NumberAnimation { from: 1.0; to: 0.3; duration: 450 }
            }
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: {
                if (root.variant === "primary") return "#ffffff"
                if (root._result === "ok") return Theme.live
                if (root._result === "err") return Theme.offline
                if (root.active) return root.tone
                return Theme.fg
            }
            font.family: Theme.mono
            font.pixelSize: 13
            font.weight: Font.Medium
        }
    }

    HoverHandler { id: hover; enabled: root.enabled }
    TapHandler {
        id: tap
        enabled: root.enabled
        onTapped: {
            if (root.action !== "") root._busy = true
            root.clicked()
        }
    }

    Timer { id: flash; interval: 900; onTriggered: root._result = "none" }

    Connections {
        target: cam
        enabled: root.action !== ""
        function onCommandResult(a, ok, msg) {
            if (a === root.action) {
                root._busy = false
                root._result = ok ? "ok" : "err"
                flash.restart()
            }
        }
    }
}
