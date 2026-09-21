import QtQuick
Item {
    property real minimum: 0
    property real maximum: 1
    property real step: 1
    property bool integer: false
    property real value: 0
    property real liveValue: value
    signal moved(real value)
    signal released(real value)
}
