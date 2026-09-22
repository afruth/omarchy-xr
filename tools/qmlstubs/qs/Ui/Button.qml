import QtQuick
Item {
    property string text
    property string tooltipText
    property bool focusable: true
    property bool bordered: true
    property bool selected: false
    property bool leftAlign: false
    signal clicked()
}
