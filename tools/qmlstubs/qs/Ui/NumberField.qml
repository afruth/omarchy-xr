import QtQuick
import QtQuick.Controls as QQC
Item {
    id: root
    property string label
    property int from: 0
    property int to: 100
    property int value: 0
    property int stepSize: 1
    property real fieldWidth: 160
    property alias field: spin
    signal modified(int value)
    QQC.SpinBox {
        id: spin
        from: root.from
        to: root.to
        value: root.value
        stepSize: root.stepSize
    }
}
