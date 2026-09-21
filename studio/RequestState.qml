import QtQml

// A status poll never owns the editor's busy state. Correlate replies so a
// background response cannot finish a foreground action queued behind it.
QtObject {
    property int nextId: 0
    property int foregroundId: 0
    property int pollId: 0
    property int stopId: 0
    property string action: ""
    property string stopAction: ""
    readonly property bool busy: foregroundId !== 0
    function begin(name) {
        // Stop must stay usable while a slow request still owns the panel.
        if (name === "stop" || name === "stop_viewer") {
            if (stopId !== 0) return 0;
            stopId = ++nextId;
            stopAction = name;
            return stopId;
        }
        if (busy || (name === "status" && pollId !== 0)) return 0;
        var id = ++nextId;
        if (name === "status") pollId = id;
        else { foregroundId = id; action = name; }
        return id;
    }
    function finish(id) {
        if (id === stopId && stopId !== 0) {
            var stopped = stopAction;
            stopId = 0; stopAction = "";
            return stopped;
        }
        if (id === pollId && pollId !== 0) { pollId = 0; return "status"; }
        if (id === foregroundId && foregroundId !== 0) {
            var completed = action;
            foregroundId = 0; action = "";
            return completed;
        }
        return "";
    }
    function reset() { foregroundId = 0; pollId = 0; stopId = 0; action = ""; stopAction = ""; }
}
