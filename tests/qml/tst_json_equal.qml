import QtQuick
import QtTest
import "../../studio/json_equal.js" as JsonEqual

TestCase {
    name: "JsonEqual"
    function test_identical_replies_match() {
        var row = {output: "OMXR-1", visible: true, width: 3840, height: 2160};
        compare(JsonEqual.same({fps: 60, captures: [row]}, {fps: 60, captures: [row]}), true);
    }
    function test_fps_change_keeps_the_capture_list() {
        var first = {fps: 60, captures: [{output: "OMXR-1", visible: true}]};
        var second = {fps: 59.8, captures: [{output: "OMXR-1", visible: true}]};
        compare(JsonEqual.same(first, second), false);
        compare(JsonEqual.same(first.captures, second.captures), true);
    }
    function test_capture_visibility_change() {
        compare(JsonEqual.same([{output: "A", visible: true}], [{output: "A", visible: false}]), false);
    }
    function test_empty_lists_match() {
        compare(JsonEqual.same([], []), true);
    }
}
