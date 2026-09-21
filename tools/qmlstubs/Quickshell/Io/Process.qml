import QtQuick
QtObject {
    property var command
    property bool stdinEnabled: false
    property bool running: false
    property var stdout
    property var stderr
    signal started()
    signal exited(int code)
    function write(line) {}
}
