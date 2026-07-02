// Penelope console design tokens — single source of truth for the whole UI.
// Values are the exact tokens from the Command Center handoff.
pragma Singleton
import QtQuick

QtObject {
    // ----- color -----
    readonly property color bg:          "#08090b"
    readonly property color panel:       Qt.rgba(1, 1, 1, 0.025)
    readonly property color panelHot:    Qt.rgba(1, 1, 1, 0.05)
    readonly property color border:      Qt.rgba(1, 1, 1, 0.07)
    readonly property color borderHover: Qt.rgba(1, 1, 1, 0.16)

    readonly property color fg:     "#f4f4f5"
    readonly property color dim:    "#a1a1aa"
    readonly property color dimmer: "#71717a"

    readonly property color accent:     "#e06c75"
    readonly property color accentSoft: "#ea8b92"
    readonly property color accentDeep: "#c14b55"
    readonly property color accentTint: Qt.rgba(224 / 255, 108 / 255, 117 / 255, 0.13)
    readonly property color accentRing: Qt.rgba(224 / 255, 108 / 255, 117 / 255, 0.55)

    // status tones
    readonly property color live:     "#10b981"  // emerald
    readonly property color busy:     "#06b6d4"  // cyan
    readonly property color degraded: "#f59e0b"  // amber
    readonly property color offline:  "#f43f5e"  // rose
    readonly property color unknown:  "#71717a"

    // ----- typography -----
    readonly property string mono: "JetBrains Mono"   // system fallback if unbundled
    readonly property string sans: "Inter"

    // ----- radii -----
    readonly property int rControl: 8
    readonly property int rSurface: 10
    readonly property int rPill:    999
    readonly property int rChip:    3

    // ----- spacing (4px base) -----
    readonly property int s1: 4
    readonly property int s2: 8
    readonly property int s3: 12
    readonly property int s4: 16
    readonly property int s5: 20
    readonly property int s6: 24
    readonly property int s8: 32

    // ----- motion -----
    readonly property int fast: 150
    readonly property int base: 220
}
