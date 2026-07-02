// The OBSBOT brand mark: a coral ring + centre dot + faint glow, drawn as
// Rectangles (no raster asset), per the handoff.
import QtQuick
import Obsbot

Item {
    id: root
    property real size: 26
    property color color: Theme.accent
    implicitWidth: size
    implicitHeight: size

    Rectangle {   // glow
        anchors.centerIn: parent
        width: root.size * 1.5; height: width; radius: width / 2
        color: root.color
        opacity: 0.12
    }
    Rectangle {   // ring
        anchors.centerIn: parent
        width: root.size; height: root.size; radius: root.size / 2
        color: "transparent"
        border.width: Math.max(2, root.size * 0.11)
        border.color: root.color
    }
    Rectangle {   // centre dot
        anchors.centerIn: parent
        width: root.size * 0.28; height: width; radius: width / 2
        color: root.color
    }
}
