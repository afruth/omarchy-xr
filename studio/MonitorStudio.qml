import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Controls as QQC
import Quickshell
import Quickshell.Io
import qs.Ui as Ui
import qs.Commons
import "MonitorSnap.js" as MonitorSnap
import "MonitorPresets.js" as MonitorPresets
import "CurvatureAngles.js" as CurvatureAngles

Item {
    id: root
    property var shell: null
    property bool closingFromHost: false
    readonly property bool busy: requests.busy
    property double busySinceMs: 0
    property bool backendSlow: false
    RequestState { id: requests }
    property bool loaded: false
    property bool dirty: false
    property int selected: 0
    property var dragSnap: null
    property var controlDraft: ({fingers:3,fit_all:"CTRL + Up",fit_target:"CTRL + Down",recenter:"",zoom_in:"",zoom_out:""})
    property bool controlsDirty: false
    function setControl(key,value) {
        var copy=Object.assign({},controlDraft);copy[key]=value;controlDraft=copy;controlsDirty=true;
    }
    property var environmentSettings: ({id:"",brightness:25,rotation:0})
    property var environmentItems: []
    property bool canImportEnvironment: false
    property int imageResolution: 4096
    property string imagePath: ""
    function chooseEnvironment(identity) { setEnvironment("id",identity); }
    function setEnvironment(key,value) {
        var settings=Object.assign({},environmentSettings);settings[key]=value;
        environmentSettings=settings;
        send("set_environment");
    }
    FileDialog {
        id: environmentFile
        title: "Import spherical panorama"
        nameFilters: ["Panoramas (*.jpg *.jpeg *.png *.bmp)"]
        onAccepted: {
            root.imagePath=root.localPath(selectedFile);
            root.send("import_environment");
        }
    }
    property var builtInSetups: []
    function supportsSetup(setup) {
        return setup.layout.monitors.every(function(m) {
            return MonitorPresets.supported(m.width,m.height,root.graphicsLimits);
        });
    }
    property var savedSetups: []
    property string setupId: ""
    property string setupName: ""
    property string pendingSetupId: ""
    function chooseSetup(identity) {
        if (dirty) {pendingSetupId=identity;return;}
        send("use_setup", undefined, identity);
    }
    property int fps: 60
    property real curvature: 0
    property real workspaceDegrees: -1
    property bool workspaceFollow: false
    property var graphicsLimits: ({maxWidth:8192,maxHeight:8192,detected:false})
    property real geometryDistance: 5
    readonly property var angleLimits: CurvatureAngles.limits(monitors,curvature,geometryDistance,workspaceDegrees)
    property int spacing: 24
    property int activeCount: 0
    property bool viewing: false
    property bool directOutput: false
    property bool stereoOutput: false
    property bool laptopOffEnabled: false
    property var laptopDisplay: ({available:false,off:false,error:""})
    property bool spectatorEnabled: false
    property var performance: ({})
    property string viewerExit: ""
    property string controlsHint: ""
    property var glasses: ({})
    property bool confirmRecovery: false
    property string feedback: ""
    property bool feedbackError: false
    readonly property string pendingAction: requests.action
    property int activeTab: 0
    property var history: []
    readonly property var sdk: glasses.sdk || ({})
    readonly property bool canStart: loaded && !busy && !glasses.recovering && !!sdk.available && (directOutput || (!!glasses.displays && glasses.displays.length === 1))
    function selectTab(index) {
        activeTab = Math.max(0, Math.min(3, Number(index)));
        scroll.contentItem.contentY = 0;
        Qt.callLater(fit);
    }
    function notify(message, failed) {
        feedback = message;
        feedbackError = !!failed;
        if (message && (!history.length || history[0].message !== message)) {
            var entries = history.slice();
            entries.unshift({
                time: Qt.formatTime(new Date(), "hh:mm:ss"),
                message: message,
                failed: !!failed
            });
            history = entries.slice(0, 40);
        }
    }
    function requestRecovery() {
        confirmRecovery = true;
    }
    property string status: "Loading saved layout…"
    property bool error: false
    property real viewScale: 0.1
    property real offsetX: 40
    property real offsetY: 60
    property var monitors: []
    readonly property var current: monitors.length ? monitors[Math.min(selected, monitors.length - 1)] : ({
            width: 1920,
            height: 1080,
            x: 0,
            y: 0
        })
    readonly property real totalPixels: monitors.reduce(function (sum, m) {
        return sum + m.width * m.height;
    }, 0)
    readonly property bool opened: window.visible
    function open(payload) {
        window.visible = true;
        if (!backend.running)
            backend.running = true;
    }
    function snapshot() {
        return JSON.stringify({
            environment: environmentSettings,
            environments: environmentItems,
            builtInSetups: builtInSetups,
            setups: savedSetups,
            setupId: setupId,
            workspaceDegrees: workspaceDegrees, workspaceFollow: workspaceFollow,
            laptopOffEnabled: laptopOffEnabled,
            laptopDisplay: laptopDisplay,
            spectatorEnabled: spectatorEnabled,
            performance: performance,
            graphicsLimits: graphicsLimits,
            scrollHeight: scroll.contentHeight,
            viewportHeight: scroll.availableHeight,
            controls: controlDraft,
            controlsDirty: controlsDirty,
            monitors: monitors,
            status: status,
            error: error,
            busy: busy,
            active: activeCount,
            loaded: loaded,
            glasses: glasses,
            feedback: feedback,
            confirmation: confirmRecovery,
            tab: activeTab,
            viewing: viewing,
            direct: directOutput,
            dirty: dirty
        });
    }
    function close() {
        closingFromHost = true;
        window.visible = false;
        closingFromHost = false;
    }
    function hide() {
        if (shell)
            shell.hide("afruth.omarchy-xr");
        else
            close();
    }
    function localPath(url) {
        return decodeURIComponent(String(url).replace(/^file:\/\//, ""));
    }
    function send(action, enabled, selectedSetup, updateSetup) {
        if (!backend.running) return;
        if (!busy) { busySinceMs = Date.now(); backendSlow = false; }
        var requestId = requests.begin(action);
        if (!requestId) return;
        if (action !== "status") {
            error = false;
            notify(action === "check" ? "Checking glasses connection…" : action === "reinitialize" ? "Starting recovery — watch for the administrator prompt…" : action === "present_direct" ? (root.directOutput ? "Updating monitors in the running XR session…" : "Starting stereo and reserving the glasses…") : "Working…");
        }
        backend.write(JSON.stringify({
            requestId: requestId,
            action: action === "check" ? "status" : action,
            textSize: enabled,
            environment: environmentSettings,
            imagePath: imagePath,
            imageResolution: imageResolution,
            enabled: enabled === undefined ? true : enabled,
            controls: controlDraft,
            setupName: setupName,
            setupId: selectedSetup === undefined ? setupId : selectedSetup,
            updateSetup: !!updateSetup,
            layout: {
                version: 1,
                fps: fps,
                curvature: curvature,
                workspaceDegrees: workspaceDegrees,
                workspaceFollow: workspaceFollow,
                spacing: spacing,
                monitors: monitors
            },
            id: current.id
        }) + "\n");
    }
    function changed() {
        dirty = true;
        canvas.requestPaint();
    }
    function edit(key, value) {
        if (!monitors.length)
            return;
        if (key === "width" || key === "height") {
            resizeMonitor(key === "width" ? value : current.width, key === "height" ? value : current.height);
            return;
        }
        var copy = JSON.parse(JSON.stringify(monitors));
        copy[selected][key] = (key === "curvature" || key === "scale") ? value : Math.round(value);
        monitors = copy;
        changed();
    }
    function resizeMonitor(width, height) {
        if (!monitors.length || !MonitorPresets.supported(width,height,graphicsLimits)) return;
        try {
            monitors = MonitorSnap.resize(monitors,selected,width,height,spacing);
            dragSnap = null;
            changed();
        } catch (exception) {
            error = true;
            notify(String(exception.message));
        }
    }
    function moveMonitor(x, y) {
        var snap = MonitorSnap.place(monitors, selected, x, y, spacing, viewScale);
        dragSnap = snap;
        if (snap.x !== current.x || snap.y !== current.y) {
            var copy = JSON.parse(JSON.stringify(monitors));
            copy[selected].x = snap.x; copy[selected].y = snap.y;
            monitors = copy;
            changed();
        } else canvas.requestPaint();
    }
    function setCount(count) {
        count = Math.max(1, Math.min(16, count));
        var copy = JSON.parse(JSON.stringify(monitors));
        while (copy.length > count)
            copy.pop();
        var nextX = copy.reduce(function (n, m) {
            return Math.max(n, m.x + m.width + root.spacing);
        }, 0);
        while (copy.length < count) {
            copy.push({
                id: Date.now().toString(36) + "_" + copy.length,
                width: 1920,
                height: 1080,
                x: nextX,
                y: 0
            });
            nextX += 1920 + spacing;
        }
        monitors = copy;
        selected = Math.max(0, Math.min(selected, copy.length - 1));
        changed();
        fit();
    }
    function arrange(grid) {
        var copy = JSON.parse(JSON.stringify(monitors));
        var cols = grid ? Math.ceil(Math.sqrt(copy.length)) : copy.length;
        var x = 0, y = 0, rowHeight = 0;
        for (var i = 0; i < copy.length; i++) {
            if (i && i % cols === 0) {
                x = 0;
                y += rowHeight + spacing;
                rowHeight = 0;
            }
            copy[i].x = x;
            copy[i].y = y;
            x += copy[i].width + spacing;
            rowHeight = Math.max(rowHeight, copy[i].height);
        }
        monitors = copy;
        changed();
        fit();
    }
    function fit() {
        if (!monitors.length || canvas.width < 1)
            return;
        var l = Infinity, t = Infinity, r = -Infinity, b = -Infinity;
        monitors.forEach(function (m) {
            l = Math.min(l, m.x);
            t = Math.min(t, m.y);
            r = Math.max(r, m.x + m.width);
            b = Math.max(b, m.y + m.height);
        });
        viewScale = Math.min((canvas.width - 80) / (r - l), (canvas.height - 80) / (b - t));
        viewScale = Math.max(.001, viewScale);
        offsetX = (canvas.width - (r - l) * viewScale) / 2 - l * viewScale;
        offsetY = (canvas.height - (b - t) * viewScale) / 2 - t * viewScale;
        canvas.requestPaint();
    }
    function hit(x, y) {
        for (var i = monitors.length - 1; i >= 0; i--) {
            var m = monitors[i];
            if (x >= offsetX + m.x * viewScale && x <= offsetX + (m.x + m.width) * viewScale && y >= offsetY + m.y * viewScale && y <= offsetY + (m.y + m.height) * viewScale)
                return i;
        }
        return -1;
    }
    onSelectedChanged: canvas.requestPaint()
    onViewScaleChanged: canvas.requestPaint()
    onOffsetXChanged: canvas.requestPaint()
    onOffsetYChanged: canvas.requestPaint()
    Connections {
        target: Color
        function onBackgroundChanged() {
            canvas.requestPaint();
        }
        function onForegroundChanged() {
            canvas.requestPaint();
        }
    }

    Process {
        id: backend
        command: ["python3", "-B", root.localPath(Qt.resolvedUrl("backend.py")), "--renderer", root.localPath(Qt.resolvedUrl("../bin/omarchy-xr"))]
        stdinEnabled: true
        onStarted: {
            requests.reset();
            root.send("load");
        }
        stdout: SplitParser {
            onRead: function (line) {
                try {
                    var response = JSON.parse(line);
                    if (response.graphicsLimits) root.graphicsLimits=response.graphicsLimits;
                    var replyAction = requests.finish(response.requestId);
                    if (!root.busy) root.backendSlow = false;
                    if (!replyAction) return;
                    if (response.environment) {
                        root.environmentSettings=response.environment.settings;
                        root.environmentItems=response.environment.items;
                        root.canImportEnvironment=response.environment.canImport;
                    }
                    if (response.builtInSetups) root.builtInSetups=response.builtInSetups;
                    if (response.setups) {
                        root.savedSetups=response.setups.items;
                        root.setupId=response.setups.selected || "";
                        var picked=root.savedSetups.concat(root.builtInSetups).find(function(s){return s.id===root.setupId;});
                        if(picked) root.setupName=picked.name;
                    }
                    if (response.controls) {root.controlDraft=response.controls;root.controlsDirty=false;}
                    if (response.performance) root.performance=response.performance;
                    root.viewerExit = response.viewerExit || "";
                    root.controlsHint = response.controlsHint || "";
                    if (root.performance.geometryDistance > 0) root.geometryDistance=root.performance.geometryDistance;
                    root.spectatorEnabled = !!response.spectatorEnabled;
                    root.laptopOffEnabled = !!response.laptopOffEnabled;
                    if (response.laptopDisplay) root.laptopDisplay=response.laptopDisplay;
                    if (replyAction !== "status" || !response.ok)
                        root.error = !response.ok;
                    var oldRecovery = root.glasses.recoveryMessage || "";
                    var oldSDK = (root.glasses.sdk || {}).message || "";
                    if (response.glasses)
                        root.glasses = response.glasses;
                    if (response.restorationError)
                        root.notify(response.restorationError, true);
                    if (!response.ok)
                        root.notify(response.message || "Action failed", true);
                    else if (replyAction === "check") {
                        root.notify("Checked at " + Qt.formatTime(new Date(), "hh:mm:ss") + ": " + (root.glasses.usb ? "USB detected" : "USB not detected") + " · " + (root.glasses.dedicatedDisplay ? "Dedicated XR on " + root.glasses.dedicatedDisplay : root.glasses.detectionError || (root.glasses.displays.length ? "Video on " + root.glasses.displays.join(", ") : "No VITURE video output")));
                    } else if (response.message)
                        root.notify(response.message);
                    if (response.ok && root.glasses.recoveryMessage && root.glasses.recoveryMessage !== oldRecovery)
                        root.notify(root.glasses.recoveryMessage);
                    if (response.ok && root.glasses.sdk && root.glasses.sdk.message !== oldSDK) {
                        root.notify(root.glasses.sdk.message, root.glasses.sdk.error);
                        root.status = root.glasses.sdk.message;
                    }
                    if (replyAction === "load" && response.ok)
                        root.feedback = "";
                    root.activeCount = response.active || 0;
                    root.viewing = !!response.viewing;
                    root.directOutput = !!response.direct;
                    root.stereoOutput = !!response.stereo;
                    if (response.layout) {
                        root.monitors = response.layout.monitors;
                        root.fps = response.layout.fps;
                        root.curvature = response.layout.curvature || 0;
                        root.workspaceDegrees = response.layout.workspaceDegrees === undefined ? -1 : response.layout.workspaceDegrees;
                        root.workspaceFollow = !!response.layout.workspaceFollow;
                        root.spacing = response.layout.spacing || 24;
                        root.loaded = true;
                        root.dirty = response.layoutDirty === undefined ? response.message === "Layout saved" : response.layoutDirty;
                        root.status = "Arrange your monitors, then Apply.";
                        Qt.callLater(root.fit);
                    }
                    if (response.message) {
                        root.status = response.message;
                        if (response.ok && response.message.indexOf("ready") >= 0)
                            root.dirty = false;
                    }
                } catch (e) {
                    requests.reset();
                    root.backendSlow = false;
                    root.error = true;
                    root.status = "Could not read backend response: " + e;
                    root.notify(root.status, true);
                }
            }
        }
        stderr: SplitParser {
            onRead: function (line) {
                console.warn("XR Studio: " + line);
            }
        }
        onExited: function (code) {
            requests.reset();
            root.backendSlow = false;
            root.loaded = false;
            root.error = true;
            root.status = "Monitor manager stopped (" + code + "). Reopen the panel to retry.";
            root.notify(root.status, true);
        }
    }
    Timer {
        interval: root.opened ? 3000 : 10000
        repeat: true
        running: root.loaded && (root.opened || root.viewing)
        onTriggered: if (!root.busy) root.send("status")
    }
    Timer {
        interval: 1000
        repeat: true
        running: root.busy
        onTriggered: if (root.busySinceMs > 0 && Date.now() - root.busySinceMs > 20000) root.backendSlow = true
    }
    component Label: Text {
        color: Color.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        textFormat: Text.PlainText
    }
    component Action: Ui.Button {
        focusable: true
        bordered: true
        opacity: enabled ? 1 : .4
        Accessible.role: Accessible.Button
        Accessible.name: text
    }
    component Hint: Label {
        color: Qt.alpha(Color.foreground, .68)
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    component Heading: Label {
        font.pixelSize: Style.font.title
        font.bold: true
        Layout.fillWidth: true
    }
    component Card: Rectangle {
        default property alias content: body.data
        Layout.fillWidth: true
        implicitHeight: body.implicitHeight + 40
        color: Qt.alpha(Color.foreground, .025)
        border.color: Qt.alpha(Color.foreground, .12)
        radius: Style.cornerRadius
        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16
        }
    }
    component Metric: ColumnLayout {
        id: metric
        property string caption
        property string value
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        spacing: 8
        Label {
            Layout.fillWidth: true
            text: metric.caption
            color: Qt.alpha(Color.foreground, .68)
            font.pixelSize: Style.font.bodySmall
        }
        Label {
            Layout.fillWidth: true
            text: metric.value
            font.bold: true
            font.pixelSize: Style.font.title
            wrapMode: Text.WordWrap
        }
    }

    FloatingWindow {
        id: window
        title: "XR Monitor Studio"
        visible: false
        color: Color.background
        implicitWidth: 1100
        implicitHeight: 820
        minimumSize: Qt.size(780, 600)
        onVisibleChanged: if (!visible && !root.closingFromHost && root.shell)
            root.shell.hide("afruth.omarchy-xr")
        FocusScope {
            id: frame
            anchors.fill: parent
            focus: true
            Keys.onEscapePressed: root.hide()
            Shortcut {
                sequence: "Ctrl+1"
                enabled: window.visible
                onActivated: root.selectTab(0)
            }
            Shortcut {
                sequence: "Ctrl+2"
                enabled: window.visible
                onActivated: root.selectTab(1)
            }
            Shortcut {
                sequence: "Ctrl+3"
                enabled: window.visible
                onActivated: root.selectTab(2)
            }
            Shortcut {
                sequence: "Ctrl+4"
                enabled: window.visible
                onActivated: root.selectTab(3)
            }
            QQC.Popup {
                id: recoveryDialog
                parent: frame
                x: (frame.width - width) / 2
                y: (frame.height - height) / 2
                width: Math.min(500, frame.width - 40)
                padding: Style.space(20)
                modal: true
                focus: true
                visible: root.confirmRecovery
                closePolicy: QQC.Popup.CloseOnEscape
                onClosed: root.confirmRecovery = false
                background: Rectangle {
                    color: Color.background
                    border.color: Color.accent
                }
                contentItem: ColumnLayout {
                    spacing: Style.space(16)
                    Label {
                        text: "Reinitialize glasses connection?"
                        font.bold: true
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        text: "This restarts the USB-C controller. Other USB-C devices may briefly disconnect. An administrator prompt will appear."
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Action {
                            text: "Cancel"
                            onClicked: {
                                root.confirmRecovery = false;
                                root.notify("Reinitialization cancelled. No changes made.");
                            }
                        }
                        Action {
                            text: "Reinitialize"
                            enabled: !root.busy && !!root.glasses.canReset
                            onClicked: {
                                root.confirmRecovery = false;
                                root.send("reinitialize");
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 18
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 4
                        Label {
                            text: "XR Monitor Studio"
                            font.pixelSize: Style.font.heading
                            font.bold: true
                        }
                        Label {
                            text: "Virtual monitor configuration"
                            color: Qt.alpha(Color.foreground, .68)
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: root.viewing ? Color.accent : Qt.alpha(Color.foreground, .68)
                    }
                    Label {
                        text: root.directOutput ? "Stereo live" : root.viewing ? "Preview live" : (root.viewerExit || "Standby")
                    }
                    Action {
                        text: "Hide"
                        tooltipText: "Park on the top bar; XR stays running"
                        onClicked: root.hide()
                    }
                }
                Label {
                    visible: root.controlsHint !== ""
                    text: root.controlsHint
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Color.urgent
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: ["Controls", "Monitors", "Environment", "Utilities & Debug"]
                        Action {
                            required property int index
                            required property string modelData
                            text: modelData
                            Layout.fillWidth: true
                            selected: root.activeTab === index
                            verticalPadding: 12
                            tooltipText: "Ctrl+" + (index + 1)
                            Accessible.role: Accessible.PageTab
                            Accessible.selected: selected
                            onClicked: root.selectTab(index)
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Math.max(notice.implicitHeight, dismissNotice.visible ? dismissNotice.implicitHeight : 0) + 24
                    visible: !!root.feedback
                    color: Qt.alpha(root.feedbackError ? Color.urgent : Color.accent, .07)
                    border.color: Qt.alpha(root.feedbackError ? Color.urgent : Color.accent, .4)
                    radius: Style.cornerRadius
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        Label {
                            id: notice
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                            text: root.feedback
                            color: root.feedbackError ? Color.urgent : Color.foreground
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }
                        Action {
                            id: dismissNotice
                            Layout.alignment: Qt.AlignVCenter
                            text: "Dismiss"
                            visible: !root.busy
                            bordered: false
                            onClicked: root.feedback = ""
                        }
                    }
                }
                QQC.ScrollView {
                    id: scroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: tabContent.implicitHeight
                    QQC.ScrollBar.vertical.policy: QQC.ScrollBar.AsNeeded
                    QQC.ScrollBar.vertical.active: true
                    QQC.ScrollBar.horizontal.policy: QQC.ScrollBar.AlwaysOff
                    ColumnLayout {
                        id: tabContent
                        width: scroll.availableWidth
                        height: implicitHeight
                        spacing: 20
                        ColumnLayout {
                            visible: root.activeTab === 0
                            Layout.fillWidth: true
                            spacing: 20
                            Card {
                                RowLayout {
                                    Layout.fillWidth: true
                                    Heading { text: "Laptop display"; Layout.fillWidth: true }
                                    Ui.ToggleSwitch {
                                        checked: root.laptopOffEnabled
                                        busy: root.busy
                                        enabled: root.loaded && !root.busy && (root.laptopDisplay.available || root.laptopOffEnabled)
                                        onToggled: root.send("set_laptop_off", !root.laptopOffEnabled)
                                    }
                                }
                                Hint { text: "Turn off during stereo. Restore on exit or glasses disconnect." }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.laptopDisplay.off ? "Laptop display off" : root.laptopDisplay.available ? "Laptop display on" : "No built-in display detected"
                                    }
                                    Action {
                                        text: "Restore laptop display"
                                        enabled: root.loaded && !root.busy && (root.laptopDisplay.off || !!root.laptopDisplay.error)
                                        onClicked: root.send("restore_laptop")
                                    }
                                }
                                Hint {
                                    visible: !!root.laptopDisplay.error
                                    text: root.laptopDisplay.error || ""
                                    color: Color.urgent
                                }
                            }
                            Card {
                                RowLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Label {
                                            text: "YOUR XR SESSION"
                                            color: Color.accent
                                            font.pixelSize: Style.font.bodySmall
                                            font.letterSpacing: 2
                                        }
                                        Heading {
                                            text: root.directOutput ? "Stereo active" : root.viewing ? "Preview active" : "XR session"
                                        }
                                        Hint {
                                            text: root.directOutput ? "Use Recenter to set the forward direction." : "Start stereo to display the monitor layout in the glasses."
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Metric {
                                        caption: "WORKSPACE"
                                        value: root.monitors.length + (root.monitors.length === 1 ? " monitor" : " monitors")
                                    }
                                    Metric {
                                        caption: "GLASSES"
                                        value: root.directOutput ? "Reserved for XR" : root.glasses.usb ? "Connected" : "Not connected"
                                    }
                                    Metric {
                                        caption: "HEAD TRACKING"
                                        value: root.sdk.tracking ? "Live" : root.sdk.communication ? "Waiting" : "Standby"
                                    }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Action {
                                        text: root.busy && root.pendingAction === "present_direct" ? "Starting…" : root.directOutput ? (root.dirty ? "Apply changes to XR" : "Stereo is running") : "Start stereo"
                                        selected: true
                                        verticalPadding: 12
                                        enabled: root.canStart && (!root.directOutput || root.dirty)
                                        onClicked: root.send("present_direct")
                                    }
                                    Action {
                                        text: "Close viewer"
                                        enabled: root.viewing && !root.busy
                                        verticalPadding: 12
                                        onClicked: root.send("stop_viewer")
                                    }
                                    Action {
                                        text: "Edit monitors →"
                                        verticalPadding: 12
                                        onClicked: root.selectTab(1)
                                    }
                                }
                                Hint {
                                    visible: !root.directOutput && !root.canStart && !root.busy
                                    text: !root.loaded ? "Loading your workspace…" : !root.sdk.available ? "Install the VITURE SDK in Utilities & Debug." : !root.glasses.usb ? "Connect the glasses to a USB-C video port." : "No glasses video output. Check Utilities & Debug."
                                }
                            }
                            Card {
                                Heading {
                                    text: "View controls"
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    enabled: root.viewing && !root.busy
                                    Action {
                                        text: "Recenter"
                                        iconText: "◎"
                                        verticalPadding: 14
                                        tooltipText: "Set the direction you are looking as forward"
                                        onClicked: root.send("recenter")
                                    }
                                    Action {
                                        text: "Fit workspace"
                                        verticalPadding: 14
                                        tooltipText: "Bring the whole workspace into view"
                                        onClicked: root.send("fit")
                                    }
                                    Action {
                                        text: "Fit selected monitor"
                                        verticalPadding: 14
                                        tooltipText: (root.controlDraft.fit_target || "No hotkey") + " · Fit the selected monitor by height"
                                        onClicked: root.send("fit_target")
                                    }
                                    Action {
                                        text: "− Zoom out"
                                        verticalPadding: 14
                                        onClicked: root.send("zoom_out")
                                    }
                                    Action {
                                        text: "+ Zoom in"
                                        verticalPadding: 14
                                        onClicked: root.send("zoom_in")
                                    }
                                }
                                Hint {
                                    visible: !root.viewing
                                    text: "Requires an active viewer."
                                }
                            }
                            Card {
                                Heading { text: "Input controls" }
                                Hint { text: "Apply without restarting XR. Leave a shortcut blank to disable it." }
                                Ui.Dropdown {
                                    label: "Fingers for zoom swipe"
                                    options: ["3", "5"]
                                    value: String(root.controlDraft.fingers)
                                    onChanged: function(value) {root.setControl("fingers",Number(value));}
                                }
                                Hint { text: "Flick up: fit monitor. Flick down: fit workspace. Hold swipe: zoom. Three-finger double tap: recenter. Four-finger swipe: pan the selected monitor." }
                                Repeater {
                                    model: [{key:"recenter",title:"Recenter camera"},{key:"fit_all",title:"Fit workspace"},{key:"fit_target",title:"Fit selected monitor"},{key:"zoom_in",title:"Zoom in"},{key:"zoom_out",title:"Zoom out"}]
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Label { text: modelData.title; Layout.preferredWidth: 170 }
                                        Ui.TextField {
                                            Layout.fillWidth: true
                                            text: root.controlDraft[modelData.key] || ""
                                            placeholderText: "e.g. CTRL + ALT + R"
                                            Accessible.name: modelData.title + " hotkey"
                                            onTextEdited: root.setControl(modelData.key,text)
                                        }
                                    }
                                }
                                RowLayout {
                                    Action {
                                        text: root.pendingAction === "save_controls" ? "Applying…" : "Apply controls"
                                        enabled: root.controlsDirty && !root.busy
                                        onClicked: root.send("save_controls")
                                    }
                                    Action {
                                        text: "Reset to defaults"
                                        enabled: !root.busy
                                        onClicked: {root.controlDraft={fingers:3,fit_all:"CTRL + Up",fit_target:"CTRL + Down",recenter:"",zoom_in:"",zoom_out:""};root.controlsDirty=true;}
                                    }
                                    Hint { text: root.controlsDirty ? "Unsaved changes" : "Saved" }
                                }
                            }
                            Hint {
                                text: "Closing the viewer keeps virtual monitors active."
                            }
                        }
                        ColumnLayout {
                            visible: root.activeTab === 1
                            Layout.fillWidth: true
                            spacing: 16
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Heading {
                                        text: "Monitor layout"
                                    }
                                    Hint {
                                        text: "Select a monitor to edit. Apply to update the layout."
                                    }
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: root.dirty ? "Unapplied changes" : "Layout ready"
                                    color: root.dirty ? Color.accent : Qt.alpha(Color.foreground, .68)
                                }
                            }
                            Hint {
                                text: "Limit: " + root.graphicsLimits.maxWidth + " × " + root.graphicsLimits.maxHeight + " px/monitor · "
                                    + (root.totalPixels/1000000).toFixed(1) + " MP · " + (root.totalPixels*4/1048576).toFixed(0) + " MiB/frame"
                                    + (!root.graphicsLimits.detected ? " · Hardware limit unverified" : !root.graphicsLimits.complete ? " · Partial GPU detection" : "")
                            }
                            GridLayout {
                                columns: width >= 900 ? 7 : 4
                                Layout.fillWidth: true
                                rowSpacing: 12
                                columnSpacing: 12
                                enabled: root.loaded && !root.busy
                                Ui.NumberField {
                                    Layout.alignment: Qt.AlignBottom
                                    label: "Monitors"
                                    from: 1
                                    to: 16
                                    value: root.monitors.length
                                    fieldWidth: 130
                                    onModified: function (value) {
                                        root.setCount(value);
                                    }
                                }
                                Action {
                                    Layout.alignment: Qt.AlignBottom
                                    text: "+ Add"
                                    onClicked: root.setCount(root.monitors.length + 1)
                                }
                                Action {
                                    Layout.alignment: Qt.AlignBottom
                                    text: "Remove selected"
                                    enabled: root.monitors.length > 1
                                    onClicked: {
                                        var c = root.monitors.slice();
                                        c.splice(root.selected, 1);
                                        root.monitors = c;
                                        root.selected = Math.min(root.selected, c.length - 1);
                                        root.changed();
                                        root.fit();
                                    }
                                }
                                Ui.NumberField {
                                    Layout.alignment: Qt.AlignBottom
                                    label: "Capture fps"
                                    from: 1
                                    to: 120
                                    value: root.fps
                                    fieldWidth: 110
                                    onModified: function (value) {
                                        root.fps = value;
                                        root.changed();
                                    }
                                }
                                RowLayout {
                                    Layout.columnSpan: parent.columns === 7 ? 3 : 4
                                    Layout.alignment: Qt.AlignLeft | Qt.AlignBottom
                                    spacing: 8
                                    Action { text: "Row"; onClicked: root.arrange(false) }
                                    Action { text: "Grid"; onClicked: root.arrange(true) }
                                    Action { text: "Fit"; onClicked: root.fit() }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                enabled: root.loaded && !root.busy
                                AngleField {
                                    label: "Workspace wrap (°)"
                                    amount: (root.workspaceDegrees >= 0 ? root.workspaceDegrees : root.curvature * root.angleLimits.workspace / 100) / 3.6
                                    maximumDegrees: 360
                                    fieldWidth: 190
                                    onAmountEdited: function (value) {
                                        root.workspaceDegrees = Math.round(value * 3.6);
                                        root.changed();
                                    }
                                }
                                Ui.NumberField {
                                    Layout.alignment: Qt.AlignBottom
                                    label: "Spacing (pixels)"
                                    from: 1
                                    to: 8192
                                    value: root.spacing
                                    fieldWidth: 150
                                    onModified: function (value) {
                                        root.spacing = value;
                                        root.changed();
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }
                            QQC.CheckBox {
                                text: "Monitors follow workspace curvature"
                                checked: root.workspaceFollow
                                Layout.alignment: Qt.AlignBottom
                                font.family: Style.font.family
                                palette.windowText: Color.foreground
                                palette.highlight: Color.accent
                                onToggled: { root.workspaceFollow=checked; root.changed(); }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumHeight: Math.max(430,monitorInspector.implicitHeight)
                                Layout.preferredHeight: Layout.minimumHeight
                                spacing: Style.space(16)
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    color: Color.background
                                    border.color: Qt.alpha(Color.foreground, .18)
                                    radius: Style.cornerRadius
                                    clip: true
                                    Canvas {
                                        id: canvas
                                        anchors.fill: parent
                                        onWidthChanged: root.fit()
                                        onHeightChanged: root.fit()
                                        onPaint: {
                                            var c = getContext("2d");
                                            c.reset();
                                            c.fillStyle = Color.background;
                                            c.fillRect(0, 0, width, height);
                                            c.strokeStyle = Qt.alpha(Color.foreground, .1);
                                            c.lineWidth = 1;
                                            for (var gx = 0; gx < width; gx += 24) {
                                                c.beginPath();
                                                c.moveTo(gx, 0);
                                                c.lineTo(gx, height);
                                                c.stroke();
                                            }
                                            for (var gy = 0; gy < height; gy += 24) {
                                                c.beginPath();
                                                c.moveTo(0, gy);
                                                c.lineTo(width, gy);
                                                c.stroke();
                                            }
                                            root.monitors.forEach(function (m, i) {
                                                var x = root.offsetX + m.x * root.viewScale, y = root.offsetY + m.y * root.viewScale, w = m.width * root.viewScale, h = m.height * root.viewScale;
                                                c.fillStyle = Qt.alpha(Color.accent, i === root.selected ? .23 : .08);
                                                c.fillRect(x, y, w, h);
                                                c.strokeStyle = i === root.selected ? Color.accent : Qt.alpha(Color.foreground, .68);
                                                c.lineWidth = i === root.selected && root.dragSnap && (root.dragSnap.snapX || root.dragSnap.snapY) ? 5 : i === root.selected ? 3 : 1;
                                                c.strokeRect(x, y, w, h);
                                                c.fillStyle = Color.foreground;
                                                c.font = "bold 16px monospace";
                                                c.fillText(String(i + 1), x + 10, y + 23);
                                                if (w > 115 && h > 60) {
                                                    c.font = "12px monospace";
                                                    c.fillText(m.width + " × " + m.height, x + 10, y + 44);
                                                }
                                            });
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                            preventStealing: true
                                            enabled: root.loaded && !root.busy
                                            property real startX
                                            property real startY
                                            property real originalX
                                            property real originalY
                                            property int dragging: -1
                                            onPressed: function (mouse) {
                                                root.dragSnap = null;
                                                startX = mouse.x;
                                                startY = mouse.y;
                                                dragging = mouse.button === Qt.LeftButton ? root.hit(mouse.x, mouse.y) : -1;
                                                if (dragging >= 0) {
                                                    root.selected = dragging;
                                                    originalX = root.current.x;
                                                    originalY = root.current.y;
                                                } else {
                                                    originalX = root.offsetX;
                                                    originalY = root.offsetY;
                                                }
                                            }
                                            onPositionChanged: function (mouse) {
                                                if (!pressed)
                                                    return;
                                                if (dragging >= 0) {
                                                    root.moveMonitor(originalX + (mouse.x - startX) / root.viewScale,
                                                                     originalY + (mouse.y - startY) / root.viewScale);
                                                } else {
                                                    root.offsetX = originalX + mouse.x - startX;
                                                    root.offsetY = originalY + mouse.y - startY;
                                                }
                                            }
                                            onReleased: { dragging=-1; root.dragSnap=null; canvas.requestPaint(); }
                                            onCanceled: { dragging=-1; root.dragSnap=null; canvas.requestPaint(); }
                                            onWheel: function (wheel) {
                                                if (!(wheel.modifiers & Qt.ControlModifier)) { wheel.accepted=false; return; }
                                                var old = root.viewScale;
                                                root.viewScale = Math.max(.001, Math.min(1, old * (wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15)));
                                                root.offsetX = wheel.x - (wheel.x - root.offsetX) * root.viewScale / old;
                                                root.offsetY = wheel.y - (wheel.y - root.offsetY) * root.viewScale / old;
                                            }
                                        }
                                    }
                                }
                                ColumnLayout {
                                    id: monitorInspector
                                    Layout.minimumWidth: 260
                                    Layout.maximumWidth: 260
                                    Layout.preferredWidth: 260
                                    Layout.alignment: Qt.AlignTop
                                    enabled: root.loaded && !root.busy
                                    Label {
                                        text: "Monitor " + (root.selected + 1)
                                        font.bold: true
                                        font.pixelSize: Style.font.title
                                    }
                                    Ui.Dropdown {
                                        Layout.fillWidth: true
                                        label: "Resolution"
                                        value: MonitorPresets.match(root.current)
                                        options: MonitorPresets.options(root.graphicsLimits)
                                        onChanged: function(value) {
                                            var preset = MonitorPresets.find(value);
                                            if (preset) root.resizeMonitor(preset.width,preset.height);
                                        }
                                    }
                                    Action {
                                        text: "Swap width / height"
                                        enabled: MonitorPresets.supported(root.current.height,root.current.width,root.graphicsLimits)
                                        onClicked: root.resizeMonitor(root.current.height,root.current.width)
                                    }
                                    Ui.Dropdown {
                                        Layout.fillWidth: true
                                        label: "Scale"
                                        value: String(root.current.scale || 1)
                                        options: ["1", "1.25", "1.6", "2", "3", "4"]
                                        onChanged: function(value) { root.edit("scale",Number(value)); }
                                    }
                                    Label { text: "Brightness · " + Math.round(monitorBrightness.liveValue) + "%" }
                                    Ui.PanelSlider {
                                        id: monitorBrightness
                                        Layout.fillWidth: true
                                        minimum: 1; maximum: 100; step: 1; integer: true
                                        value: root.current.brightness === undefined ? 100 : root.current.brightness
                                        onMoved: function(value) { root.edit("brightness",value); }
                                    }
                                    Ui.NumberField {
                                        label: "Width (pixels)"
                                        from: 320
                                        to: root.graphicsLimits.maxWidth
                                        stepSize: 80
                                        value: root.current.width
                                        fieldWidth: 190
                                        onModified: function (value) {
                                            root.edit("width", value);
                                        }
                                    }
                                    Ui.NumberField {
                                        label: "Height (pixels)"
                                        from: 200
                                        to: root.graphicsLimits.maxHeight
                                        stepSize: 80
                                        value: root.current.height
                                        fieldWidth: 190
                                        onModified: function (value) {
                                            root.edit("height", value);
                                        }
                                    }
                                    Ui.NumberField {
                                        label: "X position"
                                        from: -100000
                                        to: 100000
                                        stepSize: 20
                                        value: root.current.x
                                        fieldWidth: 190
                                        onModified: function (value) {
                                            root.edit("x", value);
                                        }
                                    }
                                    Ui.NumberField {
                                        label: "Y position"
                                        from: -100000
                                        to: 100000
                                        stepSize: 20
                                        value: root.current.y
                                        fieldWidth: 190
                                        onModified: function (value) {
                                            root.edit("y", value);
                                        }
                                    }
                                    AngleField {
                                        label: root.workspaceFollow ? "Surface bend (workspace)" : "Surface bend (°)"
                                        enabled: !root.workspaceFollow
                                        amount: root.current.curvature || 0
                                        maximumDegrees: root.angleLimits.surfaces[root.selected] || 0
                                        fieldWidth: 190
                                        onAmountEdited: function (value) {
                                            root.edit("curvature", value);
                                        }
                                    }
                                }
                            }
                            Ui.Dropdown {
                                label: "Text size (all desktops)"
                                value: String(Math.round(Style.font.baseSize))
                                options: ["9", "10", "11", "12", "14", "16", "20"]
                                enabled: root.loaded && !root.busy
                                onChanged: function(value) { root.send("set_text_size",Number(value)); }
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: root.dragSnap && root.dragSnap.blocked ? "Move blocked: insufficient spacing." : root.dragSnap && (root.dragSnap.snapX || root.dragSnap.snapY) ? "Snapped to nearby monitor · " + root.spacing + " px minimum gutter" : "Drag: snap to monitors or grid · Empty area: pan · Ctrl+scroll: zoom"
                                color: Qt.alpha(Color.foreground, .68)
                                font.pixelSize: Style.font.bodySmall
                            }

                            Hint {
                                text: "0° = flat. Angles vary with workspace zoom." + (root.viewing && root.performance.geometryDistance ? "" : " Estimated while viewer is closed.")
                            }
                            Card {
                                Heading { text: "Built-in setups" }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Repeater {
                                        model: root.builtInSetups
                                        delegate: Action {
                                            required property var modelData
                                            text: modelData.name + (root.setupId === modelData.id && root.dirty ? " · edited" : "")
                                            selected: root.setupId === modelData.id
                                            enabled: root.loaded && !root.busy && root.supportsSetup(modelData)
                                            tooltipText: modelData.description + (root.supportsSetup(modelData) ? " · 30 px spacing" : " · Exceeds this computer's resolution limit")
                                            onClicked: root.chooseSetup(modelData.id)
                                        }
                                    }
                                }
                                Hint { text: "Select to apply. Customize and save your own copy." }
                            }
                            Card {
                                visible: root.activeTab === 1
                                Heading { text: "Saved setups" }
                                Hint { text: "Save and switch monitor layouts." }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Repeater {
                                        model: root.savedSetups
                                        delegate: Action {
                                            required property var modelData
                                            text: modelData.name + (root.setupId === modelData.id && root.dirty ? " · edited" : "")
                                            selected: root.setupId === modelData.id
                                            enabled: !root.busy
                                            tooltipText: modelData.layout.monitors.length + " monitors · " + modelData.layout.fps + " fps · Click to switch"
                                            onClicked: root.chooseSetup(modelData.id)
                                        }
                                    }
                                }
                                Hint { visible: !root.savedSetups.length; text: "No saved setups." }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Ui.TextField {
                                        Layout.fillWidth: true
                                        text: root.setupName
                                        placeholderText: "Setup name, e.g. Coding"
                                        Accessible.name: "Setup name"
                                        onTextEdited: root.setupName=text
                                    }
                                    Action {
                                        text: "Save new"
                                        enabled: root.loaded && !root.busy && !!root.setupName.trim()
                                        onClicked: root.send("save_setup")
                                    }
                                    Action {
                                        text: "Update selected"
                                        enabled: root.loaded && !root.busy && !!root.setupId && !root.setupId.startsWith("builtin:") && !!root.setupName.trim()
                                        onClicked: root.send("save_setup", undefined, root.setupId, true)
                                    }
                                }
                                Hint { text: "Selecting applies a setup. Saving stores the current draft." }
                                ColumnLayout {
                                    visible: !!root.pendingSetupId
                                    Layout.fillWidth: true
                                    Hint { text: "Switching discards unapplied changes." }
                                    RowLayout {
                                        Action {
                                            text: "Discard edits and switch"
                                            enabled: !root.busy
                                            onClicked: {var id=root.pendingSetupId;root.pendingSetupId="";root.send("use_setup",undefined,id);}
                                        }
                                        Action { text: "Keep editing"; onClicked: root.pendingSetupId="" }
                                    }
                                }
                            }
                        }
                        ColumnLayout {
                            visible: root.activeTab === 2
                            Layout.fillWidth: true
                            spacing: 20
                            Card {
                                Heading { text: "Environment" }
                                Hint { text: "360° background · Changes apply live." }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    Action {
                                        text: "Black background"
                                        selected: !root.environmentSettings.id
                                        enabled: root.loaded && !root.busy
                                        onClicked: root.setEnvironment("id", "")
                                    }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    Repeater {
                                        model: root.environmentItems
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: 190; height: 133
                                            activeFocusOnTab: true
                                            Keys.onReturnPressed: if (!root.busy) root.chooseEnvironment(modelData.id)
                                            Keys.onSpacePressed: if (!root.busy) root.chooseEnvironment(modelData.id)
                                            radius: Style.cornerRadius
                                            color: Color.background
                                            border.width: root.environmentSettings.id === modelData.id || activeFocus ? 2 : 1
                                            border.color: root.environmentSettings.id === modelData.id ? Color.accent : Qt.alpha(Color.foreground,.2)
                                            Image {
                                                x: 6; y: 6; width: parent.width-12; height: 89
                                                source: modelData.thumbnail
                                                asynchronous: true
                                                fillMode: Image.PreserveAspectFit
                                            }
                                            Label {
                                                x: 8; y: 102; width: parent.width-16
                                                text: modelData.name
                                                elide: Text.ElideRight
                                                font.pixelSize: Style.font.bodySmall
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                enabled: root.loaded && !root.busy
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.setEnvironment("id",modelData.id)
                                            }
                                            Accessible.role: Accessible.Button
                                            Accessible.name: modelData.name
                                            Accessible.onPressAction: if (!root.busy) root.setEnvironment("id",modelData.id)
                                        }
                                    }
                                }
                                Label { text: "Brightness · " + Math.round(skyBrightness.liveValue) + "%" }
                                Ui.PanelSlider {
                                    id: skyBrightness
                                    Layout.fillWidth: true
                                    minimum: 0; maximum: 100; step: 1; integer: true
                                    value: root.environmentSettings.brightness
                                    enabled: root.loaded && !root.busy
                                    onReleased: function(value) { root.setEnvironment("brightness",value); }
                                }
                                Ui.NumberField {
                                    label: "Rotation (°)"
                                    from: -180; to: 180; stepSize: 5
                                    value: root.environmentSettings.rotation
                                    enabled: root.loaded && !root.busy
                                    onModified: function(value) { root.setEnvironment("rotation",value); }
                                }
                                Hint {
                                    visible: !!root.performance.environmentError
                                    text: root.performance.environmentError || ""
                                    color: Color.urgent
                                }
                            }
                            Card {
                                Heading { text: "Import panorama" }
                                Hint { text: "2:1 JPEG, PNG or BMP. Images stay on this computer." }
                                RowLayout {
                                    spacing: 12
                                    Ui.Dropdown {
                                        label: "Maximum resolution"
                                        value: String(root.imageResolution)
                                        options: [{value:"4096",label:"4K · Recommended"},{value:"8192",label:"8K"}]
                                        onChanged: function(value) { root.imageResolution=Number(value); }
                                    }
                                    Action {
                                        Layout.alignment: Qt.AlignBottom
                                        text: "Choose image…"
                                        enabled: root.loaded && !root.busy && root.canImportEnvironment
                                        onClicked: environmentFile.open()
                                    }
                                }
                                Hint {
                                    visible: !root.canImportEnvironment
                                    text: "Import requires ImageMagick: sudo pacman -S imagemagick"
                                }
                            }
                        }
                        ColumnLayout {
                            visible: root.activeTab === 3
                            Layout.fillWidth: true
                            spacing: 20
                            Heading {
                                text: "Connection & diagnostics"
                            }
                            Hint {
                                text: "Connection status and recovery tools."
                            }
                            Card {
                                RowLayout {
                                    Layout.fillWidth: true
                                    Heading { text: "Mono window for OBS"; Layout.fillWidth: true }
                                    Ui.ToggleSwitch {
                                        checked: root.spectatorEnabled
                                        busy: root.busy
                                        onToggled: root.send("set_spectator", !root.spectatorEnabled)
                                    }
                                }
                                Hint {
                                    text: "Separate mono recording window, up to 30 fps. Opens with stereo."
                                }
                                Hint {
                                    text: root.performance.spectatorError ? root.performance.spectatorError : root.performance.spectator ? "Active · Capture ‘Omarchy XR — Mono spectator’ in OBS." : root.spectatorEnabled ? (root.directOutput ? "Window closed or opening." : "Enabled for the next stereo session.") : "Off"
                                }
                                Action {
                                    visible: root.directOutput && root.spectatorEnabled && !root.performance.spectator
                                    text: "Reopen mono window"
                                    enabled: !root.busy
                                    onClicked: root.send("set_spectator", true)
                                }
                            }
                            Card {
                                Heading {
                                    text: "Glasses connection"
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            text: "Glasses: " + (root.glasses.usb ? "USB detected" : "USB not detected") + " · " + (root.glasses.dedicatedDisplay ? "Dedicated XR on " + root.glasses.dedicatedDisplay : root.glasses.detectionError || (root.glasses.displays && root.glasses.displays.length ? "Video on " + root.glasses.displays.join(", ") : "No VITURE video output"))
                                        }
                                        Action {
                                            text: "Check connection"
                                            enabled: root.loaded && !root.busy
                                            onClicked: root.send("check")
                                        }
                                        Action {
                                            text: root.glasses.recovering ? "Reinitializing…" : "Reinitialize USB-C…"
                                            enabled: root.loaded && !root.busy && !!root.glasses.canReset
                                            onClicked: root.requestRecovery()
                                        }
                                    }
                                    ColumnLayout {
                                        id: sdkControls
                                        Layout.fillWidth: true
                                        readonly property var sdk: root.glasses.sdk || ({})
                                        Label {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            text: "SDK: " + (!sdkControls.sdk.available ? "not installed" : sdkControls.sdk.communication ? "communicating" : "disconnected") + " · Tracking: " + (sdkControls.sdk.tracking ? "receiving (" + sdkControls.sdk.samples + " samples)" : "no recent samples") + (sdkControls.sdk.displayMode !== undefined && sdkControls.sdk.displayMode !== null ? " · Mode: 0x" + sdkControls.sdk.displayMode.toString(16) : "")
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            text: root.directOutput ? "Presentation: dedicated DRM output · stereo SBS · native desktop cursor" : sdkControls.sdk.nativeDof === false ? "Presentation: host-rendered on the glasses’ display · on-device native tracking unsupported · stereo not enabled" : "Presentation: host-rendered on the glasses’ display · stereo not enabled"
                                            color: Qt.alpha(Color.foreground, .68)
                                        }
                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            Action {
                                                text: "Get SDK"
                                                visible: !sdkControls.sdk.available
                                                onClicked: Qt.openUrlExternally("https://www.viture.com/developer")
                                            }
                                            Action {
                                                text: sdkControls.sdk.busy ? "Connecting / working…" : sdkControls.sdk.communication ? "Reconnect glasses" : "Connect glasses"
                                                enabled: root.loaded && !root.busy && !sdkControls.sdk.busy && !root.glasses.recovering
                                                onClicked: root.send("sdk_connect")
                                            }
                                            Action {
                                                text: "Retry display mode"
                                                enabled: root.loaded && !root.busy && !!sdkControls.sdk.communication && !sdkControls.sdk.busy && !root.glasses.recovering
                                                onClicked: root.send("sdk_restore")
                                            }
                                            Action {
                                                text: "Disconnect SDK"
                                                enabled: root.loaded && !root.busy && (!!sdkControls.sdk.communication || !!sdkControls.sdk.busy)
                                                onClicked: root.send("sdk_disconnect")
                                            }
                                        }
                                    }
                                    Label {
                                        visible: !!((root.glasses.sdk || {}).trackingError || (root.glasses.sdk || {}).displayError)
                                        text: [(root.glasses.sdk || {}).trackingError, (root.glasses.sdk || {}).displayError].filter(function (x) {
                                            return !!x;
                                        }).join(" · ")
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        color: Color.urgent
                                    }
                                    Label {
                                        visible: !!root.glasses.recoveryMessage
                                        text: root.glasses.recoveryMessage || ""
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        color: Qt.alpha(Color.foreground, .68)
                                    }
                                    Label {
                                        visible: root.loaded && !root.glasses.canReset && !root.glasses.recovering
                                        text: "Automatic recovery unavailable. Reconnect the USB-C cable."
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        color: Qt.alpha(Color.foreground, .68)
                                    }
                                }
                            }
                            Card {
                                Heading { text: "Rendering & capture" }
                                Hint {
                                    text:root.performance.fps !== undefined ? root.performance.fps.toFixed(1)+" presented fps · CPU work p95 "+root.performance.workP95.toFixed(2)+" ms · frame p95 "+root.performance.frameP95.toFixed(2)+" ms"
                                        +(root.performance.gpuSceneP95 !== undefined ? " · GPU scene p95 "+root.performance.gpuSceneP95.toFixed(2)+" ms · GPU capture p95 "+root.performance.gpuCaptureP95.toFixed(2)+" ms" : "")
                                        +(root.performance.refreshHz ? " · missed vblanks "+root.performance.missedVblanksWindow+" / "+root.performance.missedVblanks : "")
                                        : "Start XR to measure performance."
                                }
                                Repeater {
                                    model:root.performance.captures || []
                                    Hint {
                                        required property var modelData
                                        text:modelData.output+" · "+(modelData.visible ? modelData.width+" × "+modelData.height : "Capture paused")+" · "+modelData.transport+" · source "+modelData.nativeWidth+" × "+modelData.nativeHeight+(modelData.importMs !== undefined ? " · import "+Number(modelData.importMs).toFixed(1)+" ms" : "")
                                    }
                                }
                                Hint { text:"Adaptive capture resolution. Off-screen capture paused. Presentation rate follows the display mode." }
                            }
                            Card {
                                Heading {
                                    text: "Preview & cleanup"
                                }
                                Hint {
                                    text: "Desktop preview and monitor cleanup."
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    enabled: root.loaded
                                    Action {
                                        text: "Windowed preview"
                                        enabled: root.activeCount > 0 && !root.dirty && !root.viewing && !root.busy
                                        onClicked: root.send("start")
                                    }
                                    Action {
                                        text: "Fullscreen mono"
                                        enabled: !!root.glasses.displays && root.glasses.displays.length === 1 && !root.viewing && !root.busy
                                        onClicked: root.send("present")
                                    }
                                    Action {
                                        text: "Stop & remove monitors"
                                        enabled: root.activeCount > 0
                                        onClicked: root.send("stop")
                                    }
                                }
                                Hint {
                                    text: "Apply a layout before previewing. Removing monitors moves their windows to another display."
                                }
                            }
                            Card {
                                RowLayout {
                                    Layout.fillWidth: true
                                    Heading {
                                        text: "Session activity"
                                    }
                                    Action {
                                        text: "Clear"
                                        enabled: root.history.length > 0
                                        onClicked: root.history = []
                                    }
                                }
                                Hint {
                                    text: "Newest first. Select text and Ctrl+C to copy."
                                }
                                QQC.ScrollView {
                                    id: activityScroll
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 220
                                    contentWidth: availableWidth
                                    clip: true
                                    QQC.ScrollBar.horizontal.policy: QQC.ScrollBar.AlwaysOff
                                    QQC.TextArea {
                                        width: activityScroll.availableWidth
                                        readOnly: true
                                        selectByMouse: true
                                        wrapMode: TextEdit.Wrap
                                        color: Color.foreground
                                        selectionColor: Color.accent
                                        selectedTextColor: Color.background
                                        font.family: Style.font.family
                                        font.pixelSize: Style.font.bodySmall
                                        text: root.history.length ? root.history.map(function (e) {
                                            return e.time + "  " + (e.failed ? "ERROR  " : "") + e.message;
                                        }).join("\n\n") : "No activity yet."
                                        background: Rectangle {
                                            color: Qt.alpha(Color.foreground, .025)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: Qt.alpha(Color.foreground, .15)
                }
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label {
                            text: root.backendSlow ? "Backend is still working. Stop remains available." : root.busy ? "Working…" : root.activeCount + " active monitors"
                            color: root.busy ? Color.accent : Color.foreground
                        }
                        Label {
                            text: (root.totalPixels / 1e6).toFixed(1) + " MP · " + root.fps + " fps"
                            color: Qt.alpha(Color.foreground, .68)
                            font.pixelSize: Style.font.bodySmall
                        }
                    }
                    Action {
                        visible: root.activeTab === 1
                        text: "Save layout"
                        tooltipText: "Save for the next session without applying"
                        enabled: root.loaded && !root.busy
                        onClicked: root.send("save")
                    }
                    Action {
                        visible: root.activeTab === 1
                        text: root.directOutput ? "Apply to XR" : "Apply layout"
                        selected: true
                        enabled: root.loaded && !root.busy
                        onClicked: root.send(root.directOutput ? "present_direct" : "apply")
                    }
                }
            }
        }
    }
}
