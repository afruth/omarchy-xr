#!/usr/bin/env python3
"""Install a thin Omarchy notification-service extension for stereo mirroring."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

PLUGIN_ID = "afruth.omarchy-xr-notifications"


def install_files(source, config, native):
    if not (native / "Service.qml").is_file():
        raise RuntimeError("Requires Omarchy's Quickshell notification service")
    target = config / "omarchy/plugins" / PLUGIN_ID
    target.mkdir(parents=True, exist_ok=True)
    # Keep one discoverable marketplace plugin in the source repository. This
    # optional extension becomes a separate plugin only during explicit setup.
    for filename, installed in (("manifest.json.in", "manifest.json"), ("Bridge.qml", "Bridge.qml")):
        shutil.copy2(source / filename, target / installed)
    template = (source / "Service.qml.in").read_text()
    # A file URL lets Qt load the installed service outside Quickshell's config tree.
    # Relative imports outside that tree are redirected to qs-blackhole.
    imported = native.resolve().as_uri()
    (target / "Service.qml").write_text(template.replace('"@NATIVE_NOTIFICATIONS@"', json.dumps(imported)))
    return target


def check_existing(config):
    path = config / "omarchy/shell.json"
    settings = json.loads(path.read_text()) if path.exists() else {}
    enabled = {item if isinstance(item, str) else item.get("id") for item in settings.get("plugins", [])}
    enabled -= set(settings.get("disabledPlugins", []))
    for manifest in (config / "omarchy/plugins").glob("*/manifest.json"):
        data = json.loads(manifest.read_text())
        if data.get("id") in enabled - {PLUGIN_ID} and data.get("omarchy", {}).get("clonedFrom") == "omarchy.notifications":
            raise RuntimeError("An existing custom notification service is enabled: " + data["id"])


def wait_for_registry():
    for _ in range(40):
        result = subprocess.run(["omarchy", "plugin", "list", "--json"], check=True, capture_output=True, text=True)
        if any(item.get("id") == PLUGIN_ID for item in json.loads(result.stdout)):
            return
        time.sleep(.1)
    raise RuntimeError("Omarchy has not discovered the notification extension yet")


def main():
    config = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
    native = Path(os.environ.get("OMARCHY_PATH", "/usr/share/omarchy")) / "shell/plugins/notifications"
    check_existing(config)
    target = install_files(Path(__file__).resolve().parents[1] / "notifications", config, native)
    subprocess.run(["omarchy-shell", "shell", "rescanPlugins"], check=True)
    wait_for_registry()
    subprocess.run(["omarchy", "plugin", "enable", PLUGIN_ID], check=True)
    print("Installed", target)
    print("Desktop notifications keep Omarchy's native UI, actions, DND and history.")


if __name__ == "__main__":
    main()
