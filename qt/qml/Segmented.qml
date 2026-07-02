// Segmented control: a bordered well with an active segment tinted coral.
// currentIndex is an INPUT (bound to the model value); tapping only emits
// activated(index) — the parent updates the source of truth, which re-binds
// currentIndex. This avoids binding-vs-assignment loops.
import QtQuick
import Obsbot

Item {
    id: root
    property var options: []
    property int currentIndex: 0
    signal activated(int index)

    implicitHeight: 32
    implicitWidth: Math.max(120, options.length * 74)
    opacity: enabled ? 1.0 : 0.45

    Rectangle {
        anchors.fill: parent
        radius: Theme.rControl
        color: Qt.rgba(1, 1, 1, 0.03)
        border.width: 1
        border.color: Theme.border
    }

    Row {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 2
        Repeater {
            model: root.options
            delegate: Rectangle {
                id: seg
                required property int index
                required property var modelData
                width: (root.width - 6 - (root.options.length - 1) * 2) / Math.max(1, root.options.length)
                height: root.height - 6
                radius: Theme.rControl - 2
                property bool current: index === root.currentIndex
                color: current ? Theme.accentTint
                               : (segHover.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                border.width: current ? 1 : 0
                border.color: Theme.accentRing
                Behavior on color { ColorAnimation { duration: Theme.fast } }

                Text {
                    anchors.centerIn: parent
                    text: seg.modelData
                    color: seg.current ? Theme.accentSoft : Theme.dim
                    font.family: Theme.mono
                    font.pixelSize: 12
                }
                HoverHandler { id: segHover; enabled: root.enabled }
                TapHandler {
                    enabled: root.enabled
                    onTapped: root.activated(seg.index)
                }
            }
        }
    }
}
