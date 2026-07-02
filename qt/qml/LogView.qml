// Mono activity log, colored per kind, auto-scrolling to newest.
//   net  cyan   ok  emerald   warn  amber   cmd  coral   sys  grey
import QtQuick
import QtQuick.Layouts
import Obsbot

ListView {
    id: root
    property var logModel
    model: logModel
    clip: true
    spacing: 1
    boundsBehavior: Flickable.StopAtBounds

    function kindColor(k) {
        return k === "ok" ? Theme.live
             : k === "net" ? Theme.busy
             : k === "warn" ? Theme.degraded
             : k === "cmd" ? Theme.accentSoft
             : Theme.dim
    }

    delegate: RowLayout {
        id: line
        required property string kind
        required property string msg
        required property string ts
        width: ListView.view ? ListView.view.width : 0
        spacing: 8
        Text {
            text: line.ts
            color: Theme.dimmer
            font.family: Theme.mono
            font.pixelSize: 12
        }
        Text {
            text: line.kind.toUpperCase()
            Layout.preferredWidth: 38
            color: root.kindColor(line.kind)
            font.family: Theme.mono
            font.pixelSize: 12
        }
        // TextEdit (read-only) instead of Text so the message is mouse-selectable
        // for copy/paste; the Log page also has a "Copy" button for the full log.
        TextEdit {
            text: line.msg
            readOnly: true
            selectByMouse: true
            color: Theme.dim
            selectionColor: Theme.accent
            selectedTextColor: "#0a0a0b"
            font.family: Theme.mono
            font.pixelSize: 12
            Layout.fillWidth: true
            wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
        }
    }

    onCountChanged: positionViewAtEnd()
}
