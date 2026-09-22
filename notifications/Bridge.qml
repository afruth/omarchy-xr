import QtQuick

// Pure model adapter. Desktop lifetime, actions, DND and dismissal remain owned
// by Omarchy's notification service; no Notification QObject crosses this bridge.
QtObject {
    id: bridge
    property var service: null
    property bool ready: false
    property var palette: ({background:"#16242d",text:"#d6e2ee",accent:"#8bc9eb",urgent:"#f7768e"})
    readonly property string generation: Date.now().toString(36) + "-" + Math.random().toString(36).slice(2)
    property string lastDismissal: ""
    signal packetReady(var packet)

    function keyFor(row) { return String(row.timestamp) + ":" + String(row.originalId); }
    function publish() {
        if (!ready || !service || !service.popupModel) return;
        var entries=[];
        for (var i=0;i<Math.min(service.popupModel.count,1);i++) {
            var row=service.popupModel.get(i);
            entries.push({key:keyFor(row),app:String(row.app || "").slice(0,256),
                summary:String(row.summary || "").slice(0,2048),body:String(row.body || "").slice(0,8192),urgency:Number(row.urgency || 0)});
        }
        packetReady({version:1,generation:generation,time:Date.now(),palette:palette,
            count:service.popupModel.count,entries:entries});
    }
    function schedule() { Qt.callLater(publish); }
    function dismiss(text) {
        if (!ready || !service || text===lastDismissal) return false;
        var request;
        try { request=JSON.parse(text); } catch(e) { return false; }
        if (!request || typeof request!=="object") return false;
        var age=Date.now()-Number(request.time);
        if (request.generation!==generation || !isFinite(age) || age<0 || age>3000 || typeof request.key!=="string") return false;
        lastDismissal=text;
        for (var i=0;i<service.popupModel.count;i++) {
            if (keyFor(service.popupModel.get(i))===request.key) {
                service.dismissPopup(i);
                schedule();
                return true;
            }
        }
        return false;
    }
    onReadyChanged: schedule()
    onPaletteChanged: schedule()
    property Connections modelChanges: Connections {
        target: bridge.service ? bridge.service.popupModel : null
        function onCountChanged() { bridge.schedule(); }
        function onDataChanged() { bridge.schedule(); }
        function onRowsMoved() { bridge.schedule(); }
        function onModelReset() { bridge.schedule(); }
    }
}
