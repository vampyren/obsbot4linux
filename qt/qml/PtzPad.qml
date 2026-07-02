// PTZ pad — a real joystick-style puck (continuous hold-to-move) plus a 3×3
// arrow grid for precise one-shot nudges.
//
// Puck = VELOCITY mode: press, drag, and hold — the camera moves continuously
// toward the deflection while held, and stops the instant you release. Backed
// by CameraWorker::cmdGimbalVelocity (gimbalSpeedCtrlR), gated behind the four
// safety stops from the design spec: stop-on-release (below), stop-on-window-
// blur (Main.qml calls cam.gimbalStop() on window deactivate), stop-on-error
// (worker auto-stops on any non-OK rc), and a deadman timeout (worker auto-
// stops if no fresh command arrives within ~400ms — covers a stalled UI thread).
//
// Arrows = STEP mode: one tap = one bounded, clamped nudge (cam.nudge). Kept as
// the precise fallback alongside the puck.
import QtQuick
import QtQuick.Layouts
import Obsbot

ColumnLayout {
    id: root
    spacing: 12

    component ArrowBtn: Item {
        id: ab
        property int dir: -1
        property bool center: false
        property string glyph: ""
        signal pressed()
        readonly property string _action: center ? "center"
            : ["ptz up", "ptz down", "ptz left", "ptz right"][dir]
        property bool _busy: false
        property string _res: "none"
        width: 44; height: 44
        opacity: enabled ? 1.0 : 0.4

        Rectangle {
            anchors.fill: parent
            radius: Theme.rControl
            color: tap.pressed ? Qt.rgba(1, 1, 1, 0.12)
                  : (h.hovered ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04))
            border.width: 1
            border.color: ab._res === "ok" ? Theme.live
                         : ab._res === "err" ? Theme.offline
                         : ab.center ? Theme.accentDeep : Theme.border
            Behavior on border.color { ColorAnimation { duration: Theme.fast } }
        }
        Text {
            anchors.centerIn: parent
            visible: !ab._busy
            text: ab.glyph
            color: ab.center ? Theme.accentSoft : Theme.fg
            font.pixelSize: 16
        }
        Rectangle {
            anchors.centerIn: parent
            visible: ab._busy
            width: 8; height: 8; radius: 4
            color: Theme.accentSoft
            SequentialAnimation on opacity {
                running: ab._busy; loops: Animation.Infinite
                NumberAnimation { from: 0.3; to: 1.0; duration: 400 }
                NumberAnimation { from: 1.0; to: 0.3; duration: 400 }
            }
        }
        scale: tap.pressed ? 0.94 : 1.0
        Behavior on scale { NumberAnimation { duration: 90 } }

        HoverHandler { id: h; enabled: ab.enabled }
        TapHandler {
            id: tap
            enabled: ab.enabled
            onTapped: { ab._busy = true; ab.pressed() }
        }
        Timer { id: abflash; interval: 800; onTriggered: ab._res = "none" }
        Connections {
            target: cam
            function onCommandResult(a, ok, msg) {
                if (a === ab._action) {
                    ab._busy = false
                    ab._res = ok ? "ok" : "err"
                    abflash.restart()
                }
            }
        }
    }

    // Circular field with crosshair, inner ring, and a DRAGGABLE coral knob.
    // Hold-drag = continuous velocity move; release = immediate stop + spring
    // back to center.
    Item {
        id: field
        Layout.alignment: Qt.AlignHCenter
        width: 150; height: 150

        Rectangle {
            anchors.fill: parent; radius: width / 2
            color: Qt.rgba(1, 1, 1, 0.02)
            border.width: 1; border.color: Theme.border
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.62; height: parent.height * 0.62; radius: width / 2
            color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.10)
        }
        Rectangle { anchors.centerIn: parent; width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.06) }
        Rectangle { anchors.centerIn: parent; width: 1; height: parent.height; color: Qt.rgba(1, 1, 1, 0.06) }
        Rectangle {
            id: knob
            readonly property real homeX: field.width / 2 - width / 2
            readonly property real homeY: field.height / 2 - height / 2
            readonly property real maxDelta: field.width / 2 - width / 2 - 8   // travel radius, matches drag clamp
            width: 26; height: 26; radius: 13
            color: (root.enabled && drag.active) ? Theme.accent
                  : root.enabled ? Theme.accentSoft : Theme.dimmer
            border.width: 1; border.color: Theme.accentDeep
            x: homeX; y: homeY
            Behavior on x { enabled: !drag.active; NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
            Behavior on y { enabled: !drag.active; NumberAnimation { duration: 180; easing.type: Easing.OutBack } }

            DragHandler {
                id: drag
                enabled: root.enabled
                xAxis.minimum: 8; xAxis.maximum: field.width - knob.width - 8
                yAxis.minimum: 8; yAxis.maximum: field.height - knob.height - 8
                onActiveChanged: {
                    if (active) {
                        velocityTick.start()   // begin sending continuous velocity
                    } else {
                        velocityTick.stop()
                        cam.gimbalStop()        // stop-on-release: immediate, unconditional
                        knob.x = knob.homeX     // spring back (animated)
                        knob.y = knob.homeY
                    }
                }
            }
        }

        // Resends the current deflection as a velocity command at a steady rate
        // while held. This IS the "continue" signal the worker's deadman
        // watchdog expects — if this stops firing (e.g. UI thread stalls), the
        // worker auto-stops on its own within ~400ms regardless.
        Timer {
            id: velocityTick
            interval: 90
            repeat: true
            onTriggered: {
                var fracX = (knob.x - knob.homeX) / knob.maxDelta
                var fracY = (knob.y - knob.homeY) / knob.maxDelta
                // Small deadzone so tiny jitter near center doesn't creep the gimbal.
                if (Math.abs(fracX) < 0.12) fracX = 0
                if (Math.abs(fracY) < 0.12) fracY = 0
                cam.gimbalVelocity(fracY, fracX)   // (pitchFrac, yawFrac) — see PtzPad sign notes below
            }
        }

        // Visual-only: arrow taps deflect the knob briefly via _bump(), then this
        // springs it back. Unrelated to the drag/velocity path above.
        Timer { id: springBack; interval: 220; onTriggered: { knob.x = knob.homeX; knob.y = knob.homeY } }
    }

    // Arrow presses deflect the knob toward the direction, then it springs back.
    function _bump(dx, dy) { knob.x = knob.homeX + dx; knob.y = knob.homeY + dy; springBack.restart() }

    GridLayout {
        Layout.alignment: Qt.AlignHCenter
        columns: 3; rowSpacing: 6; columnSpacing: 6

        Item { width: 44; height: 44 }
        ArrowBtn { dir: 0; glyph: "↑"; enabled: root.enabled; onPressed: { cam.nudge(0); root._bump(0, -18) } }
        Item { width: 44; height: 44 }

        ArrowBtn { dir: 2; glyph: "←"; enabled: root.enabled; onPressed: { cam.nudge(2); root._bump(-18, 0) } }
        ArrowBtn { center: true; glyph: "⊙"; enabled: root.enabled; onPressed: cam.center() }
        ArrowBtn { dir: 3; glyph: "→"; enabled: root.enabled; onPressed: { cam.nudge(3); root._bump(18, 0) } }

        Item { width: 44; height: 44 }
        ArrowBtn { dir: 1; glyph: "↓"; enabled: root.enabled; onPressed: { cam.nudge(1); root._bump(0, 18) } }
        Item { width: 44; height: 44 }
    }
}
