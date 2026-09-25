pragma Singleton
import QtQuick
QtObject {
    property color foreground: "#e8e6e3"
    property color background: "#111315"
    property color accent: "#7eb6ff"
    property color urgent: "#ff6b6b"
    property color muted: "#707880"
    readonly property QtObject popups: QtObject {
        property color background: "#111315"
        property color text: "#e8e6e3"
        property color border: "#7eb6ff"
    }
}
