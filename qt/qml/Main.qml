// OBSBOT Tiny 3 Command Center — root window.
//
// Left icon rail + a main column (slim top bar with page title + connection
// pill, then the page content). Six pages live in a StackLayout so their state
// (log history, scroll position) survives page switches. The shared log model is
// fed by cam.logLine and consumed by the Control and Log pages.
//
// The window uses standard decorations for reliability across Wayland
// compositors; frameless/CSD with a custom title bar is a follow-up.
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Obsbot

ApplicationWindow {
    id: win
    visible: true
    width: 1320
    height: 940
    minimumWidth: 1040
    minimumHeight: 780
    title: "OBSBOT4Linux"
    color: Theme.bg

    property int page: 0

    // stop-on-window-blur: one of the four required PTZ-velocity safety stops.
    // If the window loses focus/deactivates while the puck is held, halt motion
    // immediately rather than relying solely on the drag-release/deadman paths.
    onActiveChanged: if (!active) cam.gimbalStop()

    readonly property var pageTitles: ["Live Control", "Image & Exposure", "AI Tracking", "Presets", "Settings", "Activity Log"]

    ListModel { id: logModel }

    Connections {
        target: cam
        function onLogLine(kind, msg) {
            logModel.append({ kind: kind, msg: msg, ts: Qt.formatDateTime(new Date(), "hh:mm:ss") })
            if (logModel.count > 500)
                logModel.remove(0, logModel.count - 500)
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        NavRail {
            Layout.preferredWidth: 70
            Layout.fillHeight: true
            currentIndex: win.page
            onNavigate: (i) => win.page = i
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // top bar
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 66
                Layout.leftMargin: Theme.s5
                Layout.rightMargin: Theme.s5
                Layout.topMargin: Theme.s4
                ColumnLayout {
                    spacing: 2
                    SectionLabel { text: "OBSBOT · PTZ Control"; color: Theme.accentSoft }
                    Text {
                        text: win.pageTitles[win.page]
                        color: Theme.fg
                        font.family: Theme.mono
                        font.pixelSize: 18
                    }
                }
                Item { Layout.fillWidth: true }
                StatusPill {}
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.s5
                Layout.rightMargin: Theme.s5
                Layout.topMargin: Theme.s3
                Layout.bottomMargin: Theme.s5
                currentIndex: win.page

                ControlPage {}
                ImagePage {}
                TrackingPage {}
                PresetsPage {}
                SettingsPage {}
                LogPage { logModel: logModel }
            }
        }
    }
}
