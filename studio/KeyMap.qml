import QtQuick
import QtQuick.Controls

// The XR key layer editor (docs/xr-controls-plan.md §8): the modifier and every action's key on one screen.
// Plain QtQuick so qmltestrunner can load it without Quickshell. It only reports edits (edited(profile));
// the Studio saves them, and studio/input_settings.py validates again on save. The conflict rules below
// mirror input_settings.conflicts().
Item {
    id: root
    // [{id, group, title, default, scope}] from the backend (input_settings.ACTIONS).
    property var actions: []
    // {version, modifier, fingers, keys: {id: key}}; a key is a Hyprland key name, optionally "SHIFT + ".
    property var profile: ({modifier: "CTRL + ALT", keys: {}})
    // Non-XR Hyprland bindings: [{modmask, key, description}].
    property var bindings: []
    property bool locked: false
    property color accent: palette.highlight
    property color foreground: palette.text
    property color warning: "#f38ba8"
    property string capturing: ""
    readonly property var mods: ({SHIFT: 1, CTRL: 4, ALT: 8, SUPER: 64})
    readonly property var modOrder: ["SHIFT", "CTRL", "ALT", "SUPER"]
    readonly property var groups: {
        var out = [];
        for (var i = 0; i < actions.length; ++i) if (out.indexOf(actions[i].group) < 0) out.push(actions[i].group);
        return out;
    }
    readonly property string modifierError: modifierProblem(profile.modifier)
    readonly property var conflicts: conflictsOf(profile)
    readonly property int conflictCount: Object.keys(conflicts).length + (modifierError ? 1 : 0)
    signal edited(var profile)
    implicitWidth: 720
    implicitHeight: layout.implicitHeight

    function modList(text) {
        return String(text || "").split("+").map(function(p) { return p.trim().toUpperCase(); }).filter(function(p) { return p !== ""; });
    }
    function maskOf(text) {
        return modList(text).reduce(function(sum, m) { return sum + (root.mods[m] || 0); }, 0);
    }
    function modifierProblem(text) {
        var list = modList(text);
        if (list.length < 2 || !list.some(function(m) { return m === "CTRL" || m === "ALT" || m === "SUPER"; }))
            return "The XR key layer needs two or more modifiers, such as CTRL + ALT";
        return "";
    }
    function keyOf(id) { return (profile.keys || {})[id] || ""; }
    function titleOf(id) {
        for (var i = 0; i < actions.length; ++i) if (actions[i].id === id) return actions[i].title;
        return id;
    }
    function parts(key) {
        var p = String(key).split("+").map(function(s) { return s.trim(); });
        return {shift: p.length > 1 && p[0].toUpperCase() === "SHIFT", name: p[p.length - 1]};
    }
    // {id: message} for every key that cannot be bound, in action order (the first key wins a clash).
    function conflictsOf(value) {
        var out = {}, seen = {}, mask = maskOf(value.modifier);
        for (var i = 0; i < actions.length; ++i) {
            var id = actions[i].id, key = (value.keys || {})[id] || "";
            if (key === "") continue;
            var k = parts(key);
            if (k.shift && (mask & 1)) { out[id] = "The XR modifier already includes SHIFT"; continue; }
            var full = (mask | (k.shift ? 1 : 0)) + ":" + k.name.toLowerCase();
            if (seen[full]) { out[id] = "Same key as " + titleOf(seen[full]); continue; }
            seen[full] = id;
            if ((mask & 4) && (mask & 8) && /^f([1-9]|1[0-2])$/.test(k.name.toLowerCase())) { out[id] = "CTRL + ALT + F1…F12 switch virtual terminals"; continue; }
            for (var b = 0; b < bindings.length; ++b) {
                var bind = bindings[b];
                if (bind.modmask === (mask | (k.shift ? 1 : 0)) && String(bind.key).toLowerCase() === k.name.toLowerCase()
                    && !String(bind.description || "").startsWith("XR:")) {
                    out[id] = "Already used by " + (bind.description || "another desktop binding");
                    break;
                }
            }
        }
        return out;
    }
    readonly property var pretty: ({space: "Space", equal: "=", minus: "-", comma: ",", period: ".", slash: "/", semicolon: ";",
        apostrophe: "'", bracketleft: "[", bracketright: "]", backslash: "\\", grave: "`", Return: "Enter", Page_Up: "PgUp",
        Page_Down: "PgDn", Up: "↑", Down: "↓", Left: "←", Right: "→", BackSpace: "Backspace"})
    function display(key) {
        if (!key) return "Off";
        var k = parts(key);
        var mods = modList(profile.modifier).concat(k.shift ? ["SHIFT"] : []);
        return mods.map(function(m) { return m.charAt(0) + m.slice(1).toLowerCase(); }).join("+") + "+" + (pretty[k.name] || k.name);
    }
    // A pressed key as a Hyprland key name, "" for keys the layer cannot use (modifiers alone, unknown).
    function keyName(event) {
        var k = event.key, shift = !!(event.modifiers & Qt.ShiftModifier);
        var name = "";
        if (k >= Qt.Key_A && k <= Qt.Key_Z) name = String.fromCharCode(k);
        else if (k >= Qt.Key_0 && k <= Qt.Key_9) name = String.fromCharCode(k);
        else if (k >= Qt.Key_F1 && k <= Qt.Key_F35) name = "F" + (k - Qt.Key_F1 + 1);
        else {
            var table = {};
            table[Qt.Key_Up] = "Up"; table[Qt.Key_Down] = "Down"; table[Qt.Key_Left] = "Left"; table[Qt.Key_Right] = "Right";
            table[Qt.Key_Home] = "Home"; table[Qt.Key_End] = "End"; table[Qt.Key_PageUp] = "Page_Up"; table[Qt.Key_PageDown] = "Page_Down";
            table[Qt.Key_Return] = "Return"; table[Qt.Key_Enter] = "Return"; table[Qt.Key_Space] = "space"; table[Qt.Key_Tab] = "Tab";
            table[Qt.Key_Insert] = "Insert"; table[Qt.Key_Delete] = "Delete";
            table[Qt.Key_Minus] = "minus"; table[Qt.Key_Equal] = "equal"; table[Qt.Key_Comma] = "comma"; table[Qt.Key_Period] = "period";
            table[Qt.Key_Slash] = "slash"; table[Qt.Key_Semicolon] = "semicolon"; table[Qt.Key_Apostrophe] = "apostrophe";
            table[Qt.Key_BracketLeft] = "bracketleft"; table[Qt.Key_BracketRight] = "bracketright"; table[Qt.Key_Backslash] = "backslash";
            table[Qt.Key_QuoteLeft] = "grave";
            // The shifted symbols of a US layout name the key they sit on.
            var shifted = {};
            shifted[Qt.Key_Backtab] = "Tab"; shifted[Qt.Key_Underscore] = "minus"; shifted[Qt.Key_Plus] = "equal"; shifted[Qt.Key_Less] = "comma";
            shifted[Qt.Key_Greater] = "period"; shifted[Qt.Key_Question] = "slash"; shifted[Qt.Key_Colon] = "semicolon";
            shifted[Qt.Key_QuoteDbl] = "apostrophe"; shifted[Qt.Key_BraceLeft] = "bracketleft"; shifted[Qt.Key_BraceRight] = "bracketright";
            shifted[Qt.Key_Bar] = "backslash"; shifted[Qt.Key_AsciiTilde] = "grave";
            if (table[k]) name = table[k];
            else if (shifted[k]) { name = shifted[k]; shift = true; }
        }
        if (name === "") return "";
        return (shift ? "SHIFT + " : "") + name;
    }
    function withKey(id, key) {
        var keys = Object.assign({}, profile.keys || {});
        keys[id] = key;
        return Object.assign({}, profile, {keys: keys});
    }
    function setKey(id, key) { capturing = ""; edited(withKey(id, key)); }
    function toggleModifier(name) {
        var list = modList(profile.modifier);
        var next = list.indexOf(name) >= 0 ? list.filter(function(m) { return m !== name; }) : list.concat([name]);
        edited(Object.assign({}, profile, {modifier: modOrder.filter(function(m) { return next.indexOf(m) >= 0; }).join(" + ")}));
    }
    // While capturing: Esc cancels, Backspace turns the action off, any usable key is taken.
    function capture(id, event) {
        if (event.key === Qt.Key_Escape) { capturing = ""; event.accepted = true; return; }
        if (event.key === Qt.Key_Backspace) { setKey(id, ""); event.accepted = true; return; }
        var name = keyName(event);
        event.accepted = true;
        if (name !== "") setKey(id, name);
    }

    Column {
        id: layout
        width: root.width
        spacing: 14
        Row {
            spacing: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "XR key layer"
                textFormat: Text.PlainText
                color: root.foreground
                font.bold: true
            }
            Repeater {
                model: root.modOrder
                AbstractButton {
                    id: chip
                    required property string modelData
                    objectName: "modifier-" + modelData
                    readonly property bool on: root.modList(root.profile.modifier).indexOf(modelData) >= 0
                    enabled: !root.locked
                    activeFocusOnTab: true
                    implicitWidth: chipLabel.implicitWidth + 20
                    implicitHeight: chipLabel.implicitHeight + 10
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: modelData + " in the XR modifier"
                    Accessible.checked: on
                    Accessible.onPressAction: if (enabled) clicked()
                    Keys.onSpacePressed: clicked()
                    onClicked: root.toggleModifier(modelData)
                    background: Rectangle {
                        radius: 6
                        color: chip.on ? Qt.alpha(root.accent, .25) : "transparent"
                        border.width: 1
                        border.color: chip.on || chip.activeFocus ? root.accent : Qt.alpha(root.foreground, .25)
                    }
                    contentItem: Text {
                        id: chipLabel
                        text: chip.modelData
                        textFormat: Text.PlainText
                        horizontalAlignment: Text.AlignHCenter
                        color: root.foreground
                        font.bold: chip.on
                    }
                }
            }
            Text {
                objectName: "conflict-count"
                anchors.verticalCenter: parent.verticalCenter
                visible: root.conflictCount > 0
                text: root.conflictCount === 1 ? "1 conflict" : root.conflictCount + " conflicts"
                textFormat: Text.PlainText
                color: root.warning
            }
        }
        Text {
            objectName: "modifier-error"
            width: parent.width
            visible: root.modifierError !== ""
            text: root.modifierError
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: root.warning
        }
        // Two columns of groups on a wide Studio, one on a narrow one.
        Flow {
            id: grid
            readonly property int columns: root.width >= 760 ? 2 : 1
            width: parent.width
            spacing: 24
            Repeater {
                model: root.groups
                Column {
                    id: group
                    required property string modelData
                    width: Math.floor((grid.width - grid.spacing * (grid.columns - 1)) / grid.columns)
                    spacing: 4
                    Text {
                        text: group.modelData
                        textFormat: Text.PlainText
                        color: Qt.alpha(root.foreground, .68)
                        font.bold: true
                    }
                    Repeater {
                        model: root.actions.filter(function(a) { return a.group === group.modelData; })
                        Column {
                            id: row
                            required property var modelData
                            readonly property string key: root.keyOf(modelData.id)
                            readonly property string problem: root.conflicts[modelData.id] || ""
                            width: group.width
                            spacing: 2
                            Item {
                                width: parent.width
                                height: keyButton.implicitHeight
                                Text {
                                    anchors.left: parent.left
                                    anchors.right: tag.visible ? tag.left : reset.left
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: row.modelData.title
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    color: root.foreground
                                }
                                Text {
                                    id: tag
                                    anchors.right: reset.left
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: row.modelData.scope === "canvas"
                                    text: "canvas"
                                    textFormat: Text.PlainText
                                    font.pixelSize: 11
                                    color: Qt.alpha(root.foreground, .55)
                                }
                                AbstractButton {
                                    id: reset
                                    objectName: "reset-" + row.modelData.id
                                    anchors.right: keyButton.left
                                    anchors.rightMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: row.key !== row.modelData.default
                                    enabled: !root.locked
                                    implicitWidth: 26
                                    implicitHeight: 26
                                    Accessible.role: Accessible.Button
                                    Accessible.name: "Reset " + row.modelData.title + " to " + root.display(row.modelData.default)
                                    onClicked: root.setKey(row.modelData.id, row.modelData.default)
                                    contentItem: Text {
                                        text: "↺"
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: root.foreground
                                    }
                                    background: Item {}
                                }
                                AbstractButton {
                                    id: keyButton
                                    objectName: "key-" + row.modelData.id
                                    readonly property bool listening: root.capturing === row.modelData.id
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(200, parent.width * .45)
                                    implicitHeight: keyLabel.implicitHeight + 12
                                    enabled: !root.locked
                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Button
                                    Accessible.name: row.modelData.title + ", " + (row.key ? root.display(row.key) : "off")
                                    Accessible.description: "Press Enter, then the new key. Backspace turns it off, Escape cancels."
                                    Accessible.onPressAction: if (enabled) clicked()
                                    onClicked: { root.capturing = row.modelData.id; forceActiveFocus(); }
                                    onActiveFocusChanged: if (!activeFocus && listening) root.capturing = ""
                                    Keys.onPressed: function(event) {
                                        if (listening) { root.capture(row.modelData.id, event); return; }
                                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                                            clicked(); event.accepted = true;
                                        } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                                            var next = nextItemInFocusChain(event.key === Qt.Key_Down);
                                            while (next && next !== keyButton && String(next.objectName).indexOf("key-") !== 0)
                                                next = next.nextItemInFocusChain(event.key === Qt.Key_Down);
                                            if (next) next.forceActiveFocus();
                                            event.accepted = true;
                                        }
                                    }
                                    background: Rectangle {
                                        radius: 6
                                        color: keyButton.listening ? Qt.alpha(root.accent, .3) : Qt.alpha(root.foreground, .06)
                                        border.width: 1
                                        border.color: row.problem ? root.warning : keyButton.activeFocus || keyButton.listening ? root.accent : Qt.alpha(root.foreground, .2)
                                    }
                                    contentItem: Text {
                                        id: keyLabel
                                        text: keyButton.listening ? "Press a key…" : root.display(row.key)
                                        textFormat: Text.PlainText
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        color: row.key || keyButton.listening ? root.foreground : Qt.alpha(root.foreground, .5)
                                        font.family: "monospace"
                                    }
                                }
                            }
                            Text {
                                objectName: "problem-" + row.modelData.id
                                width: parent.width
                                visible: row.problem !== ""
                                text: row.problem
                                textFormat: Text.PlainText
                                wrapMode: Text.WordWrap
                                font.pixelSize: 12
                                color: root.warning
                            }
                        }
                    }
                }
            }
        }
        Text {
            objectName: "mouse-help"
            width: parent.width
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            color: Qt.alpha(root.foreground, .75)
            text: "Mouse (hold " + root.modList(root.profile.modifier).map(function(m) { return m.charAt(0) + m.slice(1).toLowerCase(); }).join("+")
                + "): wheel zooms · Shift+wheel scrolls the canvas · drag moves a canvas window · right-drag resizes it · hold the middle button to grab"
        }
    }
}
