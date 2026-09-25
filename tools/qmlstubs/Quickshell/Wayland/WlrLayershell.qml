import QtQuick
import QtQuick.Window
Window {
    property string namespace
    property int layer
    property int keyboardFocus
    property int exclusionMode
    property int exclusiveZone
    property QtObject anchors: QtObject {
        property bool top: false
        property bool bottom: false
        property bool left: false
        property bool right: false
    }
    property QtObject margins: QtObject {
        property int top: 0
        property int bottom: 0
        property int left: 0
        property int right: 0
    }
}
