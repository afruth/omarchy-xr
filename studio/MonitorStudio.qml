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
    property int curvature: 0
    property int spacing: 24
    property int activeCount: 0
    property bool viewing: false
    property var glasses: ({})
    property bool confirmRecovery: false
    property string feedback: ""
    property bool feedbackError: false
    property string pendingAction: ""
    function notify(message, failed) { feedback=message; feedbackError=!!failed }
    function requestRecovery() { confirmRecovery=true }
    property string status: "Loading saved layout…"
    property bool error: false
    property real viewScale: 0.1
    property real offsetX: 40
    property real offsetY: 60
    property var monitors: []
    readonly property var current: monitors.length ? monitors[Math.min(selected, monitors.length - 1)] : ({width:1920,height:1080,x:0,y:0})
    readonly property real totalPixels: monitors.reduce(function(sum, m) { return sum + m.width * m.height }, 0)
    function open(payload) { window.visible = true; if (!backend.running) backend.running = true }
    function snapshot() { return JSON.stringify({monitors:monitors, status:status, error:error, busy:busy, active:activeCount, loaded:loaded, glasses:glasses, feedback:feedback, confirmation:confirmRecovery}) }
    function close() { closingFromHost = true; window.visible = false; closingFromHost = false }
    function hide() { if (shell) shell.hide("afruth.omarchy-xr"); else close() }
    function localPath(url) { return decodeURIComponent(String(url).replace(/^file:\/\//, "")) }
    function send(action) {
        if (busy || !backend.running) return
        busy = true; pendingAction=action
        if (action !== "status") {
            error=false
            notify(action === "check" ? "Checking glasses connection…" :
                action === "reinitialize" ? "Starting recovery — watch for the administrator prompt…" : "Working…")
        }
        backend.write(JSON.stringify({action:action === "check" ? "status" : action, layout:{version:1, fps:fps, curvature:curvature, spacing:spacing, monitors:monitors}, id:current.id}) + "\n")
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
        var nextX = copy.reduce(function(n,m) { return Math.max(n,m.x+m.width+root.spacing) },0)
        while (copy.length < count) {
            copy.push({id:Date.now().toString(36)+"_"+copy.length, width:1920,height:1080,x:nextX,y:0})
            nextX += 1920 + spacing
        }
        monitors = copy; selected = Math.max(0,Math.min(selected,copy.length-1)); changed(); fit()
    }
    function arrange(grid) {
        var copy = JSON.parse(JSON.stringify(monitors))
        var cols = grid ? Math.ceil(Math.sqrt(copy.length)) : copy.length
        var x=0,y=0,rowHeight=0
        for (var i=0;i<copy.length;i++) {
            if (i && i%cols===0) { x=0; y+=rowHeight+spacing; rowHeight=0 }
            copy[i].x=x; copy[i].y=y; x+=copy[i].width+spacing; rowHeight=Math.max(rowHeight,copy[i].height)
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
        command: ["python3", "-B", root.localPath(Qt.resolvedUrl("backend.py")), "--renderer", root.localPath(Qt.resolvedUrl("../bin/omarchy-xr"))]
        stdinEnabled: true
        onStarted: { root.busy=false; root.send("load") }
        stdout: SplitParser {
            onRead: function(line) {
                try {
                    var response=JSON.parse(line)
                    root.busy=false
                    if (root.pendingAction !== "status" || !response.ok) root.error=!response.ok
                    var oldRecovery=root.glasses.recoveryMessage || ""
                    var oldSDK=(root.glasses.sdk || {}).message || ""
                    if (response.glasses) root.glasses=response.glasses
                    if (!response.ok) root.notify(response.message || "Action failed", true)
                    else if (root.pendingAction === "check") {
                        root.notify("Checked at " + new Date().toLocaleTimeString() + ": "
                            + (root.glasses.usb ? "USB detected" : "USB not detected") + " · "
                            + (root.glasses.detectionError || (root.glasses.displays.length
                                ? "Video on " + root.glasses.displays.join(", ") : "No VITURE video output")))
                    } else if (response.message) root.notify(response.message)
                    if (response.ok && root.glasses.recoveryMessage && root.glasses.recoveryMessage !== oldRecovery)
                        root.notify(root.glasses.recoveryMessage)
                    if (response.ok && root.glasses.sdk && root.glasses.sdk.message !== oldSDK)
                        { root.notify(root.glasses.sdk.message, root.glasses.sdk.error); root.status=root.glasses.sdk.message }
                    if (root.pendingAction === "load" && response.ok) root.feedback=""
                    root.pendingAction=""
                    root.activeCount=response.active || 0; root.viewing=!!response.viewing
                    if (response.layout) {
                        root.monitors=response.layout.monitors; root.fps=response.layout.fps; root.curvature=response.layout.curvature || 0; root.spacing=response.layout.spacing || 24
                        root.loaded=true; root.dirty=response.message === "Layout saved"; root.status="Arrange your monitors, then Apply."
                        Qt.callLater(root.fit)
                    }
                    if (response.message) {
                        root.status=response.message
                        if (response.ok && response.message.indexOf("ready")>=0) root.dirty=false
                    }
                } catch(e) { root.busy=false; root.error=true; root.status="Could not read backend response: "+e;root.notify(root.status,true) }
            }
        }
        stderr: SplitParser { onRead: function(line) { console.warn("XR Studio: "+line) } }
        onExited: function(code) { root.busy=false; root.loaded=false; root.error=true; root.status="Monitor manager stopped ("+code+"). Reopen the panel to retry.";root.notify(root.status,true) }
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
            id: frame
            anchors.fill: parent
            focus: true
            Keys.onEscapePressed: root.hide()
            Rectangle {
                id: feedbackBanner
                anchors.top:parent.top; anchors.left:parent.left; anchors.right:parent.right
                anchors.margins:Style.space(20)
                height:root.feedback ? feedbackLabel.implicitHeight + Style.space(24) : 0
                visible:!!root.feedback
                color:Color.background
                border.color:root.feedbackError ? Color.urgent : Color.accent
                Label {
                    id:feedbackLabel
                    anchors.left:parent.left; anchors.right:parent.right; anchors.verticalCenter:parent.verticalCenter
                    anchors.margins:Style.space(12)
                    wrapMode:Text.WordWrap
                    text:root.feedback
                    color:root.feedbackError ? Color.urgent : Color.foreground
                    Accessible.role:Accessible.StaticText
                    Accessible.name:text
                }
            }
            QQC.Popup {
                id: recoveryDialog
                parent:frame
                x:(frame.width-width)/2; y:(frame.height-height)/2
                width:Math.min(500,frame.width-40)
                padding:Style.space(20)
                modal:true; focus:true
                visible:root.confirmRecovery
                closePolicy:QQC.Popup.CloseOnEscape
                onClosed:root.confirmRecovery=false
                background:Rectangle { color:Color.background; border.color:Color.accent }
                contentItem:ColumnLayout {
                    spacing:Style.space(16)
                    Label { text:"Reinitialize glasses connection?"; font.bold:true; Layout.fillWidth:true; wrapMode:Text.WordWrap }
                    Label {
                        text:"This restarts the USB-C controller. Other USB-C devices may briefly disconnect. An administrator prompt will appear."
                        Layout.fillWidth:true; wrapMode:Text.WordWrap
                    }
                    RowLayout {
                        Action { text:"Cancel"; onClicked:{root.confirmRecovery=false;root.notify("Reinitialization cancelled. No changes made.")} }
                        Action { text:"Reinitialize"; enabled:!root.busy && !!root.glasses.canReset; onClicked:{root.confirmRecovery=false;root.send("reinitialize")} }
                    }
                }
            }
            QQC.ScrollView {
                id: scroll
                anchors.top:feedbackBanner.bottom
                anchors.left:parent.left; anchors.right:parent.right; anchors.bottom:parent.bottom
                anchors.margins: Style.space(20)
                contentWidth: Math.max(availableWidth, 780)
                ColumnLayout {
                width: scroll.contentWidth
                height: Math.max(scroll.availableHeight, 850)
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
                ColumnLayout {
                    Layout.fillWidth:true
                    RowLayout {
                        Layout.fillWidth:true
                        Label {
                            Layout.fillWidth:true; wrapMode:Text.WordWrap
                            text:"Glasses: " + (root.glasses.usb ? "USB detected" : "USB not detected")
                                + " · " + (root.glasses.detectionError || (root.glasses.displays && root.glasses.displays.length
                                    ? "Video on " + root.glasses.displays.join(", ") : "No VITURE video output"))
                        }
                        Action { text:"Check connection"; enabled:root.loaded && !root.busy; onClicked:root.send("check") }
                        Action {
                            text:root.glasses.recovering ? "Reinitializing…" : "Reinitialize USB-C…"
                            enabled:root.loaded && !root.busy && !!root.glasses.canReset
                            onClicked:root.requestRecovery()
                        }
                    }
                    ColumnLayout {
                        id:sdkControls
                        Layout.fillWidth:true
                        readonly property var sdk:root.glasses.sdk || ({})
                        Label {
                            Layout.fillWidth:true; wrapMode:Text.WordWrap
                            text:"SDK: " + (!sdkControls.sdk.available ? "not installed" : sdkControls.sdk.communication ? "communicating" : "disconnected")
                                + " · Tracking: " + (sdkControls.sdk.tracking ? "receiving (" + sdkControls.sdk.samples + " samples)" : "no recent samples")
                                + (sdkControls.sdk.displayMode !== undefined && sdkControls.sdk.displayMode !== null ? " · Mode: 0x" + sdkControls.sdk.displayMode.toString(16) : "")
                        }
                        RowLayout {
                        Action {
                            text:"Get SDK"; visible:!sdkControls.sdk.available
                            onClicked:Qt.openUrlExternally("https://www.viture.com/developer")
                        }
                        Action {
                            text:sdkControls.sdk.busy ? "Connecting / working…" : sdkControls.sdk.communication ? "Reconnect glasses" : "Connect glasses"
                            enabled:root.loaded && !root.busy && !sdkControls.sdk.busy && !root.glasses.recovering
                            onClicked:root.send("sdk_connect")
                        }
                        Action {
                            text:"Retry display mode"
                            enabled:root.loaded && !root.busy && !!sdkControls.sdk.communication && !sdkControls.sdk.busy && !root.glasses.recovering
                            onClicked:root.send("sdk_restore")
                        }
                        Action {
                            text:"Disconnect SDK"
                            enabled:root.loaded && !root.busy && (!!sdkControls.sdk.communication || !!sdkControls.sdk.busy)
                            onClicked:root.send("sdk_disconnect")
                        }
                        }
                    }
                    Label {
                        visible:!!((root.glasses.sdk || {}).trackingError || (root.glasses.sdk || {}).displayError)
                        text:[(root.glasses.sdk || {}).trackingError, (root.glasses.sdk || {}).displayError].filter(function(x){return !!x}).join(" · ")
                        Layout.fillWidth:true; wrapMode:Text.WordWrap; color:Color.urgent
                    }
                    Label {
                        visible:!!root.glasses.recoveryMessage
                        text:root.glasses.recoveryMessage || ""
                        Layout.fillWidth:true; wrapMode:Text.WordWrap; color:Color.muted
                    }
                    Label {
                        visible:root.loaded && !root.glasses.canReset && !root.glasses.recovering
                        text:"Automatic recovery needs one supported USB-C controller and pkexec. Try unplugging and reconnecting the glasses."
                        Layout.fillWidth:true; wrapMode:Text.WordWrap; color:Color.muted
                    }

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
                    enabled:root.loaded && !root.busy
                    Ui.NumberField { label:"Workspace curvature (%)"; from:0; to:100; stepSize:5; value:root.curvature; fieldWidth:190; onModified:function(value){root.curvature=value;root.changed()} }
                    Ui.NumberField { label:"Spacing (pixels)"; from:1; to:8192; value:root.spacing; fieldWidth:150; onModified:function(value){root.spacing=value;root.changed()} }
                    Label { Layout.fillWidth:true; wrapMode:Text.WordWrap; text:"0 = flat arrangement. Higher values wrap monitor positions around your viewing origin. Surfaces stay flat unless curved individually."; color:Color.muted }
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
                        Ui.NumberField { label:"Surface curvature (%)"; from:0; to:100; stepSize:5; value:root.current.curvature || 0; fieldWidth:190; onModified:function(value){root.edit("curvature",value)} }
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
