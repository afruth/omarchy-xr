import QtQuick
import QtTest
import "../../studio"

TestCase {
    id: test
    name: "SearchPrompt"
    when: windowShown
    visible: true
    width: 800; height: 120

    SearchPrompt {
        id: prompt
        owner: "123"
        promptSeq: 4
    }
    SignalSpy { id: spy; target: prompt; signalName: "line" }
    function init() {
        prompt.open = false;
        prompt.owner = "123";
        prompt.promptSeq = 4;
        prompt.open = true;
        spy.clear();
    }
    function fields(index) { return spy.signalArguments[index === undefined ? spy.count - 1 : index][0].split(" "); }
    function test_typing_emits_hex_edits() {
        keyClick(Qt.Key_T); keyClick(Qt.Key_E); keyClick(Qt.Key_R); keyClick(Qt.Key_M);
        compare(spy.count, 4);
        var f = fields();
        compare(f.length, 8);
        compare(f.slice(0, 3), ["v1", "123", "4"]);
        compare(f[4], "7465726d");
        compare(f[5], "1");
        compare(f[6], "-");
        verify(Math.abs(Number(f[7]) - Date.now() / 1000) < 5);
        for (var i = 0; i < 4; ++i) compare(fields(i)[3], String(i + 1));
    }
    // This line's key: the newest entry of the key log.
    function key() { return fields()[6].split(",").pop(); }
    function test_keys_forward_with_text() {
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_Return);
        compare(fields()[6], "enter");
        compare(fields()[4], "61");
        keyClick(Qt.Key_Return, Qt.ShiftModifier);
        compare(key(), "shift-enter");
        keyClick(Qt.Key_3, Qt.ControlModifier);
        compare(key(), "ctrl-3");
        keyClick(Qt.Key_Up); compare(key(), "up");
        keyClick(Qt.Key_Down); compare(key(), "down");
        keyClick(Qt.Key_Tab); compare(key(), "tab");
        keyClick(Qt.Key_Backtab, Qt.ShiftModifier); compare(key(), "shift-tab");
        keyClick(Qt.Key_A, Qt.ControlModifier); compare(key(), "ctrl-a");
        keyClick(Qt.Key_Z, Qt.ControlModifier); compare(key(), "ctrl-z");
        keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier); compare(key(), "ctrl-shift-z");
        keyClick(Qt.Key_F1); compare(key(), "f1");
        // Keys never edit the text: Ctrl+A did not select, Ctrl+Z did not undo.
        compare(prompt.text, "a");
        compare(fields()[3], String(spy.count));
    }
    // Each line carries the keys since the last text edit (at most 8), so a reader that missed a line
    // still sees Down before Enter; a text edit starts the log over.
    function test_key_log() {
        keyClick(Qt.Key_T);
        compare(fields()[6], "-");
        keyClick(Qt.Key_Down); keyClick(Qt.Key_Down); keyClick(Qt.Key_Return);
        compare(fields()[6], "down,down,enter");
        compare(fields()[3], "4");
        keyClick(Qt.Key_X);
        compare(fields()[6], "-");
        for (var i = 0; i < 9; ++i) keyClick(Qt.Key_Tab);
        compare(fields()[6], "tab,tab,tab,tab,tab,tab,tab,tab");
    }
    function test_escape_clears_then_closes() {
        keyClick(Qt.Key_X);
        keyClick(Qt.Key_Escape);
        compare(prompt.text, "");
        var f = fields();
        compare([f[4], f[5], f[6]], ["-", "1", "-"]);
        keyClick(Qt.Key_Escape);
        f = fields();
        compare([f[4], f[5], f[6]], ["-", "0", "esc"]);
        compare(spy.count, 3);
    }
    function test_parse_prompt() {
        var now = Math.floor(Date.now() / 1000);
        var p = prompt.parsePrompt("v1 123 4 1 OMXR-abcd1234-canvas " + now + "\n");
        verify(p !== null);
        compare([p.owner, p.seq, p.open, p.output, p.stamp], ["123", 4, true, "OMXR-abcd1234-canvas", now]);
        compare(prompt.parsePrompt("v1 123 4 0 - " + now).output, "");
        compare(prompt.parsePrompt("v1 123 4 1 " + now), null);
        compare(prompt.parsePrompt("v2 123 4 1 - " + now), null);
        compare(prompt.parsePrompt("v1 123 4 2 - " + now), null);
        verify(!prompt.stale(p));
    }
    function test_stale() {
        var now = Date.now() / 1000;
        verify(prompt.stale({stamp: Math.floor(now) - 10}));
        verify(!prompt.stale({stamp: Math.floor(now) - 1}));
        // Boot-clock stamps (the renderer's) are shifted by the boot offset.
        prompt.bootOffset = now - 5000;
        verify(!prompt.stale({stamp: 4999}));
        verify(prompt.stale({stamp: 4990}));
        prompt.bootOffset = 0;
    }
    function test_hex_cuts_on_character_boundary() {
        compare(prompt.hexOf(""), "-");
        compare(prompt.hexOf("é€😀"), "c3a9e282acf09f9880");
        var text = "";
        for (var i = 0; i < 300; ++i) text += "é";
        var hex = prompt.hexOf(text);
        compare(hex.length, 1100);
        compare(hex.length % 2, 0);
        compare(hex.slice(-4), "c3a9");
    }
    function test_open_resets_text_and_counter() {
        keyClick(Qt.Key_Q);
        compare(prompt.editSeq, 1);
        prompt.begin("123", 5);
        compare(prompt.text, "");
        compare(prompt.editSeq, 0);
        compare(spy.count, 1);
        keyClick(Qt.Key_W);
        compare(fields()[2], "5");
        compare(fields()[3], "1");
        prompt.open = false;
        keyClick(Qt.Key_E);
        prompt.open = true;
        compare(prompt.text, "");
        compare(prompt.editSeq, 0);
    }
}
