.pragma library

// Match spatial::pose: derive angular sweep from inverse radius and arc length.
// Keep the existing saved-layout amounts; these are not a fixed % -> ° scale.
function limits(monitors, workspaceAmount, distance, workspaceDegrees) {
    if (!monitors.length) return {workspace:0, surfaces:[]};
    var left=Infinity, right=-Infinity, top=Infinity, bottom=-Infinity;
    monitors.forEach(function(m) {
        left=Math.min(left,m.x);right=Math.max(right,m.x+m.width);
        top=Math.min(top,m.y);bottom=Math.max(bottom,m.y+m.height);
    });
    var span=(right-left)/900, cx=(left+right)/2, cy=(top+bottom)/2;
    var d=Math.max(.001,distance), radius=Math.max(d,span/(5*Math.PI/3));
    var explicit=workspaceDegrees !== undefined && workspaceDegrees >= 0;
    var k=explicit ? workspaceDegrees*Math.PI/180/Math.max(span,.001) : workspaceAmount/100/radius;
    var stretch=explicit && k>0 ? Math.max(1,d*k) : 1;
    return {
        workspace:explicit ? 360 : span/radius*180/Math.PI,
        surfaces:monitors.map(function(m) {
            var x=(m.x+m.width/2-cx)/900, y=-(m.y+m.height/2-cy)/900;
            var px=k===0 ? x : Math.sin(x*k)/k*stretch;
            var pz=-d+(k===0 ? 0 : 2*Math.pow(Math.sin(x*k/2),2)/k*stretch);
            var range=Math.sqrt(px*px+y*y+pz*pz), width=m.width/900;
            return Math.min(width/Math.max(range,.1),8*Math.PI/9)*180/Math.PI;
        })
    };
}
function amount(degrees, maximum) {
    return maximum > 0 ? Math.max(0,Math.min(100,degrees/maximum*100)) : 0;
}
