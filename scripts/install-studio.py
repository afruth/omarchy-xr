#!/usr/bin/env python3
"""Install this project's native Omarchy panel and compiled renderer locally."""
from pathlib import Path
import copy
import json
import os
import shutil
import subprocess
import sys
import tempfile

PLUGIN_ID = "afruth.omarchy-xr"
PLUGIN_FILES = (
    "manifest.json",
    "LICENSE",
    "studio/BarWidget.qml",
    "studio/MonitorStudio.qml",
    "studio/PassiveToolTip.qml",
    "studio/RequestState.qml",
    "studio/MonitorSnap.js",
    "studio/json_equal.js",
    "studio/MonitorPresets.js",
    "studio/CurvatureAngles.js",
    "studio/AngleField.qml",
    "studio/atomic_file.py",
    "studio/backend.py",
    "studio/clock.py",
    "studio/environment.py",
    "studio/laptop_display.py",
    "studio/workspace_presets.py",
    "studio/graphics_limits.py",
    "studio/input_settings.py",
    "studio/glasses.py",
    "studio/sdk.py",
    "studio/sdk_worker.py",
    "studio/install_runtime.py",
    "studio/dedicated.py",
    "studio/dedicated_helper.py",
)
BAR_SECTION_ANCHORS = {"left": "omarchy.workspaces", "center": "omarchy.weather", "right": "omarchy.tray"}


def plugin_target(config_home):
    return Path(config_home) / "omarchy/plugins" / PLUGIN_ID


def bar_entry_id(entry):
    if isinstance(entry, dict):
        return str(entry.get("id") or "")
    return str(entry or "")


def widget_in_bar(config, plugin_id=PLUGIN_ID):
    layout = ((config or {}).get("bar") or {}).get("layout") or {}
    if not isinstance(layout, dict):
        return False
    for section in ("left", "center", "right"):
        entries = layout.get(section) or []
        if isinstance(entries, list) and any(bar_entry_id(entry) == plugin_id for entry in entries):
            return True
    return False


def ensure_bar_widget(config, plugin_id=PLUGIN_ID, section="right"):
    """Return shell.json with this plugin in the bar layout, unchanged if already present."""
    result = copy.deepcopy(config) if isinstance(config, dict) else {}
    bar = result.get("bar")
    if not isinstance(bar, dict):
        result["bar"] = bar = {}
    layout = bar.get("layout")
    if not isinstance(layout, dict):
        bar["layout"] = layout = {}
    for name in ("left", "center", "right"):
        if not isinstance(layout.get(name), list):
            layout[name] = []
        if any(bar_entry_id(entry) == plugin_id for entry in layout[name]):
            return result
    if section not in BAR_SECTION_ANCHORS:
        section = "right"
    entries = list(layout[section])
    anchor = BAR_SECTION_ANCHORS[section]
    index = next((i + 1 for i, entry in enumerate(entries) if bar_entry_id(entry) == anchor), len(entries))
    entries.insert(index, {"id": plugin_id})
    layout[section] = entries
    return result


def place_bar_widget(shell_json, runner=subprocess.run, plugin_id=PLUGIN_ID, section="right"):
    """Ask the running shell to put the widget, then ensure shell.json carries it."""
    try:
        runner(["omarchy-shell", "shell", "rescanPlugins"], check=False, capture_output=True, text=True, timeout=15)
        runner(["omarchy", "bar", "put", plugin_id], check=False, capture_output=True, text=True, timeout=15)
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        pass
    path = Path(shell_json)
    if not path.is_file():
        return False
    try:
        config = json.loads(path.read_text())
    except json.JSONDecodeError:
        return False
    if not isinstance(config, dict):
        return False
    if widget_in_bar(config, plugin_id):
        return True
    updated = ensure_bar_widget(config, plugin_id, section)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(updated, indent=2) + "\n")
    tmp.replace(path)
    try:
        runner(["omarchy-shell", "shell", "reloadConfig"], check=False, capture_output=True, text=True, timeout=15)
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        pass
    return True


def install_plugin_files(root, target, renderer):
    if target.exists() and (not (target / "manifest.json").exists() or json.loads((target / "manifest.json").read_text()).get("id") != PLUGIN_ID):
        raise SystemExit("Refusing to overwrite an unrelated plugin directory")
    for file in PLUGIN_FILES:
        destination = target / file
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / file, destination)
    (target / "bin").mkdir(exist_ok=True)
    shutil.copy2(renderer, target / "bin/omarchy-xr.new")
    (target / "bin/omarchy-xr.new").replace(target / "bin/omarchy-xr")
    return target


def write_launcher(applications):
    applications.mkdir(parents=True, exist_ok=True)
    (applications / "omarchy-xr-studio.desktop").write_text("""[Desktop Entry]
Type=Application
Name=XR Monitor Studio
Comment=Arrange virtual monitors for Omarchy XR
Exec=omarchy-shell shell summon afruth.omarchy-xr {}
Icon=video-display
Terminal=false
Categories=Settings;HardwareSettings;
""")


def import_bundled_skies(root):
    if not shutil.which("magick"):
        print("Optional panorama import and bundled skies require: sudo pacman -S imagemagick")
        return
    sys.path.insert(0, str(root / "studio"))
    from environment import Environment
    with tempfile.TemporaryDirectory() as state:
        library = Environment(state)
        for asset in sorted((root / "assets/environments").glob("*.png")):
            enhanced = asset.parent / "upscaled" / asset.name
            library.import_image(enhanced if enhanced.is_file() else asset,
                                 resolution=8192 if enhanced.is_file() else 4096,
                                 name=asset.stem.replace("-", " ").title()
                                 + (" (4×)" if enhanced.is_file() else ""))


def install(root=None, config_home=None, data_home=None, runner=subprocess.run, import_skies=True):
    root = Path(root) if root else Path(__file__).resolve().parent.parent
    renderer = root / "build/omarchy-xr"
    if not renderer.is_file():
        raise SystemExit("Build first: make")
    config_home = Path(config_home or os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
    data_home = Path(data_home or os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
    target = install_plugin_files(root, plugin_target(config_home), renderer)
    write_launcher(data_home / "applications")
    placed = place_bar_widget(config_home / "omarchy/shell.json", runner=runner)
    if import_skies:
        import_bundled_skies(root)
    return target, placed


if __name__ == "__main__":
    target, placed = install()
    print(target)
    if not placed:
        print("After opening an Omarchy session: omarchy-shell shell rescanPlugins && omarchy plugin enable "
              + PLUGIN_ID + " && omarchy bar put " + PLUGIN_ID)
