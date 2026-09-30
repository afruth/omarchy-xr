import QtQuick
import QtTest
import "../../studio/LayoutChanges.js" as Changes

TestCase {
    name:"LayoutChanges"
    function test_changes_and_revert_copy() {
        var original={fps:60,monitors:[{id:"1",x:0,width:1920}]}, draft=Changes.copy(original);
        draft.monitors[0].width=1280;draft.monitors.push({id:"2"});draft.fps=30;
        compare(original.monitors[0].width,1920);
        compare(Changes.summary(original,draft),"Add 1 monitor · Resize 1 monitor · Change workspace settings");
        compare(Changes.summary(original,Changes.copy(original)),"No pending changes");
    }
}
