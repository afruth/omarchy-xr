import QtQuick
import QtQuick.Controls
import QtTest
import "../../studio"

TestCase {
    name: "EditorRequests"
    when: windowShown
    visible: true
    width: 600; height: 400
    RequestState { id: requests }
    SpinBox {
        id: number
        from: 320; to: 8192; value: 1920
        editable: true; live: false
        enabled: !requests.busy
        contentItem: TextInput {
            text: number.displayText
            validator: number.validator
            readOnly: !number.editable
        }
    }
    Flickable {
        x: 0; y: 100; width: 400; height: 250
        contentWidth: 800; contentHeight: 800
        MouseArea {
            id: canvas
            width: 800; height: 800
            enabled: !requests.busy
            preventStealing: true
            property int movements: 0
            onPositionChanged: if (pressed) movements++
        }
    }
    function init() { requests.reset(); number.value = 1920; }
    function test_poll_preserves_partial_number_and_focus() {
        number.contentItem.forceActiveFocus(); number.contentItem.selectAll();
        keyClick(Qt.Key_1); keyClick(Qt.Key_2);
        compare(number.contentItem.text, "12");
        var id = requests.begin("status");
        compare(requests.busy, false);
        wait(50);
        compare(requests.finish(id), "status");
        verify(number.contentItem.activeFocus);
        compare(number.contentItem.text, "12");
        keyClick(Qt.Key_8); keyClick(Qt.Key_0); keyClick(Qt.Key_Return);
        compare(number.value, 1280);
    }
    function test_poll_cannot_finish_queued_apply() {
        var poll = requests.begin("status");
        var apply = requests.begin("apply");
        verify(apply > poll); verify(requests.busy);
        compare(requests.finish(poll), "status");
        verify(requests.busy); compare(requests.action, "apply");
        compare(requests.begin("save"), 0);
        compare(requests.finish(apply), "apply");
        verify(!requests.busy);
        compare(requests.finish(apply), "");
    }
    function test_drag_survives_polls_and_scroll_threshold() {
        canvas.movements = 0;
        mousePress(canvas, 20, 20);
        verify(canvas.pressed, "initial press");
        for (var i = 1; i <= 10; ++i) {
            var id = requests.begin("status");
            mouseMove(canvas, 20+i*12, 20+i*8, 20);
            requests.finish(id);
            verify(canvas.pressed);
        }
        verify(canvas.movements >= 8);
        mouseRelease(canvas, 140, 100);
    }
}
