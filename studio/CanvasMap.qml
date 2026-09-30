import QtQuick

Item {
    id: root
    property var windows: []
    readonly property var validWindows: windows.filter(function(w) {
        return [w.x,w.y,w.w,w.h].every(function(v){return typeof v==="number" && isFinite(v);});
    })
    property real period: 1
    property string selected: ""
    property color foreground: "white"
    property color accent: "lightblue"
    signal picked(string address)
    implicitHeight: 230
    function bounds() {
        var top=0, bottom=1;
        validWindows.forEach(function(w) { top=Math.min(top,w.y); bottom=Math.max(bottom,w.y+w.h); });
        return {top:top,bottom:bottom};
    }
    function mapping() {
        var span=bounds(), scale=Math.min((width-16)/Math.max(1,period),(height-32)/Math.max(1,span.bottom-span.top));
        return {span:span,scale:scale,left:(width-period*scale)/2,
            top:8+((height-32)-(span.bottom-span.top)*scale)/2};
    }
    function rectangles() {
        var map=mapping(), span=map.span, sx=map.scale, sy=map.scale, offsetY=map.top, offsetX=map.left;
        var out=[];
        validWindows.forEach(function(w) {
            var x=((w.x%period)+period)%period;
            [-period,0,period].forEach(function(offset) {
                var box={x:offsetX+(x+offset)*sx,y:offsetY+(w.y-span.top)*sy,
                    width:Math.max(4,w.w*sx),height:Math.max(4,w.h*sy),window:w};
                if(box.x<offsetX+period*sx && box.x+box.width>offsetX) out.push(box);
            });
        });
        return out;
    }
    onWindowsChanged: drawing.requestPaint()
    onPeriodChanged: drawing.requestPaint()
    onSelectedChanged: drawing.requestPaint()
    onForegroundChanged: drawing.requestPaint()
    onAccentChanged: drawing.requestPaint()
    onWidthChanged: drawing.requestPaint()
    onHeightChanged: drawing.requestPaint()
    Canvas {
        id: drawing
        anchors.fill: parent
        onPaint: {
            var c=getContext("2d"); c.reset();
            c.fillStyle=Qt.alpha(root.foreground,.035); c.fillRect(0,0,width,height);
            var map=root.mapping();
            c.save(); c.beginPath(); c.rect(map.left,8,root.period*map.scale,height-32); c.clip();
            root.rectangles().forEach(function(r) {
                var active=r.window.output===root.selected;
                c.fillStyle=Qt.alpha(active ? root.accent : root.foreground,active ? .25 : .1);
                c.fillRect(r.x,r.y,r.width,r.height);
                c.strokeStyle=active ? root.accent : Qt.alpha(root.foreground,.5);
                c.lineWidth=active ? 2 : 1; c.strokeRect(r.x,r.y,r.width,r.height);
                if(r.width>55 && r.height>20) {
                    c.save(); c.beginPath(); c.rect(r.x+4,r.y+3,r.width-8,r.height-6); c.clip();
                    c.fillStyle=root.foreground; c.font="12px sans-serif";
                    c.fillText((r.window.focused ? "⌨ " : "")+(r.window.title || r.window.class),r.x+5,r.y+16);
                    c.restore();
                }
            });
            c.restore(); c.fillStyle=Qt.alpha(root.foreground,.65); c.font="12px sans-serif";
            c.fillText("360° window map · edges join",8,height-8);
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: function(mouse) {
            var map=root.mapping();
            if(mouse.x<map.left || mouse.x>map.left+root.period*map.scale) return;
            var boxes=root.rectangles();
            for(var i=boxes.length-1;i>=0;--i) {
                var r=boxes[i];
                if(mouse.x>=r.x && mouse.x<=r.x+r.width && mouse.y>=r.y && mouse.y<=r.y+r.height) {
                    root.picked(r.window.output); return;
                }
            }
        }
    }
}
