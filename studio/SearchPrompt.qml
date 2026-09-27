import QtQuick

// Window canvas search field (plan §5.4). Plain QtQuick so qmltestrunner can load it without Quickshell;
// SearchPromptWindow hosts it on a layer-shell surface and carries the mailboxes. The field owns the
// keyboard while open, so every edit and every canvas key leaves as one .search line:
// v1 <owner> <promptSeq> <editSeq> <hex text|-> <open 0/1> <keys> <stamp>. keys is '-' for a text edit,
// else the keys since the last text edit (at most 8, comma-separated, this line's last): the mailbox keeps
// one line, so a renderer that polls after two quick keys still sees both.
Rectangle {
    id: root
    property string owner: ""
    property int promptSeq: 0
    property bool open: false
    property int editSeq: 0
    property var keys: []
    // Unix seconds minus boot seconds; the renderer stamps .prompt with CLOCK_BOOTTIME.
    property real bootOffset: 0
    property color accent: "#89b4fa"
    property color foreground: "#cdd6f4"
    property color muted: "#7f849c"
    property alias text: field.text
    property bool quiet: false
    // Virtual monitors mode lists the renderer's ranked windows here (.results); the canvas draws its own.
    property var results: []
    readonly property int fieldHeight: 72
    readonly property int rowHeight: 44
    signal line(string text)
    implicitWidth: 720
    implicitHeight: fieldHeight + (results.length ? results.length * rowHeight + 12 : 0)
    color: "#1e1e2e"
    border.color: accent
    border.width: 2
    radius: 12

    // UTF-8 bytes as lowercase hex, at most 550 bytes cut on a character boundary; '-' when empty.
    function hexOf(value) {
        var out = "", bytes = 0;
        for (const ch of String(value)) {
            var c = ch.codePointAt(0), seq;
            if (c < 0x80) seq = [c];
            else if (c < 0x800) seq = [0xc0 | c >> 6, 0x80 | c & 63];
            else if (c < 0x10000) seq = [0xe0 | c >> 12, 0x80 | c >> 6 & 63, 0x80 | c & 63];
            else seq = [0xf0 | c >> 18, 0x80 | c >> 12 & 63, 0x80 | c >> 6 & 63, 0x80 | c & 63];
            if (bytes + seq.length > 550) break;
            bytes += seq.length;
            for (var i = 0; i < seq.length; ++i) out += (seq[i] < 16 ? "0" : "") + seq[i].toString(16);
        }
        return out === "" ? "-" : out;
    }
    // Hex UTF-8 back to text ('-' is empty); malformed input gives "".
    function textOf(hex) {
        if (hex === "-" || !/^([0-9a-f]{2})+$/.test(hex)) return "";
        try { return decodeURIComponent(hex.replace(/(..)/g, "%$1")); } catch (e) { return ""; }
    }
    // .results (renderer -> prompt, monitors mode): v1 <pid> <promptSeq> <seq> <stamp>, then
    // "<selected 0/1> <hex class|-> <hex title|->" rows. Rows of another renderer or prompt are ignored.
    function parseResults(value, pid, seq) {
        var lines = String(value).split("\n").filter(function(l) { return l !== ""; });
        if (!lines.length) return [];
        var head = lines[0].split(" ");
        if (head.length !== 5 || head[0] !== "v1" || head[1] !== String(pid) || head[2] !== String(seq)) return [];
        var rows = [];
        for (var i = 1; i < lines.length && rows.length < 8; ++i) {
            var f = lines[i].split(" ");
            if (f.length !== 3 || (f[0] !== "0" && f[0] !== "1")) return [];
            rows.push({selected: f[0] === "1", cls: textOf(f[1]), title: textOf(f[2])});
        }
        return rows;
    }
    // .prompt (renderer -> prompt): v1 <pid> <seq> <open 0/1> <output|-> <stamp>, else null.
    function parsePrompt(value) {
        var f = String(value).trim().split(/\s+/);
        if (f.length !== 6 || f[0] !== "v1" || !/^[0-9]+$/.test(f[1]) || !/^[0-9]+$/.test(f[2])
            || (f[3] !== "0" && f[3] !== "1") || !/^[0-9]+$/.test(f[5])) return null;
        return {owner: f[1], seq: Number(f[2]), open: f[3] === "1", output: f[4] === "-" ? "" : f[4], stamp: Number(f[5])};
    }
    // A request older than 3 s means the renderer stopped its heartbeat. Unix stamps compare directly.
    function stale(prompt, now) {
        var t = now === undefined ? Date.now() / 1000 : now;
        if (prompt.stamp < 1000000000) t -= bootOffset;
        return t - prompt.stamp > 3;
    }
    // A new request (renderer pid and prompt sequence) starts with an empty field and edit counter.
    function begin(pid, seq) {
        owner = pid; promptSeq = seq;
        if (open) reset(); else open = true;
    }
    function reset() {
        quiet = true; field.text = ""; quiet = false;
        results = [];
        editSeq = 0;
        keys = [];
        field.forceActiveFocus();
    }
    function publish(key, stillOpen) {
        if (owner === "") return;
        editSeq += 1;
        keys = key === "-" ? [] : keys.concat([key]).slice(-8);
        line(["v1", owner, promptSeq, editSeq, hexOf(field.text), stillOpen ? 1 : 0, keys.length ? keys.join(",") : "-", Math.floor(Date.now() / 1000)].join(" "));
    }
    // Esc clears the text first (an ordinary edit), then asks the renderer to close and revert.
    function cancel() {
        if (field.text !== "") field.text = "";
        else publish("esc", false);
    }
    function keyName(event) {
        var ctrl = event.modifiers & Qt.ControlModifier, shift = event.modifiers & Qt.ShiftModifier;
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) return shift ? "shift-enter" : "enter";
        if (event.key === Qt.Key_Up) return "up";
        if (event.key === Qt.Key_Down) return "down";
        if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && shift)) return "shift-tab";
        if (event.key === Qt.Key_Tab) return "tab";
        if (event.key === Qt.Key_F1) return "f1";
        if (!ctrl) return "";
        if (event.key >= Qt.Key_1 && event.key <= Qt.Key_8) return "ctrl-" + (event.key - Qt.Key_0);
        if (event.key === Qt.Key_A) return "ctrl-a";
        if (event.key === Qt.Key_Z) return shift ? "ctrl-shift-z" : "ctrl-z";
        return "";
    }
    onOpenChanged: if (open) reset()

    Text {
        anchors.fill: field
        verticalAlignment: Text.AlignVCenter
        leftPadding: field.leftPadding
        visible: field.text === ""
        text: "Search windows"
        color: root.muted
        font.pixelSize: 28
    }
    Column {
        objectName: "search-results"
        x: root.border.width + 6
        y: root.fieldHeight
        width: root.width - 2 * x
        Repeater {
            model: root.results
            delegate: Rectangle {
                required property var modelData
                required property int index
                objectName: "search-result-" + index
                width: parent.width
                height: root.rowHeight
                radius: 8
                color: modelData.selected ? Qt.alpha(root.accent, .28) : "transparent"
                Text {
                    anchors.fill: parent
                    leftPadding: 16
                    rightPadding: 16
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    font.pixelSize: 20
                    color: root.foreground
                    text: modelData.title + (modelData.cls ? "  ·  " + modelData.cls : "")
                }
            }
        }
    }
    TextInput {
        id: field
        objectName: "search-field"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.fieldHeight
        anchors.margins: root.border.width
        leftPadding: 22
        rightPadding: 22
        verticalAlignment: TextInput.AlignVCenter
        font.pixelSize: 28
        color: root.foreground
        selectionColor: root.accent
        clip: true
        focus: true
        onTextChanged: if (!root.quiet && root.open) root.publish("-", true)
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) { root.cancel(); event.accepted = true; return; }
            var name = root.keyName(event);
            if (name === "") return;
            root.publish(name, true);
            event.accepted = true;
        }
    }
}
