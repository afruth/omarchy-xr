import QtQuick
QtObject {
    property string path
    property bool watchChanges: false
    property bool atomicWrites: false
    property bool printErrors: true
    signal loaded()
    signal loadFailed(int error)
    signal fileChanged()
    function text() { return ""; }
    function setText(text) {}
    function reload() {}
}
