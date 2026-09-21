import QtQuick
import QtTest
import "../../studio/MonitorSnap.js" as Snap
TestCase {
    name: "MonitorSnapping"
    function test_adjacent_odd_gutter_and_edges() {
        var ms=[{x:0,y:13,width:1920,height:1080},{x:2500,y:200,width:1080,height:1920}];
        var result=Snap.place(ms,1,1945,18,31,.15);
        compare(result.x,1951); compare(result.y,13);
        verify(result.snapX && result.snapY);
    }
    function test_bottom_alignment_mixed_sizes() {
        var ms=[{x:-1920,y:-400,width:1920,height:1080},{x:100,y:100,width:1080,height:1920}];
        var result=Snap.place(ms,1,28,-1235,30,.15);
        compare(result.x,30); compare(result.y,-1240);
    }
    function test_grid_far_from_siblings() {
        var ms=[{x:0,y:0,width:1920,height:1080},{x:4000,y:4000,width:1000,height:700}];
        var result=Snap.place(ms,1,4513,3457,30,.2);
        compare(result.x,4520); compare(result.y,3460);
        verify(!result.snapX && !result.snapY);
    }
    function test_overlap_blocks_drag() {
        var ms=[{x:0,y:0,width:1920,height:1080},{x:2000,y:0,width:1000,height:700}];
        var result=Snap.place(ms,1,400,300,30,.2);
        verify(result.blocked); compare(result.x,2000); compare(result.y,0);
    }
    function test_snapping_respects_third_monitor() {
        var ms=[{x:0,y:0,width:1920,height:1080},{x:4000,y:0,width:1080,height:1080},{x:1950,y:0,width:1920,height:1080}];
        var result=Snap.place(ms,1,1947,4,30,.2);
        verify(result.blocked); compare(result.x,4000);
    }
}
