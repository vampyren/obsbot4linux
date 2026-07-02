// AI Tracking.
//   * AI Track   — Human tracking (gimbal follows a person; LED turns blue).
//   * Face Track — AiWorkModePortraitTrack, documented in the SDK header as
//     "Portrait tracking for tiny3" — a genuine Tiny3-specific mode, framed
//     tighter on the face. Shares the device's single ai_mode field with AI
//     Track, so the two are mutually exclusive (picking one turns the other off).
//   * Face focus — cameraSetFaceFocusR, autofocus-only. Independent of both
//     tracking modes: it never moves the gimbal or turns the LED blue.
//   * Gesture control — independent, SDK rc logged honestly.
//
// NOTE: an earlier build exposed a "Framing" (Normal/Upper body/Close-up)
// control that reused cameraSetAiModeU's second parameter. That parameter's
// AiSubModeType meaning is documented in the SDK header as "for tiny2" only —
// on Tiny3 it produced inconsistent zoom behavior instead of real framing, so
// it was removed and replaced with the Face Track mode above, which Tiny3
// genuinely documents.
// Advanced sensitivity/motion-speed/zone controls remain capability-gated OFF
// (no verified Tiny 3 setters).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Obsbot

RowLayout {
    id: root
    spacing: Theme.s4

    ColumnLayout {
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignTop
        spacing: Theme.s3

        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: aiCol.implicitHeight + 28
            ColumnLayout {
                id: aiCol
                anchors.fill: parent
                anchors.margins: 14
                spacing: Theme.s3

                RowLayout {
                    Layout.fillWidth: true
                    SectionLabel { text: "AI & Gesture" }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: 9; height: 9; radius: 4.5
                        visible: cam.aiPending
                        color: Theme.busy
                        SequentialAnimation on opacity {
                            running: cam.aiPending; loops: Animation.Infinite
                            NumberAnimation { from: 0.3; to: 1.0; duration: 450 }
                            NumberAnimation { from: 1.0; to: 0.3; duration: 450 }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
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
                        text: "Gesture control"; tone: Theme.live
                        enabled: cam.connected && cam.capGesture
                        checked: cam.gesture
                        onToggled: (c) => cam.setGesture(c)
                    }
                }
                // Face autofocus — independent of tracking, and subtle. Kept
                // separate + honestly labeled so it isn't mistaken for a tracking
                // control that "does nothing".
                ToggleChip {
                    Layout.fillWidth: true
                    text: "Face autofocus"; tone: Theme.live
                    enabled: cam.connected && cam.capFace
                    checked: cam.faceFocus
                    onToggled: (c) => cam.setFaceFocus(c)
                }
                Text {
                    text: "AI Track is the tracking mode: the camera follows a person and keeps their face framed (LED turns blue; manual PTZ is blocked while it's on). "
                        + "Face autofocus just biases focus toward faces — it's independent of tracking and does NOT move the gimbal or the LED, so its effect is subtle. "
                        + "Gesture control (palm to start/stop tracking) is under investigation: the camera handles gestures fine on its own, but goes gesture-deaf while a control app is attached — use the test button below and check the Log page."
                    color: Theme.dimmer
                    font.family: Theme.sans
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                // Diagnostic: pauses ALL periodic SDK traffic for 60 s so we can
                // tell whether our status polling or the mere app session is what
                // suppresses the camera's gesture recognizer.
                ActionButton {
                    text: "Gesture test: pause app traffic 60 s"
                    variant: "ghost"
                    enabled: cam.connected
                    onClicked: cam.gestureQuietTest()
                }
                KeyValue { key: "tracking mode"; value: cam.connected ? cam.aiModeName : "—"; unknown: !cam.connected }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: Theme.rControl
            color: Qt.rgba(Theme.degraded.r, Theme.degraded.g, Theme.degraded.b, 0.10)
            border.width: 1
            border.color: Qt.rgba(Theme.degraded.r, Theme.degraded.g, Theme.degraded.b, 0.4)
            implicitHeight: tbanner.implicitHeight + 20
            Text {
                id: tbanner
                anchors.fill: parent; anchors.margins: 10
                text: "Sensitivity / motion speed / tracking zone are disabled — " + cam.capUnverifiedReason
                color: Theme.degraded
                font.family: Theme.mono; font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: advCol.implicitHeight + 28
            ColumnLayout {
                id: advCol
                anchors.fill: parent
                anchors.margins: 14
                spacing: Theme.s3
                enabled: cam.capTrackingAdvanced   // false
                SectionLabel { text: "Motion speed" }
                Segmented { options: ["Slow", "Std", "Fast"]; currentIndex: 1; enabled: false }
                SectionLabel { text: "Tracking zone" }
                Segmented { options: ["Auto", "Center", "Left", "Right"]; currentIndex: 0; enabled: false }
                RowLayout {
                    spacing: 10
                    Text { text: "Sensitivity"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 12; Layout.preferredWidth: 96 }
                    Slider { Layout.fillWidth: true; from: 0; to: 100; value: 50; enabled: false }
                }
            }
        }
    }

    GlassPanel {
        Layout.preferredWidth: 460
        Layout.maximumWidth: 480
        Layout.preferredHeight: 310
        Layout.alignment: Qt.AlignTop
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8
            SectionLabel { text: "Reference" }
            Viewfinder { Layout.fillWidth: true; Layout.fillHeight: true }
        }
    }
}
