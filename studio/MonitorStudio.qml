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
import "json_equal.js" as JsonEqual

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
    property int dragIndex: -1
    property var dragSnap: null
    property var controlDraft: ({fingers:3,fit_all:"CTRL + Up",fit_target:"CTRL + Down",recenter:"",zoom_in:"",zoom_out:""})
    property bool controlsDirty: false
    function setControl(key,value) {
        var copy=Object.assign({},controlDraft);copy[key]=value;controlDraft=copy;controlsDirty=true;
    }
    property var environmentSettings: ({id:"",brightness:25,rotation:0,animated:true})
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
    property var captureRows: []
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
    readonly property bool canStart: loaded && !busy && !glasses.recovering && !!glasses.runtimeInstalled && !!sdk.available && !!glasses.helperAvailable && (directOutput || (!!glasses.displays && glasses.displays.length === 1))
    function selectTab(index) {
        activeTab = Math.max(0, Math.min(3, Number(index)));
        var surface = panelBody.item;
        if (surface)
            surface.scrollView.contentItem.contentY = 0;
        Qt.callLater(fit);
    }
    function notify(message, failed) {
        feedback = message;
        feedbackError = !!failed;
        if (message && !failed) feedbackTimeout.restart();
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
            setupName: setupName,
            fps: fps,
            spacing: spacing,
            curvature: curvature,
            selected: selected,
            canImportEnvironment: canImportEnvironment,
            workspaceDegrees: workspaceDegrees, workspaceFollow: workspaceFollow,
            laptopOffEnabled: laptopOffEnabled,
            laptopDisplay: laptopDisplay,
            spectatorEnabled: spectatorEnabled,
            performance: performance,
            graphicsLimits: graphicsLimits,
            scrollHeight: panelBody.item ? panelBody.item.scrollView.contentHeight : 0,
            viewportHeight: panelBody.item ? panelBody.item.scrollView.availableHeight : 0,
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
    function openSetupTerminal(command) {
        Quickshell.execDetached(["omarchy", "launch", "terminal"].concat(command));
    }
    function installRuntime() {
        var command = glasses.runtimeInstalled && sdk.available && sdk.packaged && sdk.licenseAccepted === false && glasses.helperAvailable
            ? ["omarchy-xr-setup", "--controls", "--notifications"]
            : ["python3", localPath(Qt.resolvedUrl("install_runtime.py")), "--controls", "--notifications"];
        openSetupTerminal(command);
    }
    function runSetupAction(action) {
        openSetupTerminal(["python3", localPath(Qt.resolvedUrl("setup_actions.py")), action]);
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
    }
    function edit(key, value) {
        if (!monitors.length)
            return;
        if (key === "width" || key === "height") {
            resizeMonitor(key === "width" ? value : current.width, key === "height" ? value : current.height);
            return;
        }
        var stored = (key === "curvature" || key === "scale") ? value : Math.round(value);
        if (monitors[selected][key] === stored)
            return;
        var copy = JSON.parse(JSON.stringify(monitors));
        copy[selected][key] = stored;
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
    function previewMonitor(index, x, y) {
        dragIndex = index;
        dragSnap = MonitorSnap.preview(monitors, index, x, y, spacing, viewScale, dragSnap);
    }
    function finishMonitorDrag() {
        var index = dragIndex;
        var snap = dragSnap;
        if (index >= 0 && snap && monitors[index]) {
            var next = MonitorSnap.commit(monitors, index, snap);
            if (next !== monitors) {
                monitors = next;
                changed();
            }
        }
        dragIndex = -1;
        dragSnap = null;
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
        var view = panelBody.item ? panelBody.item.monitorMap : null;
        if (!monitors.length || !view || view.width < 1)
            return;
        var l = Infinity, t = Infinity, r = -Infinity, b = -Infinity;
        monitors.forEach(function (m) {
            l = Math.min(l, m.x);
            t = Math.min(t, m.y);
            r = Math.max(r, m.x + m.width);
            b = Math.max(b, m.y + m.height);
        });
        viewScale = Math.min((view.width - 80) / (r - l), (view.height - 80) / (b - t));
        viewScale = Math.max(.001, viewScale);
        offsetX = (view.width - (r - l) * viewScale) / 2 - l * viewScale;
        offsetY = (view.height - (b - t) * viewScale) / 2 - t * viewScale;
    }
    function hit(x, y) {
        for (var i = monitors.length - 1; i >= 0; i--) {
            var m = monitors[i];
            if (x >= offsetX + m.x * viewScale && x <= offsetX + (m.x + m.width) * viewScale && y >= offsetY + m.y * viewScale && y <= offsetY + (m.y + m.height) * viewScale)
                return i;
        }
        return -1;
    }
    function repaintGrid() {
        var surface = panelBody.item;
        if (surface)
            surface.gridCanvas.requestPaint();
    }
    Connections {
        target: Color
        function onBackgroundChanged() {
            root.repaintGrid();
        }
        function onForegroundChanged() {
            root.repaintGrid();
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
                    if (response.performance) {
                        var nextCaptures = response.performance.captures || [];
                        if (!JsonEqual.same(root.captureRows, nextCaptures))
                            root.captureRows = nextCaptures;
                        if (!JsonEqual.same(root.performance, response.performance))
                            root.performance = response.performance;
                    }
                    root.viewerExit = response.viewerExit || "";
                    root.controlsHint = response.controlsHint || "";
                    if (root.performance.geometryDistance > 0) root.geometryDistance=root.performance.geometryDistance;
                    root.spectatorEnabled = !!response.spectatorEnabled;
                    root.laptopOffEnabled = !!response.laptopOffEnabled;
                    if (response.laptopDisplay && !JsonEqual.same(root.laptopDisplay, response.laptopDisplay))
                        root.laptopDisplay = response.laptopDisplay;
                    if (replyAction !== "status" || !response.ok)
                        root.error = !response.ok;
                    var oldRecovery = root.glasses.recoveryMessage || "";
                    var oldSDK = (root.glasses.sdk || {}).message || "";
                    if (response.glasses && !JsonEqual.same(root.glasses, response.glasses))
                        root.glasses = response.glasses;
                    if (response.restorationError)
                        root.notify(response.restorationError, true);
                    if (!response.ok)
                        root.notify(response.message || "Action failed", true);
                    else if (replyAction === "check") {
                        root.notify("Connection checked at " + Qt.formatTime(new Date(), "hh:mm:ss") + ": " + (root.glasses.usb ? "glasses detected" : "glasses not detected") + " · " + (root.glasses.dedicatedDisplay ? "stereo active" : root.glasses.detectionError || (root.glasses.displays.length ? "video connected" : "glasses video not detected")));
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
                    root.status = "Studio could not read the latest update. Close and reopen it to try again.";
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
            root.status = "XR Monitor Studio stopped unexpectedly. Close and reopen it to try again.";
            root.notify(root.status, true);
        }
    }
    Timer {
        id: feedbackTimeout
        interval: 6000
        onTriggered: if (!root.busy && !root.feedbackError) root.feedback = "";
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
    component HelpTip: PassiveToolTip {
        id: tip
        boundaryItem: window.contentItem
        readonly property real cap: Math.min(420, Math.max(1, window.width - 40))
        width: Math.min(cap, Math.ceil(tooltipMetrics.width + 24))
        implicitWidth: width
        TextMetrics { id: tooltipMetrics; text: tip.text; font.family: Style.font.family; font.pixelSize: Style.font.bodySmall }
        padding: 0
        background: Ui.BorderSurface {
            color: Color.tooltip.background
            borderSpec: Border.localOrSurfaceSpec("tooltip", "border", Color.tooltip.border, Color.tooltip.border, Style.normalBorderWidth)
            radius: Style.cornerRadius
        }
        contentItem: Text {
            text: tip.text
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            width: tip.width
            color: Color.tooltip.text
            font.family: Style.font.family
            font.pixelSize: Style.font.bodySmall
            padding: 12
        }
    }
    component BoundDropdown: Ui.Dropdown {
        id: picker
        property string sourceValue: ""
        onSourceValueChanged: value = sourceValue
        Component.onCompleted: value = sourceValue
    }
    component Label: Text {
        id: label
        property string helpText: ""
        color: Color.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.body
        textFormat: Text.PlainText
        Accessible.description: helpText
        HoverHandler { id: labelHover }
        HelpTip {
            target: label
            active: labelHover.hovered
            text: label.helpText
        }
    }
    component Action: Ui.Button {
        id: action
        property string helpText: ""
        HelpTip {
            target: action
            active: action.hot
            text: action.helpText
        }
        focusable: true
        bordered: true
        opacity: enabled ? 1 : .4
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.description: helpText
        Accessible.onPressAction: if (enabled) clicked()
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
        implicitHeight: body.implicitHeight + 32
        color: Qt.alpha(Color.foreground, .025)
        border.color: Qt.alpha(Color.foreground, .12)
        radius: Style.cornerRadius
        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
        }
    }
    component Disclosure: ColumnLayout {
        id: disclosure
        property string title: ""
        property string helpText: ""
        property bool expanded: false
        default property alias content: disclosureBody.data
        Layout.fillWidth: true
        spacing: 12
        onExpandedChanged: if (expanded) revealTimer.restart()
        Timer {
            id: revealTimer
            interval: 50
            onTriggered: disclosure.reveal()
        }
        function reveal() {
            var view = panelBody.item.scrollView;
            var position = disclosure.mapToItem(view.contentItem, 0, 0);
            var overflow = position.y + disclosure.height - view.availableHeight;
            if (overflow > 0)
                view.contentItem.contentY = Math.min(view.contentHeight - view.availableHeight,
                    view.contentItem.contentY + overflow);
        }
        Action {
            Layout.fillWidth: true
            leftAlign: true
            text: (disclosure.expanded ? "▾  " : "▸  ") + disclosure.title
            helpText: disclosure.helpText
            Accessible.name: disclosure.title
            Accessible.description: (disclosure.expanded ? "Expanded. " : "Collapsed. ") + disclosure.helpText
            onClicked: disclosure.expanded = !disclosure.expanded
        }
        QQC.ScrollView {
            id: disclosureScroll
            visible: disclosure.expanded
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(300, disclosureBody.implicitHeight)
            clip: true
            contentWidth: availableWidth
            contentHeight: disclosureBody.implicitHeight
            rightPadding: contentHeight > availableHeight + 1 ? 12 : 0
            QQC.ScrollBar.vertical: ScrollThumb {}
            QQC.ScrollBar.horizontal.policy: QQC.ScrollBar.AlwaysOff
            ColumnLayout {
                id: disclosureBody
                width: disclosureScroll.availableWidth
                spacing: 12
            }
        }
    }
    component ScrollThumb: QQC.ScrollBar {
        orientation: Qt.Vertical
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        policy: QQC.ScrollBar.AsNeeded
        active: true
        contentItem: Rectangle {
            implicitWidth: 5
            implicitHeight: 20
            radius: Style.cornerRadius
            color: Qt.alpha(Color.foreground, parent.pressed ? .65 : .3)
        }
    }
    QtObject {
        id: sliderPalette
        property color foreground: Color.foreground
        property color background: Color.background
    }

    FloatingWindow {
        id: window
        title: "XR Monitor Studio"
        visible: false
        color: Color.background
        implicitWidth: 1100
        implicitHeight: 760
        minimumSize: Qt.size(780, 600)
        onVisibleChanged: {
            if (!visible) {
                root.dragIndex = -1;
                root.dragSnap = null;
                if (!root.closingFromHost && root.shell)
                    root.shell.hide("afruth.omarchy-xr");
            }
        }
        // Hiding unloads the editor. The backend Process stays on the root, so XR keeps running.
        Loader {
            id: panelBody
            anchors.fill: parent
            active: window.visible
            sourceComponent: studioSurface
            onLoaded: Qt.callLater(root.fit)
        }
    }
    Component {
        id: studioSurface
        FocusScope {
            id: frame
            property alias scrollView: scroll
            property alias monitorMap: layoutView
            property alias gridCanvas: grid
            anchors.fill: parent
            focus: true
            FileDialog {
                id: environmentFile
                title: "Import spherical panorama"
                nameFilters: ["Panoramas (*.jpg *.jpeg *.png *.bmp)"]
                onAccepted: {
                    root.imagePath = root.localPath(selectedFile);
                    root.send("import_environment");
                }
            }
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
                anchors.margins: 20
                spacing: 14
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 4
                        Label {
                            text: "XR Monitor Studio"
                            font.pixelSize: Style.font.heading
                            font.bold: true
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
                        helpText: "Park on the top bar; XR stays running"
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
                        model: ["Controls", "Monitors", "Environment", "Utilities"]
                        Action {
                            required property int index
                            required property string modelData
                            text: modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            selected: root.activeTab === index
                            verticalPadding: 10
                            helpText: "Ctrl+" + (index + 1)
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
                    rightPadding: contentHeight > height + 1 ? 12 : 0
                    QQC.ScrollBar.vertical: ScrollThumb {
                        visible: scroll.contentHeight > scroll.availableHeight + 1
                    }
                    QQC.ScrollBar.horizontal.policy: QQC.ScrollBar.AlwaysOff
                    ColumnLayout {
                        id: tabContent
                        width: scroll.availableWidth
                        height: implicitHeight
                        spacing: 12
                        ColumnLayout {
                            visible: root.activeTab === 0
                            Layout.fillWidth: true
                            spacing: 12
                            Card {
                                visible: root.loaded && (!root.glasses.runtimeInstalled || !root.sdk.available || !root.glasses.helperAvailable)
                                color: Qt.alpha(Color.accent, .08)
                                border.color: Qt.alpha(Color.accent, .55)
                                Heading {
                                    text: !root.glasses.runtimeInstalled || !root.sdk.available ? "XR runtime required" : "Stereo setup incomplete"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: !root.glasses.runtimeInstalled || !root.sdk.available
                                        ? "Install the XR software before starting stereo. The installer verifies the download and includes everything needed to use the glasses."
                                        : "One system component needed for stereo is missing. Install it once, then try again."
                                }
                                Action {
                                    text: !root.glasses.runtimeInstalled || !root.sdk.available || root.sdk.packaged ? "Install XR runtime" : "Install stereo helper"
                                    selected: true
                                    helpText: !root.glasses.runtimeInstalled || !root.sdk.available || root.sdk.packaged
                                        ? "Open a terminal, verify the download, and ask before installing it"
                                        : "Open a terminal and ask for administrator approval to finish stereo setup"
                                    onClicked: {
                                        if (!root.glasses.runtimeInstalled || !root.sdk.available || root.sdk.packaged)
                                            root.installRuntime();
                                        else
                                            root.runSetupAction("helper");
                                    }
                                }
                            }
                            Card {
                                RowLayout {
                                    Layout.fillWidth: true
                                    Heading {
                                        Layout.fillWidth: true
                                        text: root.directOutput ? "Stereo active" : root.viewing ? "Preview active" : "XR session"
                                    }
                                    Label {
                                        text: root.sdk.tracking ? "Tracking active" : root.glasses.usb ? "Glasses connected" : "Glasses disconnected"
                                        color: Qt.alpha(Color.foreground, .68)
                                        helpText: "Head tracking is " + (root.sdk.tracking ? "active" : root.sdk.communication ? "starting" : "not connected")
                                    }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Action {
                                        visible: !root.directOutput || root.dirty || root.pendingAction === "present_direct"
                                        text: root.busy && root.pendingAction === "present_direct" ? "Starting…" : root.directOutput ? "Apply monitor changes" : "Start stereo"
                                        selected: true
                                        helpText: "Show your virtual monitors in the glasses"
                                        enabled: root.canStart && (!root.directOutput || root.dirty)
                                        onClicked: root.send("present_direct")
                                    }
                                    Action {
                                        visible: root.viewing || root.activeCount > 0
                                        text: root.directOutput ? "Stop stereo" : root.viewing ? "Close preview" : "Restore desktop"
                                        helpText: "Move your XR windows back to your computer display"
                                        enabled: (root.viewing || root.activeCount > 0) && !root.busy
                                        onClicked: root.send("stop_viewer")
                                    }
                                    Action {
                                        text: "Edit monitors"
                                        helpText: "Configure the size and arrangement of your virtual monitors"
                                        onClicked: root.selectTab(1)
                                    }
                                }
                                Hint {
                                    visible: !root.directOutput && !root.canStart && !root.busy
                                    text: !root.loaded ? "Loading workspace…" : !root.glasses.runtimeInstalled || !root.sdk.available ? "XR runtime required — use Install XR runtime above." : !root.glasses.helperAvailable ? "Stereo helper required — use Install stereo helper above." : !root.glasses.usb ? "Connect your glasses to start." : "Glasses video unavailable — open Utilities."
                                }
                            }
                            Card {
                                Heading {
                                    text: "View controls"
                                    helpText: root.viewing ? "Adjust your view without changing the monitor layout" : "Start stereo or a preview to use these controls"
                                }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width >= 5 * (Style.font.body * 8 + 26) ? 5 : 3
                                    columnSpacing: 10
                                    rowSpacing: 10
                                    enabled: root.viewing && !root.busy
                                    Action {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        text: "Recenter"
                                        helpText: "Set the direction you are looking as forward"
                                        onClicked: root.send("recenter")
                                    }
                                    Action {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        text: "Fit workspace"
                                        helpText: "Bring the whole workspace into view"
                                        onClicked: root.send("fit")
                                    }
                                    Action {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        text: "Fit monitor"
                                        helpText: "Fit the monitor selected by your head direction by height" + (root.controlDraft.fit_target ? " · " + root.controlDraft.fit_target : "")
                                        onClicked: root.send("fit_target")
                                    }
                                    Action {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        text: "Zoom out"
                                        helpText: "Move the workspace farther away"
                                        onClicked: root.send("zoom_out")
                                    }
                                    Action {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        text: "Zoom in"
                                        helpText: "Bring the workspace closer"
                                        onClicked: root.send("zoom_in")
                                    }
                                }
                            }
                            Disclosure {
                                title: root.controlsDirty ? "Shortcuts & gestures · Unsaved" : "Shortcuts & gestures"
                                helpText: "Apply shortcuts without restarting XR. Leave a shortcut blank to disable it."
                                Label {
                                    text: "Zoom gesture"
                                    helpText: "Flick up to fit the selected monitor; flick down to fit the workspace. Hold a swipe to zoom. Double tap with three fingers to recenter; swipe with four fingers to pan the selected monitor."
                                }
                                BoundDropdown {
                                    label: "Swipe fingers"
                                    options: ["3", "5"]
                                    sourceValue: String(root.controlDraft.fingers)
                                    onChanged: function(picked) {root.setControl("fingers",Number(picked));}
                                }
                                Repeater {
                                    model: [{key:"recenter",title:"Recenter camera"},{key:"fit_all",title:"Fit workspace"},{key:"fit_target",title:"Fit selected monitor"},{key:"zoom_in",title:"Zoom in"},{key:"zoom_out",title:"Zoom out"}]
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Label {
                                            text: modelData.title
                                            Layout.preferredWidth: 180
                                            helpText: "Use a modifier combination such as CTRL + ALT + R. Leave blank to disable this shortcut."
                                        }
                                        Ui.TextField {
                                            Layout.fillWidth: true
                                            text: root.controlDraft[modelData.key] || ""
                                            placeholderText: "No shortcut"
                                            Accessible.name: modelData.title + " hotkey"
                                            Accessible.description: "Use a modifier combination, for example CTRL + ALT + R. Leave blank to disable."
                                            onTextEdited: root.setControl(modelData.key,text)
                                        }
                                    }
                                }
                                RowLayout {
                                    Action {
                                        text: root.pendingAction === "save_controls" ? "Saving…" : "Save shortcuts"
                                        helpText: "Apply shortcuts and gestures to the current XR session"
                                        enabled: root.controlsDirty && !root.busy
                                        onClicked: root.send("save_controls")
                                    }
                                    Action {
                                        text: "Reset to defaults"
                                        enabled: !root.busy
                                        onClicked: {root.controlDraft={fingers:3,fit_all:"CTRL + Up",fit_target:"CTRL + Down",recenter:"",zoom_in:"",zoom_out:""};root.controlsDirty=true;}
                                    }
                                }
                            }
                            Disclosure {
                                title: "Laptop display"
                                helpText: "Choose whether your laptop display stays on during stereo"
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: "Turn off during stereo"
                                        Layout.fillWidth: true
                                        helpText: "The laptop display is restored when stereo stops or your glasses disconnect"
                                    }
                                    Ui.ToggleSwitch {
                                        checked: root.laptopOffEnabled
                                        busy: root.busy
                                        activeFocusOnTab: true
                                        Accessible.role: Accessible.CheckBox
                                        Accessible.name: "Turn off laptop display during stereo"
                                        Accessible.checked: checked
                                        enabled: root.loaded && !root.busy && (root.laptopDisplay.available || root.laptopOffEnabled)
                                        Keys.onSpacePressed: if (enabled) toggled()
                                        Accessible.onToggleAction: if (enabled) toggled()
                                        onToggled: root.send("set_laptop_off", !root.laptopOffEnabled)
                                        Rectangle {
                                            anchors.fill: parent
                                            anchors.margins: -3
                                            color: "transparent"
                                            border.width: parent.activeFocus ? 1 : 0
                                            border.color: Color.accent
                                            radius: Style.cornerRadius
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.laptopDisplay.off ? "Display off" : root.laptopDisplay.available ? "Display on" : "No built-in display"
                                    }
                                    Action {
                                        text: "Restore display"
                                        helpText: "Turn your laptop display back on now"
                                        enabled: root.loaded && !root.busy && (root.laptopDisplay.off || !!root.laptopDisplay.error)
                                        onClicked: root.send("restore_laptop")
                                    }
                                }
                            }
                            Hint {
                                visible: !!root.laptopDisplay.error
                                text: root.laptopDisplay.error || ""
                                color: Color.urgent
                            }
                        }
                        ColumnLayout {
                            visible: root.activeTab === 1
                            Layout.fillWidth: true
                            spacing: 12
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Label {
                                    text: "Setup"
                                    helpText: "Choosing a setup applies it immediately. Save your current layout as a setup to reuse it."
                                }
                                BoundDropdown {
                                    id: setupPicker
                                    Layout.fillWidth: true
                                    label: "Apply setup"
                                    showLabel: false
                                    sourceValue: root.pendingSetupId !== "" ? root.pendingSetupId : root.setupId
                                    options: [{value:"",label:"Choose a setup…"}].concat(root.builtInSetups.filter(root.supportsSetup).map(function(s) {
                                        return {value:s.id,label:s.name};
                                    }), root.savedSetups.map(function(s) { return {value:s.id,label:s.name}; }))
                                    enabled: root.loaded && !root.busy
                                    Accessible.name: "Apply monitor setup"
                                    onChanged: function(picked) {
                                        if (picked) root.chooseSetup(picked);
                                        else setupPicker.value = sourceValue;
                                    }
                                }
                                Action {
                                    text: "Add monitor"
                                    enabled: root.loaded && !root.busy && root.monitors.length < 16
                                    helpText: "Add a monitor to the draft layout"
                                    onClicked: root.setCount(root.monitors.length + 1)
                                }
                                Action {
                                    text: "Arrange…"
                                    enabled: root.loaded && !root.busy
                                    helpText: "Arrange in a row or grid, or fit the layout map"
                                    onClicked: arrangeMenu.open()
                                    QQC.Popup {
                                        id: arrangeMenu
                                        x: parent.width - width
                                        y: parent.height + 6
                                        width: 220
                                        padding: 12
                                        background: Rectangle { color: Color.popups.background; border.color: Color.popups.border; radius: Style.cornerRadius }
                                        contentItem: ColumnLayout {
                                            spacing: 8
                                            Action { Layout.fillWidth:true; text:"Arrange in row"; onClicked:{root.arrange(false);arrangeMenu.close();} }
                                            Action { Layout.fillWidth:true; text:"Arrange in grid"; onClicked:{root.arrange(true);arrangeMenu.close();} }
                                            Action { Layout.fillWidth:true; text:"Fit layout map"; onClicked:{root.fit();arrangeMenu.close();} }
                                        }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(monitorInspector.implicitHeight, Math.min(340, scroll.availableHeight - 96))
                                spacing: 16
                                Rectangle {
                                    id: layoutView
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    color: Color.background
                                    border.color: Qt.alpha(Color.foreground, .18)
                                    radius: Style.cornerRadius
                                    clip: true
                                    onWidthChanged: root.fit()
                                    onHeightChanged: root.fit()
                                    Canvas {
                                        id: grid
                                        anchors.fill: parent
                                        onPaint: {
                                            var c = getContext("2d");
                                            c.reset();
                                            c.fillStyle = Color.background;
                                            c.fillRect(0, 0, width, height);
                                            c.strokeStyle = Qt.alpha(Color.foreground, .1);
                                            c.lineWidth = 1;
                                            c.beginPath();
                                            for (var gx = 0; gx < width; gx += 24) {
                                                c.moveTo(gx, 0);
                                                c.lineTo(gx, height);
                                            }
                                            for (var gy = 0; gy < height; gy += 24) {
                                                c.moveTo(0, gy);
                                                c.lineTo(width, gy);
                                            }
                                            c.stroke();
                                        }
                                    }
                                    Repeater {
                                        model: root.monitors
                                        delegate: Rectangle {
                                            id: monitorTile
                                            required property var modelData
                                            required property int index
                                            readonly property bool snapped: index === root.dragIndex && root.dragSnap && (root.dragSnap.snapX || root.dragSnap.snapY)
                                            readonly property real poseX: index === root.dragIndex && root.dragSnap ? root.dragSnap.x : modelData.x
                                            readonly property real poseY: index === root.dragIndex && root.dragSnap ? root.dragSnap.y : modelData.y
                                            x: root.offsetX + poseX * root.viewScale
                                            y: root.offsetY + poseY * root.viewScale
                                            width: modelData.width * root.viewScale
                                            height: modelData.height * root.viewScale
                                            color: Qt.alpha(Color.accent, index === root.selected ? .23 : .08)
                                            border.color: index === root.selected ? Color.accent : Qt.alpha(Color.foreground, .68)
                                            border.width: snapped ? 5 : index === root.selected ? 3 : 1
                                            Label {
                                                x: 10
                                                y: 4
                                                text: String(monitorTile.index + 1)
                                                font.bold: true
                                                font.pixelSize: 16
                                                font.family: Style.font.family
                                                color: Color.foreground
                                            }
                                            Label {
                                                x: 10
                                                y: 26
                                                visible: monitorTile.width > 115 && monitorTile.height > 60
                                                text: monitorTile.modelData.width + " × " + monitorTile.modelData.height
                                                font.pixelSize: 12
                                                font.family: Style.font.family
                                                color: Color.foreground
                                            }
                                        }
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
                                            root.dragIndex = -1;
                                            root.dragSnap = null;
                                            startX = mouse.x;
                                            startY = mouse.y;
                                            dragging = mouse.button === Qt.LeftButton ? root.hit(mouse.x, mouse.y) : -1;
                                            if (dragging >= 0) {
                                                root.selected = dragging;
                                                originalX = root.monitors[dragging].x;
                                                originalY = root.monitors[dragging].y;
                                            } else {
                                                originalX = root.offsetX;
                                                originalY = root.offsetY;
                                            }
                                        }
                                        onPositionChanged: function (mouse) {
                                            if (!pressed)
                                                return;
                                            if (dragging >= 0) {
                                                root.previewMonitor(dragging,
                                                    originalX + (mouse.x - startX) / root.viewScale,
                                                    originalY + (mouse.y - startY) / root.viewScale);
                                            } else {
                                                root.offsetX = originalX + mouse.x - startX;
                                                root.offsetY = originalY + mouse.y - startY;
                                            }
                                        }
                                        onReleased: { root.finishMonitorDrag(); dragging = -1; }
                                        onCanceled: { root.finishMonitorDrag(); dragging = -1; }
                                        onWheel: function (wheel) {
                                            if (!(wheel.modifiers & Qt.ControlModifier)) { wheel.accepted=false; return; }
                                            var old = root.viewScale;
                                            root.viewScale = Math.max(.001, Math.min(1, old * (wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15)));
                                            root.offsetX = wheel.x - (wheel.x - root.offsetX) * root.viewScale / old;
                                            root.offsetY = wheel.y - (wheel.y - root.offsetY) * root.viewScale / old;
                                        }
                                    }
                                }
                                ColumnLayout {
                                    id: monitorInspector
                                    Layout.minimumWidth: 260
                                    Layout.maximumWidth: 260
                                    Layout.preferredWidth: 260
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 12
                                    enabled: root.loaded && !root.busy
                                    BoundDropdown {
                                        Layout.fillWidth: true
                                        label: "Selected monitor"
                                        showLabel: false
                                        sourceValue: String(root.selected)
                                        options: root.monitors.map(function(m,i) {return {value:String(i),label:"Monitor " + (i+1)};})
                                        Accessible.name: "Selected monitor"
                                        onChanged: function(picked) {root.selected=Number(picked);}
                                    }
                                    BoundDropdown {
                                        id: resolutionPicker
                                        Layout.fillWidth: true
                                        label: "Resolution"
                                        sourceValue: MonitorPresets.match(root.current)
                                        options: MonitorPresets.options(root.graphicsLimits).map(function(p) {
                                            return p.value === "custom" ? {value:"custom", label:"Custom · " + root.current.width + " × " + root.current.height} : p;
                                        })
                                        onChanged: function(picked) {
                                            var preset = MonitorPresets.find(picked);
                                            if (preset) root.resizeMonitor(preset.width,preset.height);
                                            else monitorSettings.open();
                                            resolutionPicker.value = sourceValue;
                                        }
                                    }
                                    BoundDropdown {
                                        Layout.fillWidth: true
                                        label: "Display scale"
                                        sourceValue: String(root.current.scale || 1)
                                        options: ["1", "1.25", "1.6", "2", "3", "4"].map(function(v) {return {value:v,label:Math.round(Number(v)*100)+"%"};})
                                        onChanged: function(picked) { root.edit("scale",Number(picked)); }
                                    }
                                    AngleField {
                                        visible: !root.workspaceFollow
                                        label: "Surface bend (°)"
                                        amount: root.current.curvature || 0
                                        maximumDegrees: root.angleLimits.surfaces[root.selected] || 0
                                        fieldWidth: 260
                                        onAmountEdited: function(value) {root.edit("curvature",value);}
                                    }
                                    Action {
                                        visible: root.workspaceFollow
                                        Layout.fillWidth: true
                                        text: "Workspace bend…"
                                        helpText: "Change workspace curvature, or turn off matching to bend this monitor independently."
                                        onClicked: workspaceSettings.open()
                                    }
                                    Action {
                                        Layout.fillWidth: true
                                        text: "Monitor settings…"
                                        helpText: "Custom size, position, brightness and orientation"
                                        onClicked: monitorSettings.open()
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Action {
                                    text: "Workspace settings…"
                                    helpText: "Workspace curvature, monitor spacing, capture rate and desktop text size"
                                    enabled: root.loaded && !root.busy
                                    onClicked: workspaceSettings.open()
                                }
                                Action {
                                    text: "Save setup…"
                                    helpText: "Save this draft as a named setup"
                                    enabled: root.loaded && !root.busy
                                    onClicked: saveSetupDialog.open()
                                }
                                Item { Layout.fillWidth: true }
                                Label {
                                    text: root.dirty ? "Unapplied changes" : root.monitors.length + " monitors"
                                    color: root.dirty ? Color.accent : Qt.alpha(Color.foreground,.68)
                                    helpText: "Drag monitors to arrange. Drag empty space to pan. Ctrl+scroll zooms."
                                }
                            }
                            Hint {
                                visible: !!root.dragSnap && root.dragSnap.blocked
                                text: "Move blocked: insufficient spacing."
                                color: Color.urgent
                            }
                            QQC.Popup {
                                id: monitorSettings
                                parent: frame
                                x: (frame.width-width)/2
                                y: (frame.height-height)/2
                                width: Math.min(540, frame.width-40)
                                modal: true
                                focus: true
                                padding: 20
                                background: Rectangle {color:Color.popups.background;border.color:Color.popups.border;radius:Style.cornerRadius}
                                contentItem: ColumnLayout {
                                    spacing: 14
                                    Heading {text:"Monitor " + (root.selected+1) + " settings"}
                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 2
                                        columnSpacing: 16
                                        rowSpacing: 12
                                        enabled: root.loaded && !root.busy
                                        Ui.NumberField {label:"Width (px)";from:320;to:root.graphicsLimits.maxWidth;stepSize:80;value:root.current.width;fieldWidth:(monitorSettings.availableWidth-16)/2;onModified:function(value){root.edit("width",value);}}
                                        Ui.NumberField {label:"Height (px)";from:200;to:root.graphicsLimits.maxHeight;stepSize:80;value:root.current.height;fieldWidth:(monitorSettings.availableWidth-16)/2;onModified:function(value){root.edit("height",value);}}
                                        Ui.NumberField {label:"X position (px)";from:-100000;to:100000;stepSize:20;value:root.current.x;fieldWidth:(monitorSettings.availableWidth-16)/2;onModified:function(value){root.edit("x",value);}}
                                        Ui.NumberField {label:"Y position (px)";from:-100000;to:100000;stepSize:20;value:root.current.y;fieldWidth:(monitorSettings.availableWidth-16)/2;onModified:function(value){root.edit("y",value);}}
                                    }
                                    Label {text:"Brightness · " + Math.round(monitorBrightness.liveValue) + "%";helpText:"Brightness changes apply with the layout."}
                                    Ui.PanelSlider {
                                        id: monitorBrightness
                                        bar: sliderPalette
                                        Layout.fillWidth:true
                                        minimum:1;maximum:100;step:1;integer:true
                                        trackColor: Qt.alpha(Color.foreground, .2)
                                        value:root.current.brightness === undefined ? 100 : root.current.brightness
                                        enabled:root.loaded && !root.busy
                                        activeFocusOnTab: true
                                        Accessible.role: Accessible.Slider
                                        Accessible.name: "Monitor brightness"
                                        Accessible.description: Math.round(liveValue) + "%"
                                        Keys.onLeftPressed: if (enabled) root.edit("brightness", Math.max(1, value - 1))
                                        Keys.onRightPressed: if (enabled) root.edit("brightness", Math.min(100, value + 1))
                                        onReleased:function(value){root.edit("brightness",value);}
                                        Rectangle {
                                            anchors.fill: parent
                                            anchors.margins: -3
                                            color: "transparent"
                                            border.width: parent.activeFocus ? 1 : 0
                                            border.color: Color.accent
                                            radius: Style.cornerRadius
                                        }
                                    }
                                    RowLayout {
                                        Action {
                                            text:"Rotate 90°"
                                            enabled:!root.busy && MonitorPresets.supported(root.current.height,root.current.width,root.graphicsLimits)
                                            helpText:"Swap monitor width and height"
                                            onClicked:root.resizeMonitor(root.current.height,root.current.width)
                                        }
                                        Action {
                                            text:"Remove monitor"
                                            enabled:!root.busy && root.monitors.length>1
                                            onClicked:{var c=root.monitors.slice();c.splice(root.selected,1);root.monitors=c;root.selected=Math.min(root.selected,c.length-1);root.changed();root.fit();monitorSettings.close();}
                                        }
                                    }
                                    Action {Layout.alignment:Qt.AlignRight;text:"Done";onClicked:monitorSettings.close()}
                                }
                            }
                            QQC.Popup {
                                id: workspaceSettings
                                parent: frame
                                x:(frame.width-width)/2
                                y:(frame.height-height)/2
                                width:Math.min(560,frame.width-40)
                                modal:true
                                focus:true
                                padding:20
                                background:Rectangle {color:Color.popups.background;border.color:Color.popups.border;radius:Style.cornerRadius}
                                contentItem:ColumnLayout {
                                    spacing:14
                                    Heading {
                                        text:"Workspace settings"
                                        helpText:"Apply the layout to use geometry and capture settings. Text size changes immediately on all desktops. 0° is flat.\nMaximum: " + root.graphicsLimits.maxWidth + " × " + root.graphicsLimits.maxHeight + " px per monitor. "
                                            + (root.totalPixels/1000000).toFixed(1) + " MP · " + (root.totalPixels*4/1048576).toFixed(0) + " MiB/frame."
                                            + (!root.graphicsLimits.detected ? "\nHardware limits are unverified." : !root.graphicsLimits.complete ? "\nGPU detection is partial." : "")
                                    }
                                    GridLayout {
                                        Layout.fillWidth:true
                                        columns:2
                                        columnSpacing:16
                                        rowSpacing:12
                                        enabled:root.loaded && !root.busy
                                        AngleField {
                                            label:"Workspace wrap (°)"
                                            amount:(root.workspaceDegrees>=0 ? root.workspaceDegrees : root.curvature*root.angleLimits.workspace/100)/3.6
                                            maximumDegrees:360
                                            fieldWidth:(workspaceSettings.availableWidth-16)/2
                                            onAmountEdited:function(value){root.workspaceDegrees=Math.round(value*3.6);root.changed();}
                                        }
                                        Ui.NumberField {label:"Spacing (px)";from:1;to:8192;value:root.spacing;fieldWidth:(workspaceSettings.availableWidth-16)/2;onModified:function(value){root.spacing=value;root.changed();}}
                                        Ui.NumberField {label:"Capture rate (fps)";from:1;to:120;value:root.fps;fieldWidth:(workspaceSettings.availableWidth-16)/2;onModified:function(value){root.fps=value;root.changed();}}
                                    }
                                    RowLayout {
                                        Layout.fillWidth:true
                                        Label {Layout.fillWidth:true;text:"Match monitor bend to workspace";helpText:"Use workspace curvature for every monitor. Turn off to edit each surface bend independently."}
                                        Ui.ToggleSwitch {
                                            checked:root.workspaceFollow
                                            enabled:root.loaded && !root.busy
                                            activeFocusOnTab:true
                                            Accessible.role:Accessible.CheckBox
                                            Accessible.name:"Match monitor bend to workspace"
                                            Accessible.checked:checked
                                            Keys.onSpacePressed:if(enabled)toggled()
                                            Accessible.onToggleAction:if(enabled)toggled()
                                            onToggled:{root.workspaceFollow=!root.workspaceFollow;root.changed();}
                                            Rectangle {
                                                anchors.fill: parent
                                                anchors.margins: -3
                                                color: "transparent"
                                                border.width: parent.activeFocus ? 1 : 0
                                                border.color: Color.accent
                                                radius: Style.cornerRadius
                                            }
                                        }
                                    }
                                    BoundDropdown {
                                        Layout.fillWidth:true
                                        label:"Text size · all desktops"
                                        sourceValue:String(Math.round(Style.font.baseSize))
                                        options:["9","10","11","12","14","16","20"]
                                        enabled:root.loaded && !root.busy
                                        onChanged:function(picked){root.send("set_text_size",Number(picked));}
                                    }
                                    Action {Layout.alignment:Qt.AlignRight;text:"Done";onClicked:workspaceSettings.close()}
                                }
                            }
                            QQC.Popup {
                                id:saveSetupDialog
                                parent:frame
                                x:(frame.width-width)/2
                                y:(frame.height-height)/2
                                width:Math.min(500,frame.width-40)
                                modal:true
                                focus:true
                                padding:20
                                background:Rectangle {color:Color.popups.background;border.color:Color.popups.border;radius:Style.cornerRadius}
                                contentItem:ColumnLayout {
                                    spacing:14
                                    Heading {text:"Save setup";helpText:"Stores the current draft without applying it."}
                                    Ui.TextField {
                                        Layout.fillWidth:true
                                        text:root.setupName
                                        placeholderText:"Setup name"
                                        Accessible.name:"Setup name"
                                        onTextEdited:root.setupName=text
                                    }
                                    Flow {
                                        Layout.fillWidth:true
                                        spacing:8
                                        Action {text:"Save as new";enabled:root.loaded && !root.busy && !!root.setupName.trim();onClicked:{root.send("save_setup");saveSetupDialog.close();}}
                                        Action {text:"Update selected";enabled:root.loaded && !root.busy && !!root.setupId && !root.setupId.startsWith("builtin:") && !!root.setupName.trim();onClicked:{root.send("save_setup",undefined,root.setupId,true);saveSetupDialog.close();}}
                                        Action {text:"Cancel";onClicked:saveSetupDialog.close()}
                                    }
                                }
                            }
                            QQC.Popup {
                                parent:frame
                                x:(frame.width-width)/2
                                y:(frame.height-height)/2
                                width:Math.min(500,frame.width-40)
                                visible:!!root.pendingSetupId
                                modal:true
                                focus:true
                                padding:20
                                onClosed:root.pendingSetupId=""
                                background:Rectangle {color:Color.popups.background;border.color:Color.popups.border;radius:Style.cornerRadius}
                                contentItem:ColumnLayout {
                                    spacing:14
                                    Heading {text:"Discard unapplied changes?"}
                                    RowLayout {
                                        Action {text:"Keep editing";onClicked:root.pendingSetupId=""}
                                        Action {text:"Discard and apply setup";enabled:!root.busy;onClicked:{var id=root.pendingSetupId;root.pendingSetupId="";root.send("use_setup",undefined,id);}}
                                    }
                                }
                            }
                        }
                        ColumnLayout {
                            visible: root.activeTab === 2
                            Layout.fillWidth: true
                            spacing: 12
                            Card {
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    Heading {
                                        text: "Environment"
                                        helpText: "Choose your background. Changes apply immediately."
                                    }
                                    Action {
                                        text: "Black background"
                                        selected: !root.environmentSettings.id
                                        helpText: "Turn off the environment and use a black background."
                                        enabled: root.loaded && !root.busy
                                        onClicked: root.setEnvironment("id", "")
                                    }
                                }
                                Flickable {
                                    id: environmentGallery
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Math.min(environmentGrid.implicitHeight, 210)
                                    contentWidth: width
                                    contentHeight: environmentGrid.implicitHeight
                                    clip: true
                                    boundsBehavior: Flickable.StopAtBounds
                                    flickableDirection: Flickable.VerticalFlick
                                    QQC.ScrollBar.vertical: QQC.ScrollBar {
                                        policy: QQC.ScrollBar.AsNeeded
                                        visible: environmentGallery.contentHeight > environmentGallery.height + 1
                                        contentItem: Rectangle {
                                            implicitWidth: 5
                                            radius: Style.cornerRadius
                                            color: Qt.alpha(Color.foreground, parent.pressed ? .65 : .3)
                                        }
                                    }
                                    Grid {
                                        id: environmentGrid
                                        width: environmentGallery.width - 12
                                        columns: Math.max(1, Math.min(4, Math.floor(width / 160)))
                                        spacing: 10
                                        Repeater {
                                            model: root.environmentItems
                                            delegate: Action {
                                                id: environmentTile
                                                required property var modelData
                                                readonly property string displayName: modelData.name.replace(/\s*\(\d+[×x]\)\s*$/, "")
                                                width: (environmentGrid.width - environmentGrid.spacing * (environmentGrid.columns - 1)) / environmentGrid.columns
                                                height: 100
                                                selected: root.environmentSettings.id === modelData.id
                                                enabled: root.loaded && !root.busy
                                                helpText: modelData.name + (selected ? " · Selected" : " · Use this background")
                                                Accessible.name: displayName
                                                Accessible.checkable: true
                                                Accessible.checked: selected
                                                Accessible.onPressAction: if (enabled) root.chooseEnvironment(modelData.id)
                                                onClicked: root.chooseEnvironment(modelData.id)
                                                onActiveFocusChanged: {
                                                    if (!activeFocus) return;
                                                    if (y < environmentGallery.contentY)
                                                        environmentGallery.contentY = y;
                                                    else if (y + height > environmentGallery.contentY + environmentGallery.height)
                                                        environmentGallery.contentY = y + height - environmentGallery.height;
                                                }
                                                Image {
                                                    x: 6; y: 6
                                                    width: parent.width - 12; height: 62
                                                    visible: environmentTile.modelData.id !== "builtin:tron"
                                                    source: environmentTile.modelData.thumbnail
                                                    asynchronous: true
                                                    fillMode: Image.PreserveAspectCrop
                                                    clip: true
                                                }
                                                Canvas {
                                                    id: tronPreview
                                                    x: 6; y: 6
                                                    width: parent.width - 12; height: 62
                                                    visible: environmentTile.modelData.id === "builtin:tron"
                                                    readonly property color gridColor: Color.accent
                                                    readonly property color backdrop: Color.background
                                                    onGridColorChanged: requestPaint()
                                                    onBackdropChanged: requestPaint()
                                                    onWidthChanged: requestPaint()
                                                    onHeightChanged: requestPaint()
                                                    onVisibleChanged: if (visible) requestPaint()
                                                    onPaint: {
                                                        if (!visible) return;
                                                        var c = getContext("2d");
                                                        var horizon = height * .38;
                                                        c.reset();
                                                        c.fillStyle = backdrop;
                                                        c.fillRect(0, 0, width, height);
                                                        var glow = c.createLinearGradient(0, 0, 0, height);
                                                        glow.addColorStop(0, Qt.alpha(gridColor, 0));
                                                        glow.addColorStop(.38, Qt.alpha(gridColor, .16));
                                                        glow.addColorStop(1, Qt.alpha(gridColor, .025));
                                                        c.fillStyle = glow;
                                                        c.fillRect(0, 0, width, height);
                                                        c.beginPath();
                                                        for (var i = -6; i <= 6; i++) {
                                                            c.moveTo(width / 2 + i * 2, horizon);
                                                            c.lineTo(width / 2 + i * width / 5, height);
                                                        }
                                                        for (var row = 1; row <= 6; row++) {
                                                            var y = horizon + (height - horizon) * Math.pow(row / 6, 2);
                                                            c.moveTo(0, y);
                                                            c.lineTo(width, y);
                                                        }
                                                        c.strokeStyle = Qt.alpha(gridColor, .48);
                                                        c.lineWidth = 1;
                                                        c.stroke();
                                                        c.beginPath();
                                                        c.moveTo(0, horizon);
                                                        c.lineTo(width, horizon);
                                                        c.strokeStyle = Qt.alpha(gridColor, .1);
                                                        c.lineWidth = 5;
                                                        c.stroke();
                                                        c.strokeStyle = Qt.alpha(gridColor, .7);
                                                        c.lineWidth = 1;
                                                        c.stroke();
                                                    }
                                                }
                                                Label {
                                                    x: 8; y: 75; width: parent.width - 16
                                                    text: environmentTile.displayName
                                                    elide: Text.ElideRight
                                                    font.pixelSize: Style.font.bodySmall
                                                    font.bold: environmentTile.selected
                                                }
                                                Rectangle {
                                                    visible: environmentTile.selected
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.margins: 6
                                                    width: 22; height: 22
                                                    color: Color.accent
                                                    radius: Style.cornerRadius
                                                    Label {
                                                        anchors.centerIn: parent
                                                        text: "✓"
                                                        color: Color.background
                                                        font.bold: true
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    Label {
                                        text: "Brightness"
                                        helpText: "Adjust the background brightness. Your monitor brightness is unchanged."
                                    }
                                    Ui.PanelSlider {
                                        id: skyBrightness
                                        bar: sliderPalette
                                        Layout.fillWidth: true
                                        minimum: 0; maximum: 100; step: 1; integer: true
                                        value: root.environmentSettings.brightness
                                        enabled: root.loaded && !root.busy && !!root.environmentSettings.id
                                        opacity: enabled ? 1 : .4
                                        trackColor: Qt.alpha(Color.foreground, .2)
                                        activeFocusOnTab: true
                                        Accessible.role: Accessible.Slider
                                        Accessible.name: "Environment brightness"
                                        Accessible.description: Math.round(liveValue) + "%"
                                        Keys.onLeftPressed: if (enabled) root.setEnvironment("brightness", Math.max(0, value - 1))
                                        Keys.onRightPressed: if (enabled) root.setEnvironment("brightness", Math.min(100, value + 1))
                                        onReleased: function(value) { root.setEnvironment("brightness",value); }
                                        Rectangle {
                                            anchors.fill: parent
                                            anchors.margins: -3
                                            color: "transparent"
                                            border.width: parent.activeFocus ? 1 : 0
                                            border.color: Color.accent
                                            radius: Style.cornerRadius
                                        }
                                    }
                                    Label {
                                        Layout.minimumWidth: 44
                                        horizontalAlignment: Text.AlignRight
                                        text: Math.round(skyBrightness.liveValue) + "%"
                                    }
                                }
                                Hint {
                                    visible: !!root.performance.environmentError
                                    text: root.performance.environmentError || ""
                                    color: Color.urgent
                                }
                                Disclosure {
                                    title: "Environment options"
                                    helpText: "Adjust the background or import your own panorama."
                                    RowLayout {
                                        visible: root.environmentSettings.id === "builtin:tron"
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            text: "Animate glow"
                                            helpText: "Gently vary the horizon and structure glow over 24 seconds. The grid stays stationary; no geometry moves."
                                        }
                                        Ui.ToggleSwitch {
                                            checked: root.environmentSettings.animated !== false
                                            enabled: root.loaded && !root.busy
                                            activeFocusOnTab: true
                                            Accessible.role: Accessible.CheckBox
                                            Accessible.name: "Animate glow"
                                            Accessible.description: "Slow stationary glow; no moving geometry"
                                            Accessible.checked: checked
                                            Keys.onSpacePressed: if (enabled) toggled()
                                            Accessible.onToggleAction: if (enabled) toggled()
                                            onToggled: root.setEnvironment("animated", !checked)
                                            Rectangle {
                                                anchors.fill: parent
                                                anchors.margins: -3
                                                color: "transparent"
                                                border.width: parent.activeFocus ? 1 : 0
                                                border.color: Color.accent
                                                radius: Style.cornerRadius
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 12
                                        Ui.NumberField {
                                            label: "Rotation (°)"
                                            from: -180; to: 180; stepSize: 5
                                            value: root.environmentSettings.rotation
                                            enabled: root.loaded && !root.busy && !!root.environmentSettings.id
                                            onModified: function(value) { root.setEnvironment("rotation",value); }
                                        }
                                        BoundDropdown {
                                            label: "Import resolution"
                                            sourceValue: String(root.imageResolution)
                                            options: [{value:"4096",label:"4K"},{value:"8192",label:"8K"}]
                                            onChanged: function(picked) { root.imageResolution=Number(picked); }
                                        }
                                        Item { Layout.fillWidth: true }
                                        Action {
                                            Layout.alignment: Qt.AlignBottom
                                            text: "Import panorama…"
                                            helpText: root.canImportEnvironment
                                                ? "Choose a 2:1 JPEG, PNG, or BMP. Images stay on this computer. 4K is recommended."
                                                : "Panorama import needs the optional ImageMagick package."
                                            enabled: root.loaded && !root.busy && root.canImportEnvironment
                                            onClicked: environmentFile.open()
                                        }
                                    }
                                    Label {
                                        visible: !root.canImportEnvironment
                                        text: "Panorama import is unavailable"
                                        helpText: "Install the optional ImageMagick package to import your own background."
                                    }
                                }
                            }
                        }
                        ColumnLayout {
                            visible: root.activeTab === 3
                            Layout.fillWidth: true
                            spacing: 12
                            Card {
                                id: sdkControls
                                readonly property var sdk: root.glasses.sdk || ({})
                                RowLayout {
                                    Layout.fillWidth: true
                                    Heading { text: "Glasses connection" }
                                    Action {
                                        text: "Check connection"
                                        helpText: "Refresh the glasses video and head-tracking status"
                                        enabled: root.loaded && !root.busy
                                        onClicked: root.send("check")
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: (root.glasses.usb ? "Glasses connected" : "Glasses not detected") + " · " + (root.glasses.dedicatedDisplay ? "Stereo active" : root.glasses.detectionError || (root.glasses.displays && root.glasses.displays.length ? "Video connected" : "Glasses video not detected"))
                                }
                                Hint {
                                    text: sdkControls.sdk.tracking ? "Head tracking active" : sdkControls.sdk.communication ? "Head tracking is starting" : sdkControls.sdk.available ? "Head tracking disconnected" : "Head tracking software not installed"
                                    helpText: !sdkControls.sdk.available ? "Install the XR software to enable head tracking" : sdkControls.sdk.communication ? "The glasses are connected" : "Choose Connect tracking to begin"
                                }
                                Label {
                                    visible: !!(sdkControls.sdk.trackingError || sdkControls.sdk.displayError)
                                    text: [sdkControls.sdk.trackingError, sdkControls.sdk.displayError].filter(function (x) { return !!x; }).join(" · ")
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
                            }
                            Disclosure {
                                title: "Setup & integrations"
                                helpText: "Install the XR software and optional controls"
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Action {
                                        text: !root.glasses.runtimeInstalled || !sdkControls.sdk.available ? "Install XR runtime" : sdkControls.sdk.licenseAccepted === false ? "Finish XR setup" : !root.glasses.helperAvailable ? (sdkControls.sdk.packaged ? "Repair XR runtime" : "Install stereo helper") : "XR runtime installed"
                                        helpText: "Install the XR software or finish its one-time setup"
                                        enabled: !root.glasses.runtimeInstalled || !sdkControls.sdk.available || sdkControls.sdk.licenseAccepted === false || !root.glasses.helperAvailable
                                        onClicked: root.glasses.runtimeInstalled && !root.glasses.helperAvailable && sdkControls.sdk.available && !sdkControls.sdk.packaged
                                            ? root.runSetupAction("helper") : root.installRuntime()
                                    }
                                    Action {
                                        text: "Set up shortcuts & gestures"
                                        helpText: "Add or refresh the optional touchpad gestures and keyboard shortcuts"
                                        onClicked: root.runSetupAction("controls")
                                    }
                                    Action {
                                        text: "Set up XR notifications"
                                        helpText: "Show optional floating notifications while using stereo"
                                        onClicked: root.runSetupAction("notifications")
                                    }
                                }
                                Hint {
                                    text: "A terminal opens so you can review terms and approve any system changes."
                                }
                            }
                            Disclosure {
                                title: "Connection tools"
                                helpText: "Reconnect head tracking or recover the glasses connection"
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Action {
                                        text: sdkControls.sdk.busy ? "Connecting…" : sdkControls.sdk.communication ? "Reconnect tracking" : "Connect tracking"
                                        helpText: "Connect to the glasses and start head tracking"
                                        enabled: root.loaded && !root.busy && !root.directOutput && !sdkControls.sdk.busy && !root.glasses.recovering
                                        onClicked: root.send("sdk_connect")
                                    }
                                    Action {
                                        text: "Restore glasses video"
                                        helpText: "Switch the glasses back to their normal video mode"
                                        enabled: root.loaded && !root.busy && !root.directOutput && !!sdkControls.sdk.communication && !sdkControls.sdk.busy && !root.glasses.recovering
                                        onClicked: root.send("sdk_restore")
                                    }
                                    Action {
                                        text: root.viewing ? "Stop XR & disconnect" : "Disconnect tracking"
                                        helpText: "Stop the current XR view and disconnect head tracking"
                                        enabled: root.loaded && !root.busy && (!!sdkControls.sdk.communication || !!sdkControls.sdk.busy)
                                        onClicked: root.send("sdk_disconnect")
                                    }
                                    Action {
                                        text: root.glasses.recovering ? "Reinitializing…" : "Reinitialize USB-C…"
                                        helpText: root.glasses.canReset ? "Review the USB-C reset confirmation before reconnecting the glasses" : "Automatic recovery is unavailable. Reconnect the USB-C cable."
                                        enabled: root.loaded && !root.busy && !!root.glasses.canReset
                                        onClicked: root.requestRecovery()
                                    }
                                }
                                Hint {
                                    visible: root.loaded && !root.glasses.canReset && !root.glasses.recovering && !root.glasses.usb
                                    text: "Reconnect the USB-C cable to restore the connection."
                                }
                            }
                            Disclosure {
                                title: root.performance.spectatorError ? "Recording · needs attention" : "Recording"
                                helpText: root.performance.spectatorError || "Create a flat recording window for OBS; it opens with stereo and runs at up to 30 fps"
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        text: "Mono recording window"
                                        helpText: "In OBS, capture the window named ‘Omarchy XR — Mono spectator’"
                                    }
                                    Ui.ToggleSwitch {
                                        checked: root.spectatorEnabled
                                        busy: root.busy
                                        activeFocusOnTab: true
                                        Accessible.role: Accessible.CheckBox
                                        Accessible.name: "Mono recording window"
                                        Accessible.checked: checked
                                        Keys.onSpacePressed: if (enabled && !busy) toggled()
                                        Accessible.onToggleAction: if (enabled && !busy) toggled()
                                        onToggled: root.send("set_spectator", !root.spectatorEnabled)
                                        Rectangle {
                                            anchors.fill: parent
                                            anchors.margins: -3
                                            color: "transparent"
                                            border.width: parent.activeFocus ? 1 : 0
                                            border.color: Color.accent
                                            radius: Style.cornerRadius
                                        }
                                    }
                                }
                                Hint {
                                    visible: root.spectatorEnabled || !!root.performance.spectatorError
                                    text: root.performance.spectatorError ? root.performance.spectatorError : root.performance.spectator ? "Recording window active" : root.directOutput ? "Window closed or opening" : "Ready for the next stereo session"
                                    color: root.performance.spectatorError ? Color.urgent : Qt.alpha(Color.foreground, .68)
                                }
                                Action {
                                    visible: root.directOutput && root.spectatorEnabled && !root.performance.spectator
                                    text: "Reopen recording window"
                                    enabled: !root.busy
                                    onClicked: root.send("set_spectator", true)
                                }
                            }
                            Disclosure {
                                title: "Performance"
                                helpText: "Frame rate and capture details for troubleshooting"
                                Hint {
                                    text: root.directOutput ? "Stereo · dedicated display" : "Mono · desktop rendering"
                                    helpText: root.directOutput ? "Direct side-by-side stereo on the glasses" : sdkControls.sdk.nativeDof === false ? "Flat preview on the glasses; built-in tracking is unavailable" : "Flat preview on the glasses"
                                }
                                QQC.ScrollView {
                                    id: performanceScroll
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Math.min(180, performanceDetails.implicitHeight + 12)
                                    contentWidth: availableWidth
                                    clip: true
                                    QQC.ScrollBar.horizontal.policy: QQC.ScrollBar.AlwaysOff
                                    ColumnLayout {
                                        id: performanceDetails
                                        width: performanceScroll.availableWidth
                                        spacing: 8
                                        Hint {
                                            text: root.performance.fps !== undefined ? root.performance.fps.toFixed(1)+" fps · CPU frame time "+root.performance.workP95.toFixed(2)+" ms · total frame time "+root.performance.frameP95.toFixed(2)+" ms"
                                                +(root.performance.gpuSceneP95 !== undefined ? " · graphics "+root.performance.gpuSceneP95.toFixed(2)+" ms · capture "+root.performance.gpuCaptureP95.toFixed(2)+" ms" : "")
                                                +(root.performance.refreshHz ? " · missed frames "+root.performance.missedVblanksWindow+" recent / "+root.performance.missedVblanks+" total" : "")
                                                : "Start XR to measure performance."
                                            helpText: "Frame times show a typical slow frame; lower is better"
                                        }
                                        Repeater {
                                            model: root.captureRows
                                            Hint {
                                                required property int index
                                                required property var modelData
                                                text: "Monitor "+(index+1)+" · "+(modelData.visible ? modelData.width+" × "+modelData.height : "Capture paused")+(modelData.importMs !== undefined ? " · capture time "+Number(modelData.importMs).toFixed(1)+" ms" : "")
                                            }
                                        }
                                    }
                                }
                            }
                            Disclosure {
                                title: "Preview & stop"
                                helpText: "Open a preview, or stop XR and remove its virtual monitors"
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    enabled: root.loaded
                                    Action {
                                        text: "Open windowed preview"
                                        helpText: "Preview the applied monitor layout on the desktop; apply pending changes first"
                                        enabled: root.activeCount > 0 && !root.dirty && !root.viewing && !root.busy
                                        onClicked: root.send("start")
                                    }
                                    Action {
                                        text: "Open flat preview on glasses"
                                        helpText: "Show a non-stereo preview on one connected pair of glasses"
                                        enabled: !!root.glasses.displays && root.glasses.displays.length === 1 && !root.viewing && !root.busy
                                        onClicked: root.send("present")
                                    }
                                    Action {
                                        text: "Stop & remove monitors"
                                        helpText: "Stop XR and move windows from its virtual monitors to another display"
                                        enabled: root.activeCount > 0
                                        onClicked: root.send("stop")
                                    }
                                }
                            }
                            Disclosure {
                                title: "Session activity"
                                helpText: "Recent actions, newest first; select text and press Ctrl+C to copy"
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.history.length + " events"
                                    }
                                    Action {
                                        text: "Clear activity"
                                        helpText: "Clear this session's activity list"
                                        enabled: root.history.length > 0
                                        onClicked: root.history = []
                                    }
                                }
                                QQC.ScrollView {
                                    id: activityScroll
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 160
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
                                            return e.time + "  " + (e.failed ? "NEEDS ATTENTION  " : "") + e.message;
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
                    Label {
                        Layout.fillWidth: true
                        text: root.backendSlow ? "Still working…" : root.busy ? "Working…" : root.activeCount + (root.activeCount === 1 ? " active monitor" : " active monitors")
                        helpText: root.backendSlow ? "This is taking longer than expected. You can still use Stop & remove monitors in Utilities." : "Virtual monitors currently available on your desktop"
                        color: root.busy ? Color.accent : Color.foreground
                    }
                    Action {
                        visible: root.activeTab === 1
                        text: "Save layout"
                        helpText: "Save for the next session without applying"
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
