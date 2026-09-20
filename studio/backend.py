#!/usr/bin/env python3
"""Monitor lifecycle and JSON-lines IPC for the Omarchy shell panel."""
import argparse
import fcntl
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import uuid

from glasses import Recovery, detect
from sdk import SDK


def default_layout():
    return {"version": 1, "fps": 30, "curvature": 0, "spacing": 24, "monitors": [
        {"id": str(i + 1), "width": 1920, "height": 1080, "x": i * 1944, "y": 0}
        for i in range(3)]}


def add_gutters(layout):
    """Resolve the draft without shrinking screens; preserve order and nearby rows."""
    result = json.loads(json.dumps(layout))
    gap = result.setdefault("spacing", 24)
    if type(gap) is not int or not 1 <= gap <= 8192:
        raise ValueError("Spacing must be a whole number from 1 to 8192 pixels")
    # Validate scalar fields first, before doing geometry arithmetic.
    validate(result, check_gaps=False)
    placed = []
    for m in result["monitors"]:
        for _ in range(len(placed) * 2 + 1):
            collision = next((p for p in placed if
                m["x"] < p["x"] + p["width"] + gap and p["x"] < m["x"] + m["width"] + gap and
                m["y"] < p["y"] + p["height"] + gap and p["y"] < m["y"] + m["height"] + gap), None)
            if collision is None:
                break
            right = collision["x"] + collision["width"] + gap - m["x"]
            down = collision["y"] + collision["height"] + gap - m["y"]
            if right <= down:
                m["x"] += right
            else:
                m["y"] += down
        else:
            raise ValueError("Could not resolve this layout. Use Row or Grid")
        placed.append(m)
    return validate(result)


def validate(layout, check_gaps=True):
    if not isinstance(layout, dict) or layout.get("version") != 1:
        raise ValueError("Unsupported layout version")
    if type(layout.get("fps")) is not int or not 1 <= layout["fps"] <= 60:
        raise ValueError("Capture rate must be 1–60 fps")
    monitors = layout.get("monitors")
    if not isinstance(monitors, list) or not monitors:
        raise ValueError("Add at least one monitor")
    def curvature(value):
        if type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 100:
            raise ValueError("Curvature must be 0–100 percent")
    curvature(layout.get("curvature", 0))
    seen = set()
    for m in monitors:
        if not isinstance(m, dict) or (not isinstance(m.get("id"), str) or not re.fullmatch(r"[a-zA-Z0-9_-]{1,40}", m["id"])):
            raise ValueError("Invalid monitor identity")
        if m["id"] in seen:
            raise ValueError("Duplicate monitor identity")
        seen.add(m["id"])
        curvature(m.get("curvature", 0))
        for key in ("width", "height", "x", "y"):
            if type(m.get(key)) is not int:
                raise ValueError(f"{key} must be a whole number")
        if not 320 <= m["width"] <= 8192 or not 200 <= m["height"] <= 8192:
            raise ValueError("Resolution must be 320–8192 wide and 200–8192 high")
        if abs(m["x"]) > 100000 or abs(m["y"]) > 100000:
            raise ValueError("Positions must be within ±100,000 pixels")
    gap = layout.get("spacing", 24)
    if type(gap) is not int or not 1 <= gap <= 8192:
        raise ValueError("Spacing must be a whole number from 1 to 8192 pixels")
    if not check_gaps:
        return layout
    # A sweep avoids quadratic comparisons for normal rows of monitors.
    active = []
    for m in sorted(monitors, key=lambda item: item["x"]):
        active = [p for p in active if p["x"] + p["width"] + gap > m["x"]]
        if any(m["y"] < p["y"] + p["height"] + gap and p["y"] < m["y"] + m["height"] + gap for p in active):
            raise ValueError("Monitor gutter is too small. Apply to separate monitors or choose Row / Grid")
        active.append(m)
    return layout


def atomic_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(data, indent=2) + "\n")
    temp.replace(path)


def run_hypr(*args):
    result = subprocess.run(["hyprctl", *args], capture_output=True, text=True, timeout=15)
    text = result.stdout.strip()
    if result.returncode or text.lower().startswith(("error", "invalid", "unknown")):
        raise RuntimeError(text or result.stderr.strip() or "Hyprland command failed")
    return text


class Manager:
    def __init__(self, directory, renderer, runner=run_hypr):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.lock = (self.directory / "manager.lock").open("w")
        fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        self.runner, self.renderer = runner, str(renderer)
        self.profile = self.directory / "layout.json"
        self.journal = self.directory / "outputs.json"
        self.owned = set()
        self.prefix = "OMXR-" + uuid.uuid4().hex[:8] + "-"
        self.applied = None
        self.recovery = Recovery()
        self.sdk = SDK(self.directory)
        self.viewer = None
        self.log = None
        if self.journal.exists():
            names = json.loads(self.journal.read_text())
            if not isinstance(names, list) or not all(isinstance(n, str) and re.fullmatch(r"OMXR-[0-9a-f]{8}-[a-zA-Z0-9_-]{1,40}", n) for n in names):
                raise ValueError("Invalid output recovery journal")
            self.owned = set(names)
            self.cleanup()

    def monitors(self):
        return json.loads(self.runner("-j", "monitors"))

    def record(self):
        atomic_json(self.journal, sorted(self.owned))

    def load(self):
        return add_gutters(json.loads(self.profile.read_text())) if self.profile.exists() else default_layout()

    def save(self, layout):
        layout = add_gutters(layout)
        atomic_json(self.profile, layout)

    def stop_viewer(self):
        if self.viewer:
            if self.viewer.poll() is None:
                self.viewer.terminate()
                try:
                    self.viewer.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.viewer.kill()
                    self.viewer.wait()
            self.viewer = None
        if self.log:
            self.log.close()
            self.log = None

    def remove(self, names):
        existing = {m["name"] for m in self.monitors()}
        failures = []
        for name in names:
            try:
                if name in existing:
                    self.runner("output", "remove", name)
                self.owned.discard(name)
                self.record()
            except Exception as exc:
                failures.append(str(exc))
        if failures:
            raise RuntimeError("Some outputs could not be removed: " + "; ".join(failures))

    def cleanup(self):
        self.stop_viewer()
        self.remove(list(self.owned))
        self.applied = None

    def persist_applied(self, layout):
        self.applied = json.loads(json.dumps(layout))
        self.save(layout)
        content = "".join(f'{self.prefix}{m["id"]}\t{m["x"]}\t{m["y"]}\t{m["width"]}\t{m["height"]}\t{m.get("curvature", 0)}\n' for m in layout["monitors"])
        target = self.directory / "viewer.tsv"
        temp = target.with_suffix(".tmp")
        temp.write_text(content)
        temp.replace(target)

    def apply(self, layout):
        layout = add_gutters(layout)
        was_viewing = self.viewer is not None and self.viewer.poll() is None
        self.stop_viewer()
        geometry = lambda config: [{key: m[key] for key in ("id", "width", "height", "x", "y")} for m in config["monitors"]]
        if self.applied and geometry(self.applied) == geometry(layout):
            present = {m["name"] for m in self.monitors()}
            if all(self.prefix + m["id"] in present for m in layout["monitors"]):
                # Presentation settings never reconfigure the compositor's outputs.
                self.persist_applied(layout)
                if was_viewing:
                    self.start()
                return
        new = []
        existing = self.monitors()
        other = [m for m in existing if m["name"] not in self.owned]
        if not other:
            raise RuntimeError("Keep at least one existing display for the editor and viewer")
        base_x = math.ceil(max(m["x"] + m["width"] / m.get("scale", 1) for m in other)) + 100
        min_x = min(m["x"] for m in layout["monitors"])
        min_y = min(m["y"] for m in layout["monitors"])
        desired = {self.prefix + m["id"] for m in layout["monitors"]}
        existing_names = {m["name"] for m in existing}
        try:
            for m in layout["monitors"]:
                name = self.prefix + m["id"]
                if name in existing_names and name not in self.owned:
                    raise RuntimeError("Output name collision: " + name)
                if name not in existing_names:
                    # Journal intent first so a crash during creation can be recovered.
                    self.owned.add(name)
                    self.record()
                    new.append(name)
                    self.runner("output", "create", "headless", name)
                x, y = base_x + m["x"] - min_x, m["y"] - min_y
                self.runner("eval", f'hl.monitor({{output="{name}", mode="{m["width"]}x{m["height"]}@60", position="{x}x{y}", scale=1}})')
            # Configuration application may be asynchronous; verify actual geometry.
            for _ in range(30):
                actual = {m["name"]: m for m in self.monitors()}
                if all((actual.get(self.prefix + m["id"], {}).get("width"), actual.get(self.prefix + m["id"], {}).get("height")) == (m["width"], m["height"]) for m in layout["monitors"]):
                    break
                time.sleep(.1)
            else:
                raise RuntimeError("Hyprland did not apply the requested resolutions")
            self.remove(self.owned - desired)
            self.persist_applied(layout)
        except Exception:
            self.remove(new)
            # Existing monitors can have changed; avoid claiming the old layout is active.
            self.applied = None
            raise

    def start(self):
        if not self.applied:
            raise RuntimeError("Apply your layout first")
        if not Path(self.renderer).is_file():
            raise RuntimeError("Renderer not installed. Run make install-studio")
        self.stop_viewer()
        self.log = (self.directory / "viewer.log").open("w")
        self.viewer = subprocess.Popen([self.renderer, "--layout", str(self.directory / "viewer.tsv"), "--fps", str(self.applied["fps"]), "--workspace-curvature", str(self.applied.get("curvature", 0)), "--spacing", str(self.applied["spacing"])], stdout=self.log, stderr=self.log)
        time.sleep(.25)
        if self.viewer.poll() is not None:
            raise RuntimeError("Viewer could not start. See " + str(self.directory / "viewer.log"))

    def terminal(self, identity):
        if not self.applied or identity not in [m["id"] for m in self.applied["monitors"]]:
            raise RuntimeError("Apply this monitor before opening an application")
        name = self.prefix + identity
        monitor = next((m for m in self.monitors() if m["name"] == name), None)
        if not monitor:
            raise RuntimeError("Monitor is disconnected")
        workspace = int(monitor["activeWorkspace"]["id"])
        self.runner("eval", f'hl.exec_cmd("foot", {{workspace="{workspace} silent"}})')

    def status(self):
        try:
            glasses = detect(self.monitors())
        except Exception:
            glasses = detect([])
            glasses["detectionError"] = "Display status unavailable"
        glasses.update(self.recovery.status())
        glasses["sdk"] = self.sdk.status()
        return {"active": len(self.owned), "viewing": self.viewer is not None and self.viewer.poll() is None,
                "glasses": glasses}


def serve(manager):
    for line in sys.stdin:
        try:
            request = json.loads(line)
            action = request["action"]
            if action == "load":
                response = {"layout": manager.load()}
            elif action == "save":
                manager.save(request["layout"]); response = {"layout": manager.load(), "message": "Layout saved"}
            elif action == "apply":
                manager.apply(request["layout"]); response = {"layout": manager.applied, "message": "Virtual monitors ready. Open a terminal on a selected monitor to get started."}
            elif action == "start":
                manager.start(); response = {"message": "Live viewer opened"}
            elif action == "stop":
                manager.cleanup(); response = {"message": "Viewer stopped and virtual monitors removed"}
            elif action == "terminal":
                manager.terminal(request["id"]); response = {"message": "Terminal opened on selected monitor"}
            elif action == "reinitialize":
                manager.sdk.disconnect(); manager.recovery.start(); response = {}
            elif action == "sdk_connect":
                if manager.recovery.status()["recovering"]:
                    raise RuntimeError("Wait for USB-C recovery to finish")
                manager.sdk.connect(); response = {}
            elif action == "sdk_disconnect":
                manager.sdk.disconnect(); response = {"message": "SDK disconnected"}
            elif action == "sdk_restore":
                manager.sdk.restore(); response = {}
            elif action == "status":
                response = {}
            else:
                raise ValueError("Unknown action")
            print(json.dumps({"ok": True, **response, **manager.status()}), flush=True)
        except Exception as exc:
            print(json.dumps({"ok": False, "message": str(exc), **manager.status()}), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--renderer", required=True)
    args = parser.parse_args()
    directory = Path(os.environ.get("XDG_STATE_HOME", str(Path.home() / ".local/state"))) / "omarchy-xr"
    manager = None
    def stop(*_):
        raise SystemExit(0)
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        manager = Manager(directory, args.renderer)
        serve(manager)
    finally:
        if manager:
            manager.sdk.disconnect()
            manager.cleanup()


if __name__ == "__main__":
    main()
