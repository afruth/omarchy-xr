import QtQuick
Item {
    property string label
    property var value
    property var options
    signal changed(var value)
}
