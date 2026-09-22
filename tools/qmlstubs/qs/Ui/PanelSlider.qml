import QtQuick
Item {
    property QtObject bar: null
    property real minimum: 0
    property real maximum: 1
    property real step: 1
    property bool integer: false
    property real value: 0
    property real liveValue: value
    property color trackColor: "gray"
    property color fillColor: "white"
    property color knobColor: "white"
    signal moved(real value)
    signal released(real value)
}
