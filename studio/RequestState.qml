import QtQml

// A status poll never owns the editor's busy state. Correlate replies so a
// background response cannot finish a foreground action queued behind it.
QtObject {
    property int nextId: 0
    property int foregroundId: 0
    property int pollId: 0
    property string action: ""
    readonly property bool busy: foregroundId !== 0
    function begin(name) {
        if (busy || (name === "status" && pollId !== 0)) return 0;
        var id = ++nextId;
        if (name === "status") pollId = id;
        else { foregroundId = id; action = name; }
        return id;
    }
    function finish(id) {
        if (id === pollId && pollId !== 0) { pollId = 0; return "status"; }
        if (id === foregroundId && foregroundId !== 0) {
            var completed = action;
            foregroundId = 0; action = "";
            return completed;
        }
        return "";
    }
    function reset() { foregroundId = 0; pollId = 0; action = ""; }
}
