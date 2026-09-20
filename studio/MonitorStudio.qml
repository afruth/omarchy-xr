import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import Quickshell
import Quickshell.Io
import qs.Ui as Ui
import qs.Commons

Item {
    id: root
    property var shell: null
    property bool closingFromHost: false
    property bool busy: false
    property bool loaded: false
    property bool dirty: false
    property int selected: 0
    property int fps: 30
    property int activeCount: 0
    property bool viewing: false
    property string status: "Loading saved layout…"
    property bool error: false
    property real viewScale: 0.1
    property real offsetX: 40
    property real offsetY: 60
    property var monitors: []
    readonly property var current: monitors.length ? monitors[Math.min(selected, monitors.length - 1)] : ({width:1920,height:1080,x:0,y:0})
    readonly property real totalPixels: monitors.reduce(function(sum, m) { return sum + m.width * m.height }, 0)
    function open(payload) { window.visible = true; if (!backend.running) backend.running = true }
    function snapshot() { return JSON.stringify({monitors:monitors, status:status, error:error, busy:busy, active:activeCount, loaded:loaded}) }
    function close() { closingFromHost = true; window.visible = false; closingFromHost = false }
    function hide() { if (shell) shell.hide("afruth.omarchy-xr"); else close() }
    function localPath(url) { return decodeURIComponent(String(url).replace(/^file:\/\//, "")) }
    function send(action) {
        if (busy || !backend.running) return
        busy = true; error = false
        backend.write(JSON.stringify({action:action, layout:{version:1, fps:fps, monitors:monitors}, id:current.id}) + "\n")
    }
    function changed() { dirty = true; canvas.requestPaint() }
    function edit(key, value) {
        if (!monitors.length) return
        var copy = JSON.parse(JSON.stringify(monitors))
        copy[selected][key] = Math.round(value)
        monitors = copy; changed()
    }
    function setCount(count) {
        var copy = JSON.parse(JSON.stringify(monitors))
        while (copy.length > count) copy.pop()
        var nextX = copy.reduce(function(n,m) { return Math.max(n,m.x+m.width) },0)
        while (copy.length < count) {
            copy.push({id:Date.now().toString(36)+"_"+copy.length, width:1920,height:1080,x:nextX,y:0})
            nextX += 1920
        }
        monitors = copy; selected = Math.max(0,Math.min(selected,copy.length-1)); changed(); fit()
    }
    function arrange(grid) {
        var copy = JSON.parse(JSON.stringify(monitors))
        var cols = grid ? Math.ceil(Math.sqrt(copy.length)) : copy.length
        var x=0,y=0,rowHeight=0
        for (var i=0;i<copy.length;i++) {
            if (i && i%cols===0) { x=0; y+=rowHeight; rowHeight=0 }
            copy[i].x=x; copy[i].y=y; x+=copy[i].width; rowHeight=Math.max(rowHeight,copy[i].height)
        }
        monitors=copy; changed(); fit()
    }
    function fit() {
        if (!monitors.length || canvas.width < 1) return
        var l=Infinity,t=Infinity,r=-Infinity,b=-Infinity
        monitors.forEach(function(m) { l=Math.min(l,m.x); t=Math.min(t,m.y); r=Math.max(r,m.x+m.width); b=Math.max(b,m.y+m.height) })
        viewScale=Math.min((canvas.width-80)/(r-l),(canvas.height-80)/(b-t))
        viewScale=Math.max(.001,viewScale)
        offsetX=(canvas.width-(r-l)*viewScale)/2-l*viewScale
        offsetY=(canvas.height-(b-t)*viewScale)/2-t*viewScale
        canvas.requestPaint()
    }
    function hit(x,y) {
        for (var i=monitors.length-1;i>=0;i--) {
            var m=monitors[i]
            if (x>=offsetX+m.x*viewScale && x<=offsetX+(m.x+m.width)*viewScale && y>=offsetY+m.y*viewScale && y<=offsetY+(m.y+m.height)*viewScale) return i
        }
        return -1
    }
    onSelectedChanged: canvas.requestPaint()
    onViewScaleChanged: canvas.requestPaint()
    onOffsetXChanged: canvas.requestPaint()
    onOffsetYChanged: canvas.requestPaint()
    Connections { target: Color; function onBackgroundChanged() { canvas.requestPaint() } function onForegroundChanged() { canvas.requestPaint() } }

    Process {
        id: backend
        command: ["python3", root.localPath(Qt.resolvedUrl("backend.py")), "--renderer", root.localPath(Qt.resolvedUrl("../bin/omarchy-xr"))]
        stdinEnabled: true
        onStarted: { root.busy=false; root.send("load") }
        stdout: SplitParser {
            onRead: function(line) {
                try {
                    var response=JSON.parse(line)
                    root.busy=false; root.error=!response.ok
                    root.activeCount=response.active || 0; root.viewing=!!response.viewing
                    if (response.layout) {
                        root.monitors=response.layout.monitors; root.fps=response.layout.fps
                        root.loaded=true; root.dirty=false; root.status="Arrange your monitors, then Apply."
                        Qt.callLater(root.fit)
                    }
                    if (response.message) {
                        root.status=response.message
                        if (response.ok && response.message.indexOf("ready")>=0) root.dirty=false
                    }
                } catch(e) { root.busy=false; root.error=true; root.status="Could not read backend response: "+e }
            }
        }
        stderr: SplitParser { onRead: function(line) { console.warn("XR Studio: "+line) } }
        onExited: function(code) { root.busy=false; root.loaded=false; root.error=true; root.status="Monitor manager stopped ("+code+"). Reopen the panel to retry." }
    }
    Timer { interval:3000; repeat:true; running:window.visible && root.loaded; onTriggered: if (!root.busy) root.send("status") }
    component Label: Text { color:Color.foreground; font.family:Style.font.family; font.pixelSize:Style.font.body; textFormat:Text.PlainText }
    component Action: Ui.Button { focusable:true; bordered:true }

    FloatingWindow {
        id: window
        title: "XR Monitor Studio"
        visible: false
        color: Color.background
        implicitWidth: 1060
        implicitHeight: 720
        minimumSize: Qt.size(780,600)
        onVisibleChanged: if (!visible && !root.closingFromHost && root.shell) root.shell.hide("afruth.omarchy-xr")
        FocusScope {
            anchors.fill: parent
            focus: true
            Keys.onEscapePressed: root.hide()
            QQC.ScrollView {
                id: scroll
                anchors.fill: parent; anchors.margins: Style.space(20)
                contentWidth: availableWidth
                ColumnLayout {
                width: scroll.availableWidth
                height: Math.max(scroll.availableHeight, 640)
                spacing: Style.space(12)
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Label { text:"XR Monitor Studio"; font.pixelSize:Style.font.heading; font.bold:true }
                        Label { text: "Arrange a workspace around you"; color:Color.muted }
                    }
                    Item { Layout.fillWidth:true }
                    Label { text:root.activeCount+" active"+(root.viewing ? " · viewer open" : "") }
                    Action { text:"Close"; onClicked:root.hide() }
                }
                RowLayout {
                    enabled:root.loaded && !root.busy
                    Ui.NumberField { label:"Monitors"; from:1; to:2147483647; value:root.monitors.length; fieldWidth:130; onModified:function(value){root.setCount(value)} }
                    Action { text:"+ Add"; onClicked:root.setCount(root.monitors.length+1) }
                    Action { text:"Remove selected"; enabled:root.monitors.length>1; onClicked:{var c=root.monitors.slice();c.splice(root.selected,1);root.monitors=c;root.selected=Math.min(root.selected,c.length-1);root.changed();root.fit()} }
                    Item { Layout.fillWidth:true }
                    Ui.NumberField { label:"Capture fps"; from:1; to:60; value:root.fps; fieldWidth:110; onModified:function(value){root.fps=value;root.changed()} }
                    Action { text:"Row"; onClicked:root.arrange(false) }
                    Action { text:"Grid"; onClicked:root.arrange(true) }
                    Action { text:"Fit"; onClicked:root.fit() }
                }
                RowLayout {
                    Layout.fillWidth:true; Layout.fillHeight:true; spacing:Style.space(16)
                    Rectangle {
                        Layout.fillWidth:true; Layout.fillHeight:true
                        color:Color.background; border.color:Color.muted; clip:true
                        Canvas {
                            id:canvas
                            anchors.fill:parent
                            onWidthChanged:root.fit()
                            onHeightChanged:root.fit()
                            onPaint: {
                                var c=getContext("2d");c.reset();c.fillStyle=Color.background;c.fillRect(0,0,width,height)
                                c.strokeStyle=Qt.alpha(Color.foreground,.1);c.lineWidth=1
                                for(var gx=0;gx<width;gx+=24){c.beginPath();c.moveTo(gx,0);c.lineTo(gx,height);c.stroke()}
                                for(var gy=0;gy<height;gy+=24){c.beginPath();c.moveTo(0,gy);c.lineTo(width,gy);c.stroke()}
                                root.monitors.forEach(function(m,i){
                                    var x=root.offsetX+m.x*root.viewScale,y=root.offsetY+m.y*root.viewScale,w=m.width*root.viewScale,h=m.height*root.viewScale
                                    c.fillStyle=Qt.alpha(Color.accent,i===root.selected?.23:.08);c.fillRect(x,y,w,h)
                                    c.strokeStyle=i===root.selected?Color.accent:Color.muted;c.lineWidth=i===root.selected?3:1;c.strokeRect(x,y,w,h)
                                    c.fillStyle=Color.foreground;c.font="bold 16px monospace";c.fillText(String(i+1),x+10,y+23)
                                    if(w>115 && h>60){c.font="12px monospace";c.fillText(m.width+" × "+m.height,x+10,y+44)}
                                })
                            }
                            MouseArea {
                                anchors.fill:parent; acceptedButtons:Qt.LeftButton|Qt.MiddleButton
                                enabled:root.loaded && !root.busy
                                property real startX; property real startY
                                property real originalX; property real originalY
                                property int dragging:-1
                                onPressed:function(mouse){startX=mouse.x;startY=mouse.y;dragging=mouse.button===Qt.LeftButton?root.hit(mouse.x,mouse.y):-1;if(dragging>=0){root.selected=dragging;originalX=root.current.x;originalY=root.current.y}else{originalX=root.offsetX;originalY=root.offsetY}}
                                onPositionChanged:function(mouse){if(!pressed)return;if(dragging>=0){root.edit("x",Math.round((originalX+(mouse.x-startX)/root.viewScale)/20)*20);root.edit("y",Math.round((originalY+(mouse.y-startY)/root.viewScale)/20)*20)}else{root.offsetX=originalX+mouse.x-startX;root.offsetY=originalY+mouse.y-startY}}
                                onWheel:function(wheel){var old=root.viewScale;root.viewScale=Math.max(.001,Math.min(1,old*(wheel.angleDelta.y>0?1.15:1/1.15)));root.offsetX=wheel.x-(wheel.x-root.offsetX)*root.viewScale/old;root.offsetY=wheel.y-(wheel.y-root.offsetY)*root.viewScale/old}
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.preferredWidth:200; Layout.alignment:Qt.AlignTop
                        enabled:root.loaded && !root.busy
                        Label { text:"Monitor "+(root.selected+1); font.bold:true; font.pixelSize:Style.font.title }
                        Ui.NumberField { label:"Width (pixels)"; from:320; to:8192; stepSize:80; value:root.current.width; fieldWidth:190; onModified:function(value){root.edit("width",value)} }
                        Ui.NumberField { label:"Height (pixels)"; from:200; to:8192; stepSize:80; value:root.current.height; fieldWidth:190; onModified:function(value){root.edit("height",value)} }
                        Ui.NumberField { label:"X position"; from:-100000; to:100000; stepSize:20; value:root.current.x; fieldWidth:190; onModified:function(value){root.edit("x",value)} }
                        Ui.NumberField { label:"Y position"; from:-100000; to:100000; stepSize:20; value:root.current.y; fieldWidth:190; onModified:function(value){root.edit("y",value)} }
                        Action { text:"Open terminal here"; enabled:root.activeCount>0 && !root.dirty; onClicked:root.send("terminal") }
                    }
                }
                Label { text:"Drag monitors to move · drag empty space to pan · scroll to zoom · positions snap to 20 px"; color:Color.muted; font.pixelSize:Style.font.bodySmall }
                Label { text:root.monitors.length+" monitors · "+(root.totalPixels/1e6).toFixed(1)+" MP · ~"+(root.totalPixels*4*root.fps/1e9).toFixed(2)+" GB/s per full-frame copy"; color:Color.muted }
                Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; text:(root.busy?"Working… ":"")+root.status; color:root.error?Color.urgent:Color.foreground }
                RowLayout {
                    Layout.fillWidth:true
                    enabled:root.loaded && !root.busy
                    Action { text:"Save layout"; onClicked:root.send("save") }
                    Action { text:root.dirty?"Apply changes *":"Apply layout"; onClicked:root.send("apply") }
                    Action { text:"Open live viewer"; enabled:root.activeCount>0 && !root.dirty; onClicked:root.send("start") }
                    Item { Layout.fillWidth:true }
                    Action { text:"Stop & remove monitors"; enabled:root.activeCount>0; onClicked:root.send("stop") }
                }
                Label { text:"Closing this panel keeps monitors running. Stop removes virtual monitors; open applications return to an available display."; wrapMode:Text.WordWrap; Layout.fillWidth:true; color:Color.muted; font.pixelSize:Style.font.bodySmall }
            }
            }
        }
    }
}
