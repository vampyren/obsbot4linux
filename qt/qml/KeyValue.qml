// A label→value row in the STATUS panel: dim mono key, coral-soft mono value
// (dimmed italic when unknown).
import QtQuick
import QtQuick.Layouts
import Obsbot

RowLayout {
    id: root
    property string key: ""
    property string value: ""
    property bool unknown: false
    property color valueColor: Theme.accentSoft
    spacing: 8

    Text {
        text: root.key
        color: Theme.dim
        font.family: Theme.mono
        font.pixelSize: 12
        Layout.preferredWidth: 92
    }
    Text {
        text: root.value
        color: root.unknown ? Theme.dimmer : root.valueColor
        font.family: Theme.mono
        font.pixelSize: 13
        font.italic: root.unknown
        Layout.fillWidth: true
        elide: Text.ElideRight
    }
}
