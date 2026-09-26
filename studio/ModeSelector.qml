import QtQuick
import QtQuick.Controls

// Render mode toggle. Plain QtQuick so qmltestrunner can load it without Quickshell.
// Segments only report a pick; the Studio switches the mode after the backend agrees.
Item {
    id: root
    property string mode: "monitors"
    property bool locked: false
    property string hint: ""
    property color accent: palette.highlight
    property color foreground: palette.text
    readonly property var options: [{value:"monitors",label:"Virtual monitors"},{value:"canvas",label:"Window canvas"}]
    signal picked(string mode)
    implicitWidth: layout.implicitWidth
    implicitHeight: layout.implicitHeight
    activeFocusOnTab: true
    function available(value) {
        return !locked && (value !== "canvas" || hint === "");
    }
    function step(offset) {
        var index = options.findIndex(function(o) { return o.value === root.mode; }) + offset;
        if (index < 0 || index >= options.length || !available(options[index].value)) return;
        picked(options[index].value);
    }
    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)
    Column {
        id: layout
        width: root.width
        spacing: 6
        Row {
            id: segments
            width: parent.width
            Repeater {
                model: root.options
                AbstractButton {
                    id: segment
                    required property var modelData
                    required property int index
                    objectName: "mode-" + modelData.value
                    width: Math.max(implicitWidth, segments.width / root.options.length)
                    implicitWidth: label.implicitWidth + 28
                    implicitHeight: label.implicitHeight + 16
                    checkable: false
                    checked: root.mode === modelData.value
                    enabled: root.available(modelData.value)
                    opacity: enabled ? 1 : .4
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData.label
                    Accessible.checked: checked
                    Accessible.onPressAction: if (enabled) clicked()
                    onClicked: if (modelData.value !== root.mode) root.picked(modelData.value)
                    background: Rectangle {
                        color: segment.checked ? Qt.alpha(root.accent, .18) : "transparent"
                        border.width: 1
                        border.color: segment.checked || segment.activeFocus ? root.accent : Qt.alpha(root.foreground, .25)
                    }
                    contentItem: Text {
                        id: label
                        text: segment.modelData.label
                        textFormat: Text.PlainText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: root.foreground
                        font.bold: segment.checked
                    }
                }
            }
        }
        Text {
            objectName: "mode-hint"
            width: parent.width
            visible: root.hint !== ""
            text: root.hint
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: Qt.alpha(root.foreground, .68)
        }
    }
}
