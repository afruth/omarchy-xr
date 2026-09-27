import QtQuick
import QtTest
import "../../studio"

TestCase {
    id: test
    name: "KeyMap"
    when: windowShown
    visible: true
    width: 900; height: 900

    readonly property var actionList: [
        {id: "recenter", group: "View", title: "Recenter", default: "space", scope: "both"},
        {id: "focus", group: "View", title: "Focus the window you look at", default: "Down", scope: "both"},
        {id: "next", group: "Windows", title: "Next window", default: "Right", scope: "both"},
        {id: "nudge_left", group: "Canvas arrangement", title: "Nudge left", default: "SHIFT + Left", scope: "canvas"},
        {id: "help", group: "Notifications & system", title: "Show keys in the headset", default: "H", scope: "both"}]
    function defaults() {
        return {version: 2, modifier: "CTRL + ALT", fingers: 3, keys: {recenter: "space", focus: "Down", next: "Right", nudge_left: "SHIFT + Left", help: "H"}};
    }
    KeyMap {
        id: map
        width: 800
        actions: test.actionList
        profile: test.defaults()
        onEdited: function(next) { map.profile = next; }
    }
    SignalSpy { id: spy; target: map; signalName: "edited" }
    function init() {
        map.profile = defaults();
        map.bindings = [];
        map.locked = false;
        map.capturing = "";
        map.width = 800;
        spy.clear();
    }
    function button(id) { return findChild(map, "key-" + id); }

    function test_two_columns_fit() {
        waitForRendering(map);
        for (var i = 0; i < actionList.length; ++i) {
            var b = button(actionList[i].id), at = b.mapToItem(map, 0, 0);
            verify(at.x >= 0 && at.x + b.width <= map.width + 1, actionList[i].id);
        }
        verify(button("next").mapToItem(map, 0, 0).x > map.width / 2);   // the second column
    }
    function test_display_and_groups() {
        compare(map.groups, ["View", "Windows", "Canvas arrangement", "Notifications & system"]);
        compare(map.display("space"), "Ctrl+Alt+Space");
        compare(map.display("SHIFT + Left"), "Ctrl+Alt+Shift+←");
        compare(map.display(""), "Off");
        waitForRendering(map);
        compare(button("nudge_left").Accessible.name, "Nudge left, Ctrl+Alt+Shift+←");
        compare(map.conflictCount, 0);
    }
    function test_capture_a_key_with_shift() {
        var b = button("recenter");
        mouseClick(b);
        compare(map.capturing, "recenter");
        keyClick(Qt.Key_R, Qt.ShiftModifier);
        compare(spy.count, 1);
        compare(map.profile.keys.recenter, "SHIFT + R");
        compare(map.capturing, "");
        mouseClick(b); keyClick(Qt.Key_Less, Qt.ShiftModifier);           // shifted symbol names its key
        compare(map.profile.keys.recenter, "SHIFT + comma");
        mouseClick(b); keyClick(Qt.Key_Control);                          // a modifier alone keeps listening
        compare(map.capturing, "recenter");
        keyClick(Qt.Key_F5);
        compare(map.profile.keys.recenter, "F5");
    }
    function test_off_cancel_and_reset() {
        var b = button("help");
        mouseClick(b);  keyClick(Qt.Key_Backspace);
        compare(map.profile.keys.help, "");
        mouseClick(b); keyClick(Qt.Key_Escape);
        compare(map.capturing, ""); compare(map.profile.keys.help, "");
        var reset = findChild(map, "reset-help");
        verify(reset.visible);
        mouseClick(reset);
        compare(map.profile.keys.help, "H");
        verify(!reset.visible);
    }
    function test_conflicts() {
        map.profile = map.withKey("help", "space");
        compare(map.conflicts.help, "Same key as Recenter");
        verify(findChild(map, "problem-help").visible);
        compare(map.conflictCount, 1);
        map.profile = map.withKey("help", "F2");
        compare(map.conflicts.help, "CTRL + ALT + F1…F12 switch virtual terminals");
        map.bindings = [{modmask: 12, key: "h", description: "Launcher"}, {modmask: 12, key: "Down", description: "XR: focus"}];
        map.profile = map.withKey("help", "H");
        compare(map.conflicts.help, "Already used by Launcher");
        verify(map.conflicts.focus === undefined);                       // XR's own bindings never conflict
    }
    function test_modifier() {
        mouseClick(findChild(map, "modifier-SUPER"));
        compare(map.profile.modifier, "CTRL + ALT + SUPER");
        mouseClick(findChild(map, "modifier-CTRL"));
        mouseClick(findChild(map, "modifier-ALT"));
        compare(map.profile.modifier, "SUPER");
        verify(findChild(map, "modifier-error").visible);
        compare(map.conflictCount, 1);
        mouseClick(findChild(map, "modifier-SHIFT"));
        compare(map.profile.modifier, "SHIFT + SUPER");
        compare(map.conflicts.nudge_left, "The XR modifier already includes SHIFT");
    }
    function test_keyboard_navigation() {
        button("recenter").forceActiveFocus();
        keyClick(Qt.Key_Down);
        verify(button("focus").activeFocus);
        keyClick(Qt.Key_Return);
        compare(map.capturing, "focus");
        keyClick(Qt.Key_J);
        compare(map.profile.keys.focus, "J");
        keyClick(Qt.Key_Up);
        verify(button("recenter").activeFocus);
    }
    function test_locked_and_narrow() {
        map.locked = true;
        mouseClick(button("recenter"));
        compare(map.capturing, "");
        map.locked = false;
        waitForRendering(map);
        map.width = 500;
        waitForRendering(map);
        verify(button("next").width <= 500 * .45 + 1);
        verify(button("help").mapToItem(map, 0, 0).x + button("help").width <= 500 + 1); // one column fits
    }
}
