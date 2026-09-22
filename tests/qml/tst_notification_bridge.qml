import QtQuick
import QtTest
import "../../notifications"

TestCase {
    name: "NotificationBridge"
    QtObject {
        id: service
        property alias popupModel: model
        property var dismissed: []
        function dismissPopup(index) {
            dismissed= dismissed.concat([model.get(index).originalId]);
            model.remove(index);
        }
    }
    ListModel { id: model }
    Bridge { id: bridge; service: service; ready: true }
    SignalSpy { id: packets; target: bridge; signalName: "packetReady" }
    function row(id,summary) {
        return {originalId:id,timestamp:123456+id,app:"Chat",summary:summary || "Hello",body:"<b>世界</b>",urgency:1};
    }
    function request(key,generation,time) {
        return JSON.stringify({key:key,generation:generation || bridge.generation,time:time===undefined?Date.now():time});
    }
    function init() { model.clear();service.dismissed=[];bridge.lastDismissal="";packets.clear(); }
    function test_mirrors_without_consuming_native_cards() {
        model.append(row(1));model.append(row(2));bridge.publish();
        var data=packets.signalArguments[packets.count-1][0];
        compare(data.version,1);compare(data.entries.length,2);compare(data.count,2);
        compare(data.entries[0].body,"<b>世界</b>");compare(model.count,2);compare(service.dismissed.length,0);
    }
    function test_flick_dismisses_identity_even_if_new_alert_arrived() {
        model.append(row(1));var key=bridge.keyFor(model.get(0));
        model.insert(0,row(2,"Newer alert"));
        verify(bridge.dismiss(request(key)));compare(service.dismissed[0],1);
        compare(model.count,1);compare(model.get(0).originalId,2);
    }
    function test_duplicate_summary_is_not_an_identity() {
        model.append(row(1,"Same"));model.append(row(2,"Same"));
        verify(bridge.dismiss(request(bridge.keyFor(model.get(1)))));
        compare(model.get(0).originalId,1);
    }
    function test_rejects_stale_foreign_and_malformed_requests() {
        model.append(row(1));var key=bridge.keyFor(model.get(0));
        verify(!bridge.dismiss("{"));verify(!bridge.dismiss("null"));verify(!bridge.dismiss(request(key,"old-shell")));
        verify(!bridge.dismiss(request(key,undefined,Date.now()-4000)));
        verify(!bridge.dismiss(request(key,undefined,Date.now()+1000)));
        verify(!bridge.dismiss(request("missing")));compare(model.count,1);
    }
    function test_fractional_renderer_timestamp_in_current_millisecond() {
        for (var i=0;i<10;i++) {
            model.append(row(i));
            var key=bridge.keyFor(model.get(0));
            verify(bridge.dismiss(request(key,undefined,Date.now()+0.75)));
            compare(model.count,0);
        }
        compare(service.dismissed.length,10);
    }
    function test_native_expiry_and_replacement_reach_overlay() {
        model.append(row(1));wait(0);packets.clear();model.setProperty(0,"summary","Updated");
        tryVerify(function(){return packets.count>0;});
        compare(packets.signalArguments[packets.count-1][0].entries[0].summary,"Updated");
        packets.clear();model.remove(0);tryVerify(function(){return packets.count>0;});
        compare(packets.signalArguments[packets.count-1][0].entries.length,0);
    }
    function test_queue_is_bounded() {
        for(var i=0;i<40;i++) model.append(row(i));
        bridge.publish();var data=packets.signalArguments[packets.count-1][0];
        compare(data.count,40);compare(data.entries.length,32);
    }
}
