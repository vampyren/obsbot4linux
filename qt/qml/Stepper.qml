// − [value] +  (with an optional reset button, used for zoom: − [1.30x] + 1x).
// The −/+ buttons can carry SDK action names so they show real busy/success
// feedback via ActionButton's cam.commandResult binding.
import QtQuick
import QtQuick.Layouts
import Obsbot

RowLayout {
    id: root
    property string valueText: ""
    property string decAction: ""
    property string incAction: ""
    property bool showReset: false
    property string resetText: "1x"
    signal decrement()
    signal increment()
    signal reset()

    spacing: 4

    ActionButton {
        text: "−"   // minus sign
        variant: "secondary"
        enabled: root.enabled
        hpad: 10
        action: root.decAction
        onClicked: root.decrement()
    }
    Rectangle {
        Layout.preferredWidth: Math.max(64, val.implicitWidth + 18)
        Layout.preferredHeight: 34
        radius: Theme.rControl
        color: Qt.rgba(1, 1, 1, 0.03)
        border.width: 1
        border.color: Theme.border
        Text {
            id: val
            anchors.centerIn: parent
            text: root.valueText
            color: Theme.fg
            font.family: Theme.mono
            font.pixelSize: 13
        }
    }
    ActionButton {
        text: "+"
        variant: "secondary"
        enabled: root.enabled
        hpad: 10
        action: root.incAction
        onClicked: root.increment()
    }
    ActionButton {
        visible: root.showReset
        text: root.resetText
        variant: "ghost"
        enabled: root.enabled
        action: "zoom 1x"
        onClicked: root.reset()
    }
}
