// Connection pill (top-right). CONNECTED = emerald static dot, DISCOVERING =
// amber pulsing dot (the only decorative-looking motion is truthful: it means
// discovery is genuinely running), DISCONNECTED = rose static dot. Derived from
// the SDK-backed cam.connState — no SIM control, per the handoff.
import QtQuick
import Obsbot

Item {
    id: root
    property int state: cam.connState   // 0=Disconnected,1=Discovering,2=Connected
    readonly property color tone: state === 2 ? Theme.live : state === 1 ? Theme.degraded : Theme.offline
    readonly property string label: state === 2 ? "CONNECTED" : state === 1 ? "DISCOVERING" : "DISCONNECTED"

    implicitHeight: 26
    implicitWidth: row.implicitWidth + 22

    Rectangle {
        anchors.fill: parent
        radius: Theme.rPill
        color: Qt.rgba(root.tone.r, root.tone.g, root.tone.b, 0.14)
        border.width: 1
        border.color: Qt.rgba(root.tone.r, root.tone.g, root.tone.b, 0.40)
        Behavior on color { ColorAnimation { duration: Theme.base } }
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 7; height: 7; radius: 3.5
            color: root.tone
            SequentialAnimation on opacity {
                running: root.state === 1; loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.25; duration: 600 }
                NumberAnimation { from: 0.25; to: 1.0; duration: 600 }
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.tone
            font.family: Theme.mono
            font.pixelSize: 11
            font.letterSpacing: 1
        }
    }
}
