// Window canvas M0 spike S7: search prompt as a layer-shell surface with exclusive keyboard focus.
// Run: quickshell -p tools/spike_search  (SPIKE_SEARCH_OUTPUT picks the output, default SPIKE-canvas)
// Prints "search <text>" on every change and "search-accept <text>" on Enter, then exits.
import QtQuick
import Quickshell
import Quickshell.Wayland

ShellRoot {
    PanelWindow {
        id: prompt
        screen: Quickshell.screens.find(s => s.name === (Quickshell.env("SPIKE_SEARCH_OUTPUT") || "SPIKE-canvas")) || Quickshell.screens[0]
        anchors.top: true
        margins.top: 200
        implicitWidth: 720
        implicitHeight: 72
        color: "transparent"
        WlrLayershell.namespace: "omarchy-xr-search"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
        exclusionMode: ExclusionMode.Ignore

        Rectangle {
            anchors.fill: parent
            radius: 12
            color: "#1e1e2e"
            border.color: "#89b4fa"
            TextInput {
                id: field
                anchors.fill: parent
                anchors.margins: 18
                color: "#cdd6f4"
                font.pixelSize: 28
                focus: true
                onTextChanged: console.log("search " + text)
                Keys.onReturnPressed: { console.log("search-accept " + text); Qt.quit() }
                Keys.onEscapePressed: { console.log("search-cancel"); Qt.quit() }
            }
        }
    }
}
