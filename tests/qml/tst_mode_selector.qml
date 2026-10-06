import QtQuick
import QtTest
import "../../studio"

TestCase {
    id: test
    name: "ModeSelector"
    when: windowShown
    visible: true
    width: 600; height: 200

    ModeSelector {
        id: selector
        width: 400
    }
    ModeSelector {
        id: glasses
        y: 100
        width: 400
        options: [{value:"stereo",label:"3D stereo · 60 Hz"},{value:"mono",label:"Mono · 120 Hz"}]
        mode: "stereo"
    }
    SignalSpy { id: glassesSpy; target: glasses; signalName: "picked" }
    SignalSpy { id: spy; target: selector; signalName: "picked" }
    SignalSpy { id: actionSpy; target: selector; signalName: "actionRequested" }
    function init() {
        selector.mode = "monitors";
        selector.locked = false;
        selector.hint = "";
        selector.actionText = "";
        spy.clear();
        actionSpy.clear();
    }
    function segment(value) { return findChild(selector, "mode-" + value); }
    function click(value) { var s = segment(value); mouseClick(s, s.width / 2, s.height / 2); }
    // The glasses display choice reuses the selector with its own options; a hint only ever blocks "canvas".
    function test_custom_options_pick_their_values() {
        glasses.hint = "Stop the glasses to switch.";
        var mono = findChild(glasses, "mode-mono");
        verify(mono && mono.enabled && findChild(glasses, "mode-stereo").checked);
        mouseClick(mono, mono.width / 2, mono.height / 2);
        compare(glassesSpy.count, 1);
        compare(glassesSpy.signalArguments[0][0], "mono");
        glasses.hint = "";
    }
    function test_pick_emits_only_changes() {
        click("canvas");
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], "canvas");
        selector.mode = "canvas";
        verify(segment("canvas").checked);
        verify(!segment("monitors").checked);
        click("canvas");
        compare(spy.count, 1);
    }
    function test_locked_blocks_clicks() {
        selector.locked = true;
        verify(!segment("monitors").enabled);
        verify(!segment("canvas").enabled);
        click("canvas");
        selector.forceActiveFocus();
        keyClick(Qt.Key_Right);
        compare(spy.count, 0);
    }
    function test_hint_disables_canvas_only() {
        selector.hint = "Window canvas needs XR controls v6";
        selector.mode = "canvas";
        verify(!segment("canvas").enabled);
        verify(segment("monitors").enabled);
        var text = findChild(selector, "mode-hint");
        verify(text.visible);
        compare(text.text, "Window canvas needs XR controls v6");
        click("monitors");
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], "monitors");
        selector.mode = "monitors";
        selector.forceActiveFocus();
        keyClick(Qt.Key_Right);
        compare(spy.count, 1);
        selector.hint = "";
        verify(!text.visible);
    }
    function test_hint_action_button() {
        var button = findChild(selector, "mode-action");
        selector.actionText = "Install XR controls";
        verify(!button.visible);
        selector.hint = "Window canvas needs XR controls v6.";
        verify(button.visible);
        waitForRendering(selector);
        compare(button.Accessible.name, "Install XR controls");
        mouseClick(button, button.width / 2, button.height / 2);
        compare(actionSpy.count, 1);
        compare(spy.count, 0);
        selector.locked = true;
        verify(!button.enabled);
        mouseClick(button, button.width / 2, button.height / 2);
        compare(actionSpy.count, 1);
        selector.hint = "";
        verify(!button.visible);
    }
    function test_keyboard_arrows() {
        selector.forceActiveFocus();
        keyClick(Qt.Key_Right);
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], "canvas");
        keyClick(Qt.Key_Left);
        compare(spy.count, 1);
        selector.mode = "canvas";
        keyClick(Qt.Key_Right);
        compare(spy.count, 1);
        keyClick(Qt.Key_Left);
        compare(spy.count, 2);
        compare(spy.signalArguments[1][0], "monitors");
    }
    function test_accessible_names() {
        compare(segment("monitors").Accessible.name, "Virtual monitors");
        compare(segment("canvas").Accessible.name, "Window canvas");
        compare(segment("canvas").Accessible.role, Accessible.RadioButton);
    }
}
