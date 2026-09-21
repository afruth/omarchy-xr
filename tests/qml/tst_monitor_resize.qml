import QtQuick
import QtTest
import "../../studio/MonitorSnap.js" as Snap
TestCase {
    name: "MonitorResize"
    function panel(id,x,y,w,h) { return {id:id,x:x,y:y,width:w,height:h,scale:1.25,brightness:80,curvature:30}; }
    function clear(monitors,gap) {
        for(var i=0;i<monitors.length;i++) for(var j=i+1;j<monitors.length;j++) {
            var a=monitors[i],b=monitors[j];
            verify(a.x+a.width+gap<=b.x || b.x+b.width+gap<=a.x || a.y+a.height+gap<=b.y || b.y+b.height+gap<=a.y);
        }
    }
    function test_row_cascade_unordered() {
        var original=[panel("right",220,0,100,100),panel("left",0,0,100,100),panel("middle",110,0,100,100)];
        var result=Snap.resize(original,1,200,100,10);
        compare(result[1].x,0);compare(result[2].x,210);compare(result[0].x,320);
        compare(original[1].width,100);compare(result[1].scale,1.25);compare(result[1].brightness,80);
        clear(result,10);
    }
    function test_grid_both_axes_and_negative_positions() {
        var original=[panel("a",-110,-110,100,100),panel("b",0,-110,100,100),panel("c",-110,0,100,100),panel("d",0,0,100,100)];
        var result=Snap.resize(original,0,250,300,10);
        compare(result[0].x,-110);compare(result[0].y,-110);
        compare(result[1].x,150);compare(result[2].y,200);
        clear(result,10);
        var restored=Snap.resize(result,0,100,100,10);
        for (var i=0;i<original.length;i++) {
            compare(restored[i].x,original[i].x); compare(restored[i].y,original[i].y);
        }
    }
    function test_shrink_preserves_previous_gaps() {
        var original=[panel("a",0,0,200,100),panel("b",210,0,100,100),panel("far",5000,0,100,100)];
        var shrunk=Snap.resize(original,0,100,100,10);
        compare(shrunk[1].x,110);compare(shrunk[2].x,4900);clear(shrunk,10);
        var grown=Snap.resize(original,0,400,100,10);
        compare(grown[1].x,410);compare(grown[2].x,5200);clear(grown,10);
    }
    function test_swap_vertical_cascade_and_limit() {
        var original=[panel("a",0,0,300,100),panel("b",0,111,100,100),panel("c",0,222,100,100)];
        var result=Snap.resize(original,0,100,300,11);
        compare(result[1].y,311);compare(result[2].y,422);clear(result,11);
        var failed=false;
        try { Snap.resize([panel("a",99900,0,100,100),panel("b",100000,0,100,100)],0,1000,100,1); }
        catch(e) { failed=true; }
        verify(failed);
    }
}
