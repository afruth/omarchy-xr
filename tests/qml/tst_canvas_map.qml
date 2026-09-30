import QtQuick
import QtTest
import "../../studio"

TestCase {
    name:"CanvasMap"
    when:windowShown
    visible:true
    width:500; height:300
    CanvasMap {id:map;width:400;height:230;period:1000}
    SignalSpy {id:picks;target:map;signalName:"picked"}
    function init() {picks.clear();map.windows=[];}
    function test_seam_and_selection() {
        map.windows=[{output:"0x1",title:"Editor",x:950,y:0,w:100,h:100}];
        var boxes=map.rectangles();compare(boxes.length,2);
        var y=boxes[0].y+boxes[0].height/2;
        mouseClick(map,15,y);compare(picks.count,1);compare(picks.signalArguments[0][0],"0x1");
        mouseClick(map,380,y);compare(picks.count,2);compare(picks.signalArguments[1][0],"0x1");
    }
    function test_vertical_geometry() {
        map.windows=[{output:"0x1",x:0,y:-500,w:100,h:200},{output:"0x2",x:500,y:600,w:100,h:100}];
        var boxes=map.rectangles();
        verify(boxes.find(function(r){return r.window.output==="0x1";}).y<boxes.find(function(r){return r.window.output==="0x2";}).y);
        var span=map.bounds();compare(span.top,-500);compare(span.bottom,700);
    }
}
