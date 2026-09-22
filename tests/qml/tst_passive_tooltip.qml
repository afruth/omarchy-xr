import QtQuick
import QtTest
import "../../studio"

TestCase {
    id: test
    name: "PassiveToolTip"
    when: windowShown
    visible: true
    width: 600; height: 400

    MouseArea {
        id: underneath
        anchors.fill: parent
        property int clicks: 0
        onClicked: clicks++
    }
    Item {
        id: control
        x: 240; y: 160; width: 100; height: 40
        MouseArea {
            id: button
            anchors.fill: parent
            property int clicks: 0
            onClicked: { clicks++; forceActiveFocus(); }
        }
        PassiveToolTip {
            id: tip
            boundaryItem: test
            target: control
            width: 260
            text: "Help for a control"
            delay: 0
            timeout: -1
        }
    }
    function init() {
        tip.active=false;
        tip.delay=0; tip.timeout=-1;
        control.x=240; control.y=160;
        button.clicks=0; underneath.clicks=0;
    }
    function cleanup() { tip.active=false; }
    function showTip() { tip.active=true; tryCompare(tip, "visible", true); }
    function test_source_button_remains_clickable() {
        showTip();
        verify(tip.y + tip.height <= control.y - tip.gap);
        mouseClick(button, 50, 20);
        compare(button.clicks, 1);
        verify(button.activeFocus);
    }
    function test_click_passes_through_tooltip_to_neighbour() {
        showTip();
        mouseClick(test, tip.x + tip.width/2, tip.y + tip.height/2);
        compare(underneath.clicks, 1);
    }
    function test_top_edge_places_tip_below_control() {
        control.y=8;
        showTip();
        verify(tip.y >= control.y + control.height + tip.gap);
        mouseClick(button, 50, 20);
        compare(button.clicks, 1);
    }
    function test_right_edge_stays_inside_window() {
        control.x=490;
        showTip();
        verify(tip.x + tip.width <= test.width - tip.edge);
    }
    function test_hover_delay_and_exit() {
        tip.delay=80;
        tip.active=true;
        verify(!tip.visible);
        tryCompare(tip, "visible", true);
        tip.active=false;
        verify(!tip.visible);
        // Retaining keyboard focus after a click must not keep help open.
        button.forceActiveFocus();
        wait(100);
        verify(!tip.visible);
    }
    function test_help_expires_until_hover_restarts() {
        tip.timeout=80;
        showTip();
        tryCompare(tip, "visible", false);
        wait(100);
        verify(!tip.visible);
        tip.active=false;
        showTip();
    }
}
