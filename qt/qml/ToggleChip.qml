// A two-state toggle (AI Track / Face focus). Tinted in its tone colour when
// checked; shows a pulsing dot while `busy` (an AI command is in flight). The
// checked state is a bound INPUT — the parent flips it via onToggled → the
// controller → status resync, so the chip never lies about device state.
import QtQuick
import Obsbot

Item {
    id: root
    property string text: ""
    property bool checked: false
    property bool busy: false
    property color tone: Theme.busy
    signal toggled(bool checked)

    implicitHeight: 34
    implicitWidth: Math.max(96, content.implicitWidth + 26)
    opacity: enabled ? 1.0 : 0.45

    Rectangle {
        anchors.fill: parent
        radius: Theme.rControl
        color: root.checked ? Qt.rgba(root.tone.r, root.tone.g, root.tone.b, 0.16)
                            : Qt.rgba(1, 1, 1, hover.hovered ? 0.09 : 0.055)
        border.width: 1
        border.color: root.checked ? root.tone : Qt.rgba(1, 1, 1, 0.14)
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 7
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: root.busy ? 9 : 7
            height: width; radius: width / 2
            color: root.checked || root.busy ? root.tone : Theme.dimmer
            SequentialAnimation on opacity {
                running: root.busy; loops: Animation.Infinite
                NumberAnimation { from: 0.3; to: 1.0; duration: 450 }
                NumberAnimation { from: 1.0; to: 0.3; duration: 450 }
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.checked ? root.tone : Theme.fg
            font.family: Theme.mono
            font.pixelSize: 12
        }
    }

    HoverHandler { id: hover; enabled: root.enabled }
    TapHandler {
        enabled: root.enabled
        onTapped: root.toggled(!root.checked)
    }
}
