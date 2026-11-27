// Image & Exposure.
//   * Brightness / Contrast / Saturation / Sharpness — WIRED (SDK 0–100, live
//     values read on connect, applied on release).
//   * HDR — NOT available on Tiny 3 (SDK HDR/WDR is for tiny4k/tiny2/meet/tail-
//     air; Tiny 3 reports hdr_support=0 in every mode). Shown as an honest note.
//   * White balance / Color temp / Exposure — still capability-gated OFF (not
//     verified on the Tiny 3 SDK), shown disabled with an honest hint.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Obsbot

RowLayout {
    id: root
    spacing: Theme.s4

    // A live, coral-styled slider. Applies to the device on release (few SDK
    // calls), and reflects the device's current value via `boundValue`.
    component ImageSlider: RowLayout {
        id: row
        property string label: ""
        property string param: ""
        property int boundValue: 50
        spacing: 10
        Text { text: row.label; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13; Layout.preferredWidth: 96 }
        Slider {
            id: sl
            Layout.fillWidth: true
            enabled: cam.connected
            from: 0; to: 100; stepSize: 1
            value: row.boundValue
            onPressedChanged: if (!pressed) cam.setImageParam(row.param, Math.round(value))
            opacity: enabled ? 1 : 0.4

            background: Rectangle {
                x: sl.leftPadding
                y: sl.topPadding + sl.availableHeight / 2 - height / 2
                width: sl.availableWidth; height: 5; radius: 2.5
                color: Qt.rgba(1, 1, 1, 0.12)
                Rectangle {
                    width: sl.position * parent.width; height: parent.height; radius: 2.5
                    color: Theme.accent
                }
            }
            handle: Rectangle {
                x: sl.leftPadding + sl.position * (sl.availableWidth - width)
                y: sl.topPadding + sl.availableHeight / 2 - height / 2
                width: 18; height: 18; radius: 9
                color: sl.pressed ? Theme.accentDeep : Theme.accentSoft
                border.width: 1; border.color: Theme.accentDeep
            }
        }
        // Value pill — prominent, coral when dragging, tabular so it doesn't jitter.
        Rectangle {
            Layout.preferredWidth: 40; Layout.preferredHeight: 24
            radius: Theme.rControl
            color: sl.pressed ? Theme.accentTint : Qt.rgba(1, 1, 1, 0.04)
            border.width: 1; border.color: sl.pressed ? Theme.accentRing : Theme.border
            Text {
                anchors.centerIn: parent
                text: Math.round(sl.value)
                color: sl.pressed ? Theme.accentSoft : Theme.fg
                font.family: Theme.mono; font.pixelSize: 13
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignTop
        spacing: Theme.s3

        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: col.implicitHeight + 28
            ColumnLayout {
                id: col
                anchors.fill: parent
                anchors.margins: 14
                spacing: Theme.s3
                RowLayout {
                    Layout.fillWidth: true
                    SectionLabel { text: "Picture" }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: "Reset defaults"; variant: "secondary"
                        enabled: cam.connected
                        onClicked: cam.resetImageDefaults()
                    }
                }
                ImageSlider { label: "Brightness"; param: "brightness"; boundValue: cam.brightness }
                ImageSlider { label: "Contrast";   param: "contrast";   boundValue: cam.contrast }
                ImageSlider { label: "Saturation"; param: "saturation"; boundValue: cam.saturation }
                ImageSlider { label: "Sharpness";  param: "sharpness";  boundValue: cam.sharpness }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // HDR — the SDK's HDR/WDR control is documented for tiny4k/tiny2/
                // meet/tail-air, NOT tiny3, and the Tiny 3 reports hdr_support=0
                // in every mode (incl. 1080p30). So HDR is not exposed on Tiny 3
                // via this SDK; shown as an honest note rather than a dead toggle.
                // (Tip: 1080p30 already looks crisper than 1080p60 simply because
                //  30fps compresses less — that's framerate, not HDR.)
                RowLayout {
                    spacing: 10
                    Text { text: "HDR"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 12; Layout.preferredWidth: 96 }
                    Text {
                        text: "not available on the Tiny 3 (not exposed by the SDK)"
                        color: Theme.dimmer; font.family: Theme.mono; font.pixelSize: 11
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // White balance & color temperature (verified on Tiny 3 Lite)
        GlassPanel {
            Layout.fillWidth: true
            implicitHeight: wbCol.implicitHeight + 28
            ColumnLayout {
                id: wbCol
                anchors.fill: parent
                anchors.margins: 14
                spacing: Theme.s3

                SectionLabel { text: "White balance" }

                RowLayout {
                    spacing: 10
                    Text { text: "Mode"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13; Layout.preferredWidth: 96 }
                    Segmented {
                        Layout.fillWidth: true
                        enabled: cam.connected && cam.capWb
                        options: ["Auto", "Manual"]
                        currentIndex: cam.wbMode
                        onActivated: (i) => cam.setWbMode(i)
                    }
                }

                RowLayout {
                    id: tempRow
                    spacing: 10
                    enabled: cam.connected && cam.capWb && cam.wbMode === 1
                    opacity: enabled ? 1 : 0.4
                    Text { text: "Color temp"; color: Theme.fg; font.family: Theme.mono; font.pixelSize: 13; Layout.preferredWidth: 96 }
                    Slider {
                        id: tempSl
                        Layout.fillWidth: true
                        enabled: tempRow.enabled
                        from: 2800; to: 7500; stepSize: 50
                        value: cam.wbTemp
                        onPressedChanged: if (!pressed) cam.setWbTemp(Math.round(value))

                        background: Rectangle {
                            x: tempSl.leftPadding
                            y: tempSl.topPadding + tempSl.availableHeight / 2 - height / 2
                            width: tempSl.availableWidth; height: 5; radius: 2.5
                            color: Qt.rgba(1, 1, 1, 0.12)
                            Rectangle {
                                width: tempSl.position * parent.width; height: parent.height; radius: 2.5
                                color: Theme.accent
                            }
                        }
                        handle: Rectangle {
                            x: tempSl.leftPadding + tempSl.position * (tempSl.availableWidth - width)
                            y: tempSl.topPadding + tempSl.availableHeight / 2 - height / 2
                            width: 18; height: 18; radius: 9
                            color: tempSl.pressed ? Theme.accentDeep : Theme.accentSoft
                            border.width: 1; border.color: Theme.accentDeep
                        }
                    }
                    Rectangle {
                        Layout.preferredWidth: 62; Layout.preferredHeight: 24
                        radius: Theme.rControl
                        color: tempSl.pressed ? Theme.accentTint : Qt.rgba(1, 1, 1, 0.04)
                        border.width: 1; border.color: tempSl.pressed ? Theme.accentRing : Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: Math.round(tempSl.value) + "K"
                            color: tempSl.pressed ? Theme.accentSoft : Theme.fg
                            font.family: Theme.mono; font.pixelSize: 13
                        }
                    }
                }
            }
        }

        // still-gated controls (Exposure)
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.rControl
            color: Qt.rgba(Theme.degraded.r, Theme.degraded.g, Theme.degraded.b, 0.10)
            border.width: 1
            border.color: Qt.rgba(Theme.degraded.r, Theme.degraded.g, Theme.degraded.b, 0.4)
            implicitHeight: banner.implicitHeight + 20
            Text {
                id: banner
                anchors.fill: parent; anchors.margins: 10
                text: "Exposure control is disabled — " + cam.capUnverifiedReason
                color: Theme.degraded
                font.family: Theme.mono; font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }

    // reference preview — fixed size, roomy enough to judge image tweaks by
    // (300x220 was too tiny; fill-the-page swallowed the controls — don't).
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
