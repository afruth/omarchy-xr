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
    function test_preview_without_hold_matches_place() {
        var ms=[{x:0,y:0,width:1920,height:1080},{x:2000,y:0,width:1000,height:700}];
        var placed=Snap.place(ms,1,1960,0,30,.2);
        var previewed=Snap.preview(ms,1,1960,0,30,.2,null);
        compare(previewed.x, placed.x);
        compare(previewed.y, placed.y);
        compare(previewed.blocked, placed.blocked);
    }
    function test_blocked_drag_holds_last_preview() {
        var ms=[{x:0,y:0,width:1920,height:1080},{x:2000,y:0,width:1000,height:700}];
        var first=Snap.preview(ms,1,1960,0,30,.2,null);
        verify(!first.blocked);
        var second=Snap.preview(ms,1,400,300,30,.2,first);
        verify(second.blocked);
        compare(second.x, first.x);
        compare(second.y, first.y);
        compare(ms[1].x, 2000);
    }
    function test_commit_copies_only_the_moved_monitor() {
        var ms=[{x:0,y:0,width:1920,height:1080,id:"a"},{x:2000,y:0,width:1000,height:700,id:"b"}];
        var next=Snap.commit(ms,1,{x:2200,y:40,snapX:false,snapY:false,blocked:false});
        compare(next[1].x, 2200);
        compare(next[1].y, 40);
        compare(next[1].id, "b");
        verify(next !== ms);
        verify(next[0] === ms[0]);
        compare(ms[1].x, 2000);
    }
    function test_commit_same_position_keeps_the_array() {
        var ms=[{x:0,y:0,width:10,height:10}];
        verify(Snap.commit(ms,0,{x:0,y:0}) === ms);
        verify(Snap.commit(ms,0,null) === ms);
    }
}
