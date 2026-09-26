import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons

// Hosts the canvas search field (plan §5.4) on an overlay layer-shell surface of the canvas output. It
// stays loaded with the Studio plugin (keepLoaded), follows the renderer's .prompt request and answers
// in .search; while shown it holds the keyboard exclusively, the renderer draws the palette itself.
WlrLayershell {
    id: host
    readonly property string xrRuntime: Quickshell.env("OMARCHY_XR_RUNTIME") ||
        ((Quickshell.env("XDG_RUNTIME_DIR") || (Quickshell.env("HOME") + "/.local/state")) + "/omarchy-xr")
    property bool ready: false
    property var request: null
    namespace: "omarchy-xr-search"
    layer: WlrLayer.Overlay
    keyboardFocus: visible ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    exclusionMode: ExclusionMode.Ignore
    anchors.top: true
    margins.top: 200
    implicitWidth: 720
    implicitHeight: 72
    color: "transparent"
    screen: screenNamed(request ? request.output : "")
    visible: field.open

    function screenNamed(name) {
        var list = Quickshell.screens;
        for (var i = 0; i < list.length; ++i) if (list[i].name === name) return list[i];
        return null;
    }
    // A fresh open request starts (or restarts) the field; a close, a stale or a broken line hides it.
    function take(text) {
        var next = field.parsePrompt(text);
        if (!next || !next.open || field.bootOffset <= 0 || field.stale(next)) { request = next; field.open = false; return; }
        request = next;
        if (!field.open || field.owner !== next.owner || field.promptSeq !== next.seq) field.begin(next.owner, next.seq);
    }

    SearchPrompt {
        id: field
        anchors.fill: parent
        accent: Color.popups.border
        foreground: Color.popups.text
        muted: Color.muted
        color: Color.popups.background
        onLine: function(text) { answer.setText(text + "\n"); }
    }
    FileView {
        id: prompt
        // Armed after mkdir like the notification dismissal: FileView cannot watch a missing directory.
        path: host.ready ? host.xrRuntime + "/pose.sock.controls.prompt" : ""
        watchChanges: true
        printErrors: false
        onLoaded: host.take(text())
        onFileChanged: reload()
    }
    FileView { id: answer; path: host.ready ? host.xrRuntime + "/pose.sock.controls.search" : ""; atomicWrites: true; printErrors: false }
    FileView {
        id: uptime
        path: "/proc/uptime"
        printErrors: false
        onLoaded: {
            var seconds = parseFloat(text().split(" ")[0]);
            if (seconds > 0) field.bootOffset = Date.now() / 1000 - seconds;
        }
    }
    Process {
        command: ["mkdir", "-p", host.xrRuntime]
        running: true
        onExited: function(code) { host.ready = code === 0; }
    }
    // The renderer rewrites an open request every second; one that stops ticking belongs to a dead renderer.
    Timer {
        interval: 1000
        repeat: true
        running: field.open
        onTriggered: {
            uptime.reload();
            prompt.reload();
            if (host.request && field.stale(host.request)) field.open = false;
        }
    }
}
