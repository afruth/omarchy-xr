#!/usr/bin/env python3
"""Preview native Studio QML against a read-only snapshot, with no XR backend.

python3 scripts/preview-ui.py prepare
python3 scripts/preview-ui.py run
python3 scripts/preview-ui.py call tab 2
python3 scripts/preview-ui.py capture /tmp/environment.png

Run stays in the foreground. IPC and screenshots use only the isolated preview.
Prepare again to refresh source; Quickshell reloads the isolated configuration.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


ROOT = Path(__file__).resolve().parent.parent
TITLE = "XR Studio UI Preview"
DEFAULT_DIRECTORY = "/tmp/omarchy-xr-ui-preview"

PREVIEW_METHODS = r'''
    property var previewActions: []
    function previewInitialize(data) {
        var aliases = {environment:"environmentSettings", environments:"environmentItems",
            setups:"savedSetups", controls:"controlDraft", active:"activeCount", direct:"directOutput",
            confirmation:"confirmRecovery", tab:"activeTab"};
        Object.keys(data).forEach(function(key) {
            var property = aliases[key] || key;
            if (property in root && ["busy", "scrollHeight", "viewportHeight"].indexOf(key) < 0)
                root[property] = data[key];
        });
        root.captureRows = root.performance.captures || [];
        if (!("canImportEnvironment" in data)) root.canImportEnvironment = true;
        if (!("loaded" in data)) root.loaded = true;
        root.open({});
        Qt.callLater(root.fit);
    }
    function previewSize(width, height) {
        window.width = width;
        window.height = height;
        Qt.callLater(root.fit);
    }
    function previewItems() {
        var result = [];
        function visit(item, path) {
            if (!item || !item.visible) return;
            result.push({item:item, path:path});
            var children = item.children || [];
            for (var i = 0; i < children.length; i++) visit(children[i], path + "/" + i);
        }
        // Quickshell places popup overlays beside its proxy content item.
        visit(window.contentItem.parent || window.contentItem, "root");
        return result;
    }
    function previewTree() {
        return JSON.stringify(previewItems().map(function(entry) {
            var item = entry.item;
            var pos = item.mapToItem(panelBody.item, 0, 0);
            return {path:entry.path, type:String(item), objectName:item.objectName,
                text:item.text === undefined ? "" : String(item.text),
                name:item.Accessible ? item.Accessible.name : "",
                x:pos.x, y:pos.y, width:item.width, height:item.height, enabled:item.enabled,
                focused:item.activeFocus, label:item.label === undefined ? "" : String(item.label),
                clickable:typeof item.clicked === "function",
                title:item.title === undefined ? "" : String(item.title)};
        }));
    }
    function previewClick(label) {
        var entries = previewItems();
        for (var i=0; i<entries.length; i++) {
            var item = entries[i].item;
            var name = item.Accessible ? item.Accessible.name : "";
            if (item.enabled && typeof item.clicked === "function" &&
                (item.text === label || name === label || entries[i].path === label)) {
                item.clicked();
                return "clicked " + label;
            }
        }
        return "not found: " + label;
    }
    function previewMatches(entry, label) {
        var item = entry.item;
        return item.text === label || item.label === label || entry.path === label ||
            (item.Accessible && item.Accessible.name === label);
    }
    function previewInput(label, value) {
        var entries = previewItems();
        for (var i=0; i<entries.length; i++) {
            var item = entries[i].item;
            if (!item.enabled || !previewMatches(entries[i], label)) continue;
            if (typeof item.modified === "function") {
                if (item.field) item.field.forceActiveFocus();
                item.modified(Math.max(item.from, Math.min(item.to, Number(value))));
            } else if (typeof item.changed === "function") item.changed(value);
            else if (typeof item.textEdited === "function") { item.text=value; item.textEdited(); }
            else if (item.minimum !== undefined && typeof item.released === "function") item.released(Number(value));
            else if (typeof item.toggled === "function") item.toggled();
            else continue;
            return "input " + label + " = " + value;
        }
        return "not found: " + label;
    }
    function previewFocus(label) {
        var entries = previewItems();
        for (var i=0; i<entries.length; i++) {
            var item = entries[i].item;
            if (!item.enabled || !previewMatches(entries[i], label)) continue;
            (item.field || item).forceActiveFocus();
            return "focused " + label;
        }
        return "not found: " + label;
    }
    function previewDisclosure(title, expanded) {
        var entries = previewItems();
        for (var i=0; i<entries.length; i++) {
            var item = entries[i].item;
            if (item.title === title && item.expanded !== undefined) {
                item.expanded = expanded;
                return "expanded=" + expanded;
            }
        }
        return "not found: " + title;
    }
    function previewTheme(data) {
        Object.keys(data).forEach(function(key) {
            if (["foreground", "background", "accent", "urgent", "muted", "shellValues"].indexOf(key) >= 0)
                Color[key] = data[key];
        });
        if (data.shellValues !== undefined) Style.applyShellValues(data.shellValues);
        root.repaintGrid();
    }
    function previewFont(size) {
        Style.fontBaseSize = Math.max(1, size);
        Qt.callLater(root.fit);
        return "font base size = " + Style.font.baseSize;
    }
    function previewOpenUrl(url) {
        var entries = previewActions.slice();
        entries.push({action:"open_url", url:url});
        previewActions = entries;
        return true;
    }
'''

SHELL = '''import QtQuick
import Quickshell
import Quickshell.Io

ShellRoot {
    MonitorStudio {
        id: studio
        Component.onCompleted: previewInitialize(SNAPSHOT)
    }
    IpcHandler {
        target: "ui"
        function tab(index: int): string { studio.selectTab(index); return studio.snapshot(); }
        function snapshot(): string { return studio.snapshot(); }
        function size(width: int, height: int): string {
            studio.previewSize(width, height); return "resized";
        }
        function tree(): string { return studio.previewTree(); }
        function click(label: string): string { return studio.previewClick(label); }
        function input(label: string, value: string): string { return studio.previewInput(label, value); }
        function focus(label: string): string { return studio.previewFocus(label); }
        function font(size: int): string { return studio.previewFont(size); }
        function disclosure(title: string, expanded: bool): string {
            return studio.previewDisclosure(title, expanded);
        }
        function fixture(data: string): string {
            studio.previewInitialize(JSON.parse(data)); return studio.snapshot();
        }
        function theme(data: string): string { studio.previewTheme(JSON.parse(data)); return "theme changed locally"; }
        function actions(): string { return JSON.stringify(studio.previewActions); }
    }
}
'''


def command(*args: str) -> str:
    return subprocess.check_output(args, text=True).strip()


def replace_region(source: str, start: str, end: str, replacement: str) -> str:
    first = source.index(start)
    last = source.index(end, first)
    return source[:first] + replacement + source[last:]


def isolated_source(source: str) -> str:
    source = replace_region(source, "    Process {\n        id: backend", "    Timer {", "")
    source = replace_region(source, "    function send(", "    function changed()", '''    function send(action, enabled, selectedSetup, updateSetup) {
        if (action === "status") return;
        var entries = previewActions.slice();
        entries.push({action:action, enabled:enabled, setup:selectedSetup, update:updateSetup});
        previewActions = entries;
        console.log("PREVIEW action: " + action);
    }
''')
    source = source.replace('        if (!backend.running)\n            backend.running = true;\n', '')
    source = source.replace('title: "XR Monitor Studio"', f'title: "{TITLE}"')
    source = source.replace('Qt.openUrlExternally(', 'root.previewOpenUrl(')
    source = source.replace("    id: root\n", "    id: root\n" + PREVIEW_METHODS, 1)
    if "backend." in source or "Process {" in source:
        raise RuntimeError("Refusing preview: an unexpected process or backend reference remains")
    return source


def prepare(directory: Path, snapshot_file: str | None) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    if snapshot_file:
        state = json.loads(Path(snapshot_file).read_text())
    else:
        state = json.loads(command("omarchy-shell", "shell", "call", "afruth.omarchy-xr", "snapshot", ""))
    (directory / "snapshot.json").write_text(json.dumps(state, indent=2))
    for component in (ROOT / "studio").iterdir():
        if component.suffix in {".qml", ".js"} and component.name != "MonitorStudio.qml":
            shutil.copy2(component, directory / component.name)
    for module in ("Ui", "Commons"):
        target = directory / module
        if not target.exists():
            target.symlink_to(Path("/usr/share/omarchy/shell") / module, target_is_directory=True)
    source = isolated_source((ROOT / "studio/MonitorStudio.qml").read_text())
    staged = directory / "MonitorStudio.qml.pending"
    staged.write_text(source)
    staged.replace(directory / "MonitorStudio.qml")
    (directory / "shell.qml").write_text(SHELL.replace("SNAPSHOT", json.dumps(state)))
    print(directory)


def preview_client() -> dict:
    clients = json.loads(command("hyprctl", "-j", "clients"))
    matches = [client for client in clients if client["title"] == TITLE]
    if len(matches) != 1:
        raise RuntimeError(f"Expected one isolated preview window, found {len(matches)}")
    return matches[0]


def capture(path: str) -> None:
    client = preview_client()
    command("hyprctl", "eval", 'for _,w in ipairs(hl.get_windows()) do '
            f'if w.title == "{TITLE}" then hl.dispatch(hl.dsp.focus({{window=w}})) end end')
    time.sleep(0.3)
    x, y = client["at"]
    width, height = client["size"]
    destination = Path(path).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    command("grim", "-g", f"{x},{y} {width}x{height}", str(destination))
    print(destination)


def float_preview(arguments: list[str]) -> None:
    width, height = map(int, arguments or ["1100", "760"])
    client = preview_client()
    monitors = json.loads(command("hyprctl", "-j", "monitors"))
    monitor = next(item for item in monitors if item["id"] == client["monitor"])
    if monitor["name"].startswith("OMXR-"):
        monitor = next(item for item in monitors if not item["name"].startswith("OMXR-"))
    x = round(monitor["x"] + (monitor["width"] / monitor["scale"] - width) / 2)
    y = round(monitor["y"] + (monitor["height"] / monitor["scale"] - height) / 2)
    float_command = '' if client["floating"] else 'hl.dispatch(hl.dsp.window.float({window=w,action="toggle"})); '
    workspace = int(monitor["activeWorkspace"]["id"])
    lua = ('for _,w in ipairs(hl.get_windows()) do '
           f'if w.title == "{TITLE}" then '
           f'hl.dispatch(hl.dsp.window.move({{window=w,workspace="{workspace}",follow=false}})); '
           + float_command +
           f'hl.dispatch(hl.dsp.window.resize({{window=w,x={width},y={height},relative=false}})); '
           f'hl.dispatch(hl.dsp.window.move({{window=w,x={x},y={y},relative=false}})) end end')
    print(command("hyprctl", "eval", lua))
    for _ in range(20):
        actual = preview_client()
        if actual["floating"] and actual["size"] == [width, height]:
            return
        time.sleep(.05)
    raise RuntimeError(f"Preview did not reach the requested {width}×{height}: {actual['size']}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", default=DEFAULT_DIRECTORY)
    parser.add_argument("--snapshot", help="Use recorded JSON rather than reading live state")
    parser.add_argument("operation", choices=("prepare", "run", "call", "capture", "float"))
    parser.add_argument("arguments", nargs="*")
    args = parser.parse_args()
    directory = Path(args.directory).resolve()
    if args.operation == "prepare":
        prepare(directory, args.snapshot)
    elif args.operation == "run":
        os.execvp("quickshell", ["quickshell", "-p", str(directory), "--no-color"])
    elif args.operation == "call":
        print(command("quickshell", "ipc", "-p", str(directory), "call", "ui", *args.arguments))
    elif args.operation == "float":
        float_preview(args.arguments)
    else:
        capture(args.arguments[0])


if __name__ == "__main__":
    main()
