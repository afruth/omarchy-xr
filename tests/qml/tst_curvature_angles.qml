import QtQuick
import QtTest
import "../../studio/CurvatureAngles.js" as Angles
TestCase {
    name: "CurvatureAngles"
    function test_explicit_wrap_has_fixed_range() {
        var ms=[{x:0,y:0,width:1920,height:1080},{x:1950,y:0,width:1920,height:1080}];
        for(var d of [2,5,2400]) {
            var limits=Angles.limits(ms,0,d,360);
            compare(limits.workspace,360);
            verify(limits.surfaces[0]>0);
        }
        fuzzyCompare(Angles.amount(270,360)*3.6,270,.00001);
    }
    function test_matches_renderer_radius() {
        var limits=Angles.limits([{x:0,y:0,width:1800,height:900}],0,5);
        fuzzyCompare(limits.workspace,2/5*180/Math.PI,.00001);
        fuzzyCompare(limits.surfaces[0],2/5*180/Math.PI,.00001);
        fuzzyCompare(Angles.amount(limits.workspace/2,limits.workspace),50,.00001);
    }
    function test_caps_and_zero() {
        var limits=Angles.limits([{x:0,y:0,width:9000,height:900}],0,.1);
        fuzzyCompare(limits.workspace,300,.00001);
        fuzzyCompare(limits.surfaces[0],160,.00001);
        compare(Angles.amount(0,100),0);
        compare(Angles.amount(0,0),0);
        compare(Angles.amount(200,160),100);
    }
    function test_bent_workspace_matches_camera_centered_radius() {
        var ms=[{x:0,y:0,width:1800,height:900},{x:3600,y:0,width:1800,height:900}];
        var limits=Angles.limits(ms,100,5);
        fuzzyCompare(limits.surfaces[0],2/5*180/Math.PI,.00001);
        fuzzyCompare(limits.surfaces[1],limits.surfaces[0],.00001);
        var farther=Angles.limits(ms,100,10);
        fuzzyCompare(farther.workspace,limits.workspace/2,.00001);
    }
}
