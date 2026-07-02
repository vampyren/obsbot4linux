// Translucent glass surface: hairline border that brightens on hover, soft
// radius, no hard drop shadow (elevation reads as light, per the design).
import QtQuick
import Obsbot

Rectangle {
    id: root
    property bool hoverable: true
    radius: Theme.rSurface
    color: Theme.panel
    border.width: 1
    border.color: (hoverable && hover.hovered) ? Theme.borderHover : Theme.border

    Behavior on border.color { ColorAnimation { duration: Theme.fast } }

    // HoverHandler does not steal input from children (unlike a fill MouseArea).
    HoverHandler { id: hover }
}
