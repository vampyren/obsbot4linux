// Left icon rail: ring glyph on top, five nav items, LINK heartbeat at the
// bottom. Active item shows a coral edge bar + tint. currentIndex is an input
// (bound to the window's page); tapping emits navigate(i).
//
// NOTE: the handoff specifies lucide 2px-stroke icons. Bundling the lucide SVG
// set is a follow-up; these geometric glyphs are placeholders (not emoji) so the
// rail reads correctly until the icons are added.
import QtQuick
import Obsbot

Rectangle {
    id: root
    property int currentIndex: 0
    signal navigate(int i)

    color: Qt.rgba(1, 1, 1, 0.02)

    readonly property var labels: ["Control", "Image", "Track", "Presets", "Settings", "Log"]
    readonly property var glyphs: ["▦", "◐", "⊕", "▤", "⚙", "⌗"]

    Rectangle {   // right hairline
        anchors.right: parent.right
        width: 1; height: parent.height
        color: Theme.border
    }

    RingGlyph {
        id: ring
        size: 26
        anchors.top: parent.top
        anchors.topMargin: 16
        anchors.horizontalCenter: parent.horizontalCenter
    }

    Column {
        anchors.top: ring.bottom
        anchors.topMargin: 22
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 8
        Repeater {
            model: 6
            delegate: Item {
                id: item
                required property int index
                width: 52; height: 50

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: 9
                    color: item.index === root.currentIndex ? Theme.accentTint
                          : (h.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    Behavior on color { ColorAnimation { duration: Theme.fast } }
                }
                Rectangle {   // coral edge bar for the active item
                    visible: item.index === root.currentIndex
                    width: 3; height: 30; radius: 1.5
                    color: Theme.accent
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                }
                Column {
                    anchors.centerIn: parent
                    spacing: 3
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.glyphs[item.index]
                        color: item.index === root.currentIndex ? Theme.accentSoft : Theme.dim
                        font.pixelSize: 16
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.labels[item.index]
                        color: item.index === root.currentIndex ? Theme.accentSoft : Theme.dim
                        font.family: Theme.mono
                        font.pixelSize: 9
                    }
                }
                HoverHandler { id: h }
                TapHandler { onTapped: root.navigate(item.index) }
            }
        }
    }

    Column {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 4
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 8; height: 8; radius: 4
            color: cam.connected ? Theme.live : cam.discovering ? Theme.degraded : Theme.offline
            SequentialAnimation on opacity {
                running: cam.connected; loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.3; duration: 900 }
                NumberAnimation { from: 0.3; to: 1.0; duration: 900 }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "LINK"
            color: Theme.dimmer
            font.family: Theme.mono
            font.pixelSize: 8
            font.letterSpacing: 1
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "v" + cam.appVersion
            color: Theme.dimmer
            font.family: Theme.mono
            font.pixelSize: 8
        }
    }
}
