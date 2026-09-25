#!/usr/bin/env python3
"""Monitor lifecycle and JSON-lines IPC for the Omarchy shell panel."""
import argparse
import ctypes
import fcntl
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading
import time
import traceback
import uuid
from typing import Any

from laptop_display import LaptopDisplay, internal
from workspace_presets import built_in_setups
from environment import Environment
from glasses import Recovery, detect
from sdk import SDK
from dedicated import Dedicated, HELPER
import socket
from input_settings import DEFAULTS, load_controls, save_controls
from graphics_limits import detect as detect_graphics_limits, validate_dimensions
from atomic_file import atomic_write
from clock import boot_time
from canvas import CanvasSession, DEFAULTS as CANVAS_DEFAULTS, CANVAS_WORKSPACE, PARK_WORKSPACE

CONTROLS_HINT = "XR controls need setup — open Utilities → Setup & integrations"
CAMERA_ACTIONS = ("recenter", "fit", "fit_target", "zoom_in", "zoom_out")
# Window canvas verbs (plan §5.8); the renderer reads each name as a pose-socket datagram.
CANVAS_ACTIONS = ("overview", "search", "fill", "arrange", "undo", "redo", "pin", "help")


def default_layout():
    return {"version": 1, "fps": 60, "curvature": 0, "spacing": 24, "monitors": [
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
    placed: list[dict[str, Any]] = []
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
            raise ValueError("The monitors overlap. Choose Arrange in row or Arrange in grid.")
        placed.append(m)
    return validate(result)


def effective_scale(monitor):
    # Omarchy monitor widget: align the requested scale to whole logical pixels.
    divisor = math.gcd(monitor["width"] * 120, monitor["height"] * 120)
    units = min(round(monitor.get("scale", 1) * 120), divisor)
    while divisor % units:
        units += 1
    return units / 120


def _curvature(value):
    if type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 100:
        raise ValueError("Curvature must be 0–100 percent")


def _layout_monitors(layout):
    if not isinstance(layout, dict) or layout.get("version") != 1:
        raise ValueError("Unsupported layout version")
    if type(layout.get("fps")) is not int or not 1 <= layout["fps"] <= 120:
        raise ValueError("Capture rate must be 1–120 fps")
    monitors = layout.get("monitors")
    if not isinstance(monitors, list) or not monitors:
        raise ValueError("Add at least one monitor")
    if len(monitors) > 16:
        raise ValueError("Use at most 16 monitors")
    _curvature(layout.get("curvature", 0))
    if type(layout.get("workspaceFollow", False)) is not bool:
        raise ValueError("Match monitor bend to workspace must be on or off.")
    degrees = layout.get("workspaceDegrees", -1)
    if type(degrees) not in (int, float) or not math.isfinite(degrees) or (degrees != -1 and not 0 <= degrees <= 360):
        raise ValueError("Workspace bend must be between 0° and 360°.")
    return monitors


def _validate_monitor(monitor, seen):
    if not isinstance(monitor, dict) or not isinstance(monitor.get("id"), str) or not re.fullmatch(r"[a-zA-Z0-9_-]{1,40}", monitor["id"]):
        raise ValueError("Invalid monitor identity")
    # Output names are prefix + id; "<prefix>canvas" is the window canvas output (studio/canvas.py).
    if monitor["id"] == "canvas" or monitor["id"].endswith("-canvas"):
        raise ValueError("The monitor identity \"canvas\" is reserved for the window canvas")
    if monitor["id"] in seen:
        raise ValueError("Duplicate monitor identity")
    seen.add(monitor["id"])
    _curvature(monitor.get("curvature", 0))
    scale = monitor.get("scale", 1)
    if type(scale) not in (int, float) or scale not in (1, 1.25, 1.6, 2, 3, 4):
        raise ValueError("Unsupported monitor scale")
    brightness = monitor.get("brightness", 100)
    if type(brightness) is not int or not 1 <= brightness <= 100:
        raise ValueError("Brightness must be 1–100 percent")
    for key in ("width", "height", "x", "y"):
        if type(monitor.get(key)) is not int:
            raise ValueError(f"{key} must be a whole number")
    if not 320 <= monitor["width"] <= 8192 or not 200 <= monitor["height"] <= 8192:
        raise ValueError("Monitor size must be 320–8192 pixels wide and 200–8192 pixels high.")
    if abs(monitor["x"]) > 100000 or abs(monitor["y"]) > 100000:
        raise ValueError("Positions must be within ±100,000 pixels")


def _validate_gaps(monitors, gap):
    # A sweep avoids quadratic comparisons for normal rows of monitors.
    active: list[dict[str, Any]] = []
    for monitor in sorted(monitors, key=lambda item: item["x"]):
        active = [peer for peer in active if peer["x"] + peer["width"] + gap > monitor["x"]]
        if any(monitor["y"] < peer["y"] + peer["height"] + gap and peer["y"] < monitor["y"] + monitor["height"] + gap for peer in active):
            raise ValueError("The monitors are too close together. Choose Arrange in row or Arrange in grid.")
        active.append(monitor)


def validate(layout, check_gaps=True):
    monitors = _layout_monitors(layout)
    seen: set[str] = set()
    for monitor in monitors:
        _validate_monitor(monitor, seen)
    gap = layout.get("spacing", 24)
    if type(gap) is not int or not 1 <= gap <= 8192:
        raise ValueError("Spacing must be a whole number from 1 to 8192 pixels")
    if check_gaps:
        _validate_gaps(monitors, gap)
    return layout


def atomic_json(path, data):
    atomic_write(path, json.dumps(data, indent=2) + "\n")


def runtime_dir():
    override = os.environ.get("OMARCHY_XR_RUNTIME")
    path = Path(override) if override else Path(os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}") / "omarchy-xr"
    path.mkdir(parents=True, exist_ok=True)
    return path


def runtime_installed(renderer, system=Path("/usr/bin/omarchy-xr")):
    """Accept the package runtime or a compiled renderer copied by source setup."""
    if system.is_file():
        return True
    try:
        with Path(renderer).open("rb") as executable:
            return executable.read(4) == b"\x7fELF"
    except OSError:
        return False


def die_with_parent():
    # PR_SET_PDEATHSIG: the viewer exits if this worker is killed.
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl(1, signal.SIGTERM, 0, 0, 0)


def quarantine(path):
    path = Path(path)
    if not path.exists():
        return
    dest = path.with_name(path.name + ".corrupt")
    if dest.exists():
        dest.unlink()
    path.replace(dest)


def run_hypr(*args):
    result = subprocess.run(["hyprctl", *args], capture_output=True, text=True, timeout=15)
    text = result.stdout.strip()
    if result.returncode or text.lower().startswith(("error", "invalid", "unknown")):
        raise RuntimeError(text or result.stderr.strip() or "The desktop could not complete that change. Try again.")
    return text


class Manager:
    def __init__(self, directory, renderer, runner=run_hypr, runtime=None):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.pose_socket = (Path(runtime) if runtime else self.directory) / "pose.sock"
        self.lock = (self.directory / "manager.lock").open("w")
        try:
            fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print(json.dumps({"ok": False, "message": "XR Monitor Studio is already open."}), flush=True)
            raise SystemExit(1)
        self.runner, self.renderer = runner, str(renderer)
        self.presentation_profile = self.directory / "presentation.json"
        try:
            presentation = json.loads(self.presentation_profile.read_text())
            self.spectator_enabled = presentation.get("spectator", False) is True
            self.laptop_off_enabled = presentation.get("laptopOff", False) is True
            self.render_mode = "canvas" if presentation.get("renderMode") == "canvas" else "monitors"
        except (OSError, ValueError, AttributeError):
            self.spectator_enabled = False
            self.laptop_off_enabled = False
            self.render_mode = "monitors"
        self.environment = Environment(self.directory)
        self.graphics_limits = None
        self.profile = self.directory / "layout.json"
        self.journal = self.directory / "outputs.json"
        self.owned = set()
        self.prefix = "OMXR-" + uuid.uuid4().hex[:8] + "-"
        self.applied: dict[str, Any] | None = None
        self.output_geometry = {}
        self.internal_workspaces = {}
        self.output_error = ""
        self.recovery = Recovery()
        self.sdk = SDK(self.directory, self.pose_socket)
        self.dedicated = Dedicated(self.directory, self.renderer)
        self.direct = False
        self.stereo_active = False
        self.original_output = None
        self.restoration_error = ""
        self.viewer = None
        self.viewer_exit = ""
        self.presenting = False
        self.log = None
        self.laptop = LaptopDisplay(self.directory, self.runner)
        try: self.laptop.recover()
        except Exception as exc: self.laptop.error = str(exc)
        self.canvas = CanvasSession(self)
        self.recover_journal()
        self.recover_stranded_stereo()

    def recover_journal(self):
        if self.journal.exists():
            try:
                names = json.loads(self.journal.read_text())
                if not isinstance(names, list) or not all(isinstance(n, str) and re.fullmatch(r"OMXR-[0-9a-f]{8}-[a-zA-Z0-9_-]{1,40}", n) for n in names):
                    raise ValueError("The previous XR session did not close cleanly. Studio will recover its virtual monitors.")
                self.owned = set(names)
            except (OSError, ValueError) as exc:
                self.adopt_visible_outputs(exc)
        # Windows leave a crashed canvas before its output is removed.
        self.canvas.recover()
        if self.owned:
            self.finish_recovered_outputs()

    def adopt_visible_outputs(self, exc):
        self.restoration_error = str(exc)
        quarantine(self.journal)
        self.owned = set()
        try:
            for monitor in self.monitors():
                name = monitor.get("name", "")
                if isinstance(name, str) and re.fullmatch(r"OMXR-[0-9a-f]{8}-[a-zA-Z0-9_-]{1,40}", name):
                    self.owned.add(name)
        except Exception as scan_exc:
            self.restoration_error += "; " + str(scan_exc)

    def finish_recovered_outputs(self):
        try:
            self.cleanup()
        except Exception as exc:
            self.restoration_error = "; ".join(filter(None, (self.restoration_error, str(exc))))

    def record_stereo(self):
        atomic_json(self.directory / "stereo.json", {"stereoActive": True, "originalOutput": self.original_output})

    def clear_stereo(self):
        (self.directory / "stereo.json").unlink(missing_ok=True)

    def recover_stranded_stereo(self):
        path = self.directory / "stereo.json"
        if not path.exists():
            return
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            quarantine(path)
            return
        if not isinstance(data, dict) or data.get("stereoActive") is not True:
            return
        self.stereo_active = True
        output = data.get("originalOutput")
        self.original_output = output if isinstance(output, dict) else None
        try:
            self.ensure_sdk()
            self.stop_viewer()
        except Exception as exc:
            if not self.restoration_error:
                self.restoration_error = str(exc)

    def ensure_sdk(self):
        if not self.sdk.process or self.sdk.process.poll() is not None:
            self.sdk.connect()

    def _watch_viewer(self, process):
        process.wait()
        code = process.returncode
        if self.viewer is process:
            self.append_backend_log(f"Viewer exited (code {code})")

    def monitors(self):
        return json.loads(self.runner("-j", "monitors"))

    @property
    def canvas_mode(self):
        return self.render_mode == "canvas"

    def record(self):
        atomic_json(self.journal, sorted(self.owned))

    def load(self):
        return add_gutters(json.loads(self.profile.read_text())) if self.profile.exists() else default_layout()

    def save(self, layout):
        layout = add_gutters(layout)
        atomic_json(self.profile, layout)

    def setups(self):
        path = self.directory / "setups.json"
        if not path.exists():
            return {"version": 1, "selected": "", "items": []}
        data = json.loads(path.read_text())
        if not isinstance(data, dict) or data.get("version") != 1 or not isinstance(data.get("items"), list):
            raise ValueError("Saved monitor setups could not be read.")
        seen = set()
        for item in data["items"]:
            if not isinstance(item, dict) or not isinstance(item.get("id"), str) or item["id"] in seen:
                raise ValueError("A saved monitor setup is damaged and could not be opened.")
            seen.add(item["id"])
            self.setup_name(item.get("name"))
            validate(item.get("layout"))
        return data

    @staticmethod
    def setup_name(name):
        if not isinstance(name, str) or not name.strip() or len(name.strip()) > 80 or any(ord(c) < 32 for c in name):
            raise ValueError("Use a setup name of 1–80 characters")
        return name.strip()

    def save_setup(self, name, layout, identity=None):
        name = self.setup_name(name)
        layout = add_gutters(layout)
        data = self.setups()
        item = next((s for s in data["items"] if s["id"] == identity), None)
        if identity and item is None:
            raise ValueError("That saved setup no longer exists. Choose another setup.")
        if any(s["name"].casefold() == name.casefold() and s is not item for s in data["items"]):
            raise ValueError("That name is already used. Select it and choose Update selected, or use a new name")
        if item is None:
            item = {"id": uuid.uuid4().hex}
            data["items"].append(item)
        item.update(name=name, layout=layout)
        data["selected"] = item["id"]
        atomic_json(self.directory / "setups.json", data)
        return name

    def use_setup(self, identity):
        data = self.setups()
        item = next((s for s in built_in_setups() + data["items"] if s["id"] == identity), None)
        if item is None:
            raise ValueError("That setup no longer exists. Choose another setup.")
        layout = add_gutters(item["layout"])
        validate_dimensions(layout, self.hardware_limits())
        # Reuse live layout application, preserving the renderer and display lease.
        # With no virtual outputs yet, load the draft without starting hardware.
        if self.applied:
            self.apply(layout)
        else:
            self.save(layout)
        data["selected"] = identity
        atomic_json(self.directory / "setups.json", data)
        return {"layout": layout, "layoutDirty": False, "message": "Using setup: " + item["name"]}

    def halt_viewer(self, failures):
        try:
            self.laptop.stop()
        except Exception as exc:
            failures.append("Laptop display: " + str(exc))
        self.terminate_viewer()
        if self.log:
            self.log.close()
            self.log = None

    def terminate_viewer(self):
        viewer = self.viewer
        if viewer is None:
            return
        if viewer.poll() is None:
            self.force_viewer_exit(viewer)
        self.viewer = None

    def force_viewer_exit(self, viewer):
        viewer.terminate()
        try:
            viewer.wait(timeout=3)
        except subprocess.TimeoutExpired:
            viewer.kill()
            try:
                viewer.wait(timeout=2)
            except subprocess.TimeoutExpired:
                pass

    def release_glasses(self, failures):
        released = self.leave_side_by_side(failures)
        try:
            self.dedicated.stop()
        except Exception as exc:
            failures.append(str(exc))
        return released

    def leave_side_by_side(self, failures):
        if not self.stereo_active:
            return False
        try:
            self.sdk.stereo(False)  # request old EDID family before re-detection
        except Exception as exc:
            failures.append(str(exc))
            return False
        return True

    def finish_stereo_restore(self, failures, released):
        if self.stereo_active and not released:
            failures.append("The glasses were not asked to leave side-by-side mode")
            return
        if not self.stereo_active:
            return
        try:
            self.restore_saved_output()
            self.sdk.verify_restore()
            self.stereo_active = False
            self.original_output = None
            self.clear_stereo()
        except Exception as exc:
            failures.append(str(exc))

    def stop_viewer(self, *, release_outputs=True):
        failures: list[str] = []
        self.halt_viewer(failures)
        released = self.release_glasses(failures)
        self.finish_stereo_restore(failures, released)
        self.direct = False
        self.presenting = False
        if release_outputs:
            try:
                self.canvas.remove()
                self.remove(set(self.owned))
                self.applied = None
                self.output_geometry = {}
                self.internal_workspaces = {}
            except Exception as exc:
                failures.append("Workspace release: " + str(exc))
        self.restoration_error = "; ".join(failures)
        if failures:
            raise RuntimeError("XR could not close cleanly. Choose Stop & remove monitors again. " + self.restoration_error)

    def wait_for_output(self, original, family):
        name = original["name"]
        for _ in range(150):
            monitors = self.monitors()
            present = [monitor for monitor in monitors if monitor["name"] in set(detect(monitors)["displays"])]
            actual = next((monitor for monitor in present if monitor["name"] == name), None)
            if actual is None and len(present) == 1:
                actual = present[0]
                name = actual["name"]
                original["name"] = name
                self.original_output = original
            if actual and any(str(mode).startswith(family) for mode in actual.get("availableModes", [])):
                return name
            time.sleep(.1)
        raise RuntimeError("The glasses did not return to normal video. Unplug and reconnect them, then try again.")

    def wait_for_refresh(self, name, original):
        wanted = f'{original["width"]}x{original["height"]}@'
        for _ in range(150):
            restored: dict[str, Any] = next((monitor for monitor in self.monitors() if monitor["name"] == name), {})
            modes = restored.get("availableModes", [])
            if any(mode.startswith(wanted) and abs(float(mode.split("@")[1].removesuffix("Hz")) - original["refreshRate"]) < 1 for mode in modes):
                return
            time.sleep(.1)
        raise RuntimeError("The glasses did not restore their previous display settings. Unplug and reconnect them.")

    def restore_saved_output(self):
        if not self.original_output:
            return
        original = dict(self.original_output)
        name = self.wait_for_output(original, f'{original["width"]}x{original["height"]}@')
        self.sdk.restore_rate()
        # Refresh-rate changes trigger a second link negotiation.
        self.wait_for_refresh(name, original)
        mode = f'{original["width"]}x{original["height"]}@{original["refreshRate"]}'
        position = f'{original["x"]}x{original["y"]}'
        self.runner("eval", "hl.monitor({output=" + json.dumps(name) + ", mode=" + json.dumps(mode) + ", position=" + json.dumps(position) + ", scale=" + str(original["scale"]) + "})")

    def relocate_workspaces(self, names, removing, monitors):
        glasses = set(detect(monitors)["displays"])
        # Never a hidden workspace on the window canvas output.
        targets = [m for m in monitors if m["name"] not in names and not m["name"].endswith("-canvas")
                   and not m.get("disabled", False) and m.get("dpmsStatus", True)
                   and m.get("width", 0) > 0]
        # During layout edits keep workspaces in XR. On exit prefer the
        # laptop/desktop over the glasses' restored 2D output.
        laptop_names = {m["name"] for m in internal(monitors)}
        targets.sort(key=lambda m: (m["name"] not in self.owned,
                                    m["name"] in glasses,
                                    m["name"] not in laptop_names))
        if not targets:
            raise RuntimeError("There is no computer display available for your XR windows. Turn on a display, then choose Stop again.")
        target = targets[0]["name"]
        workspaces = json.loads(self.runner("-j", "workspaces"))
        for workspace in workspaces:
            if workspace.get("monitor") not in removing:
                continue
            # Empty canvas workspaces go with their output rather than showing on the laptop.
            if workspace.get("name") in (CANVAS_WORKSPACE, PARK_WORKSPACE) and not workspace.get("windows", 0):
                continue
            # Numeric IDs also preserve named and special workspace identity.
            identity = int(workspace["id"])
            self.runner("eval", 'hl.dispatch(hl.dsp.workspace.move({workspace='
                        + str(identity) + ', monitor=' + json.dumps(target) + '}))')
        remaining = json.loads(self.runner("-j", "workspaces"))
        stranded = [w for w in remaining if w.get("monitor") in removing and w.get("windows", 0) > 0]
        if stranded:
            raise RuntimeError("Some XR windows could not return to your computer display. Choose Stop again.")

    def remove(self, names):
        names = set(names)
        if not names:
            return
        monitors = self.monitors()
        existing = {m["name"] for m in monitors}
        removing = names & existing
        if removing:
            self.relocate_workspaces(names, removing, monitors)
        failures = []
        for name in sorted(names):
            try:
                if name in existing:
                    self.runner("output", "remove", name)
                self.owned.discard(name)
                self.record()
            except Exception as exc:
                failures.append(str(exc))
        if failures:
            raise RuntimeError("Some virtual monitors could not be removed. Choose Stop again. " + "; ".join(failures))

    def cleanup(self):
        self.stop_viewer()

    def persist_applied(self, layout):
        self.applied = json.loads(json.dumps(layout))
        self.save(layout)
        content = f'# settings {layout["fps"]} {layout.get("curvature", 0)} {layout["spacing"]} {layout.get("workspaceDegrees", -1)} {int(layout.get("workspaceFollow", False))}\n' + "".join(f'{self.prefix}{m["id"]}\t{m["x"]}\t{m["y"]}\t{m["width"]}\t{m["height"]}\t{m.get("curvature", 0)}\t{m.get("brightness", 100)}\n' for m in layout["monitors"])
        atomic_write(self.directory / "viewer.tsv", content)

    def hardware_limits(self):
        if self.graphics_limits is None:
            self.graphics_limits = detect_graphics_limits(self.renderer)
        return self.graphics_limits

    @staticmethod
    def output_rect(m):
        w, h = m["width"], m["height"]
        if m.get("transform", 0) % 2:
            w, h = h, w
        return (m["x"], m["y"], m["x"] + math.ceil(w / m.get("scale", 1)),
                m["y"] + math.ceil(h / m.get("scale", 1)))

    def stage_conflicting_outputs(self, existing, targets):
        """Vacate old rectangles before growing, swapping or replacing outputs."""
        def overlaps(a, b):
            return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]
        if not any(t["name"] != old["name"] and overlaps(self.output_rect(t), self.output_rect(old))
                   for t in targets for old in existing if old["name"] in self.owned):
            return existing
        # Beyond both old and final layouts, with separate slots large enough for
        # either mode. Keep the outputs alive so windows/capture retain identity.
        cursor = max(self.output_rect(m)[2] for m in existing + targets) + 100
        positions = {}
        for old in existing:
            if old["name"] not in self.owned: continue
            name = old["name"]
            positions[name] = (cursor, old["y"])
            mode = f'{old["width"]}x{old["height"]}@{old.get("refreshRate", 60)}'
            self.runner("eval", 'hl.monitor({output='+json.dumps(name)+', mode='+json.dumps(mode)
                        +f', position="{cursor}x{old["y"]}", scale={old.get("scale", 1)}}})')
            target = next((m for m in targets if m["name"] == name), old)
            cursor += max(self.output_rect(old)[2]-old["x"], self.output_rect(target)[2]-target["x"]) + 100
        for _ in range(30):
            updated = self.monitors()
            actual = {m["name"]: m for m in updated}
            if all((actual.get(name, {}).get("x"), actual.get(name, {}).get("y")) == pos
                   for name, pos in positions.items()):
                return updated
            time.sleep(.1)
        raise RuntimeError("The desktop could not prepare the new monitor layout. Try Apply again.")

    def reject_live_resize(self, layout):
        if not (self.applied and self.viewer and self.viewer.poll() is None and not self.direct):
            return
        previous_sizes = {monitor["id"]: (monitor["width"], monitor["height"]) for monitor in self.applied["monitors"]}
        if any(monitor["id"] in previous_sizes and previous_sizes[monitor["id"]] != (monitor["width"], monitor["height"]) for monitor in layout["monitors"]):
            raise RuntimeError("Close the preview before changing monitor sizes. You can resize monitors while stereo is active.")

    def presentation_unchanged(self, layout, rate):
        def geometry(config):
            return [{key: monitor[key] for key in ("id", "width", "height", "x", "y")} | {"scale": monitor.get("scale", 1)} for monitor in config["monitors"]]
        if not self.applied or geometry(self.applied) != geometry(layout) or max(60, self.applied["fps"]) != rate:
            return False
        actual = {monitor["name"]: monitor for monitor in self.monitors()}
        return all(self.output_matches(actual.get(self.prefix + monitor["id"], {}), monitor, rate) for monitor in layout["monitors"])

    def desktop_origin(self, existing):
        other = [monitor for monitor in existing if monitor["name"] not in self.owned]
        # Preserve the desktop origin while its built-in panel is temporarily off.
        other += [monitor for monitor in self.laptop.saved() if monitor["name"] not in {item["name"] for item in other}]
        if other:
            return max(self.output_rect(monitor)[2] for monitor in other) + 100
        leftover = [monitor for monitor in existing if monitor["name"] in self.owned]
        if not leftover:
            raise RuntimeError("Keep at least one computer display active while setting up XR.")
        # Keep the current virtual-desktop origin when no physical leftover remains.
        return min(monitor["x"] for monitor in leftover)

    def apply(self, layout):
        layout = add_gutters(layout)
        validate_dimensions(layout, self.hardware_limits())
        self.reject_live_resize(layout)
        rate = max(60, layout["fps"])
        if self.presentation_unchanged(layout, rate):
            # Presentation settings never reconfigure the compositor's outputs.
            self.persist_applied(layout)
            return
        existing = self.monitors()
        self.place_monitors(layout, rate, self.desktop_origin(existing), existing)

    def place_monitors(self, layout, rate, base_x, existing):
        new = []
        min_x = min(monitor["x"] for monitor in layout["monitors"])
        min_y = min(monitor["y"] for monitor in layout["monitors"])
        desired = {self.prefix + monitor["id"] for monitor in layout["monitors"]}
        existing_names = {monitor["name"] for monitor in existing}
        try:
            targets = [{**m, "name":self.prefix+m["id"], "scale":effective_scale(m),
                        "x":base_x+m["x"]-min_x, "y":m["y"]-min_y} for m in layout["monitors"]]
            existing = self.stage_conflicting_outputs(existing, targets)
            for m in layout["monitors"]:
                name = self.prefix + m["id"]
                scale = effective_scale(m)
                if name in existing_names and name not in self.owned:
                    raise RuntimeError("A virtual monitor name is already in use. Stop XR, then try again.")
                x, y = base_x + m["x"] - min_x, m["y"] - min_y
                if name not in existing_names:
                    # Install explicit scale/resolution before advertising a new output.
                    self.runner("eval", f'hl.monitor({{output="{name}", mode="{m["width"]}x{m["height"]}@{rate}", position="{x}x{y}", scale={scale}}})')
                    # Journal intent first so a crash during creation can be recovered.
                    self.owned.add(name)
                    self.record()
                    new.append(name)
                    self.runner("output", "create", "headless", name)
                x, y = base_x + m["x"] - min_x, m["y"] - min_y
                previous: dict[str, Any] = next((o for o in existing if o["name"] == name), {})
                if any(previous.get(k) != v for k, v in {"width":m["width"], "height":m["height"], "x":x, "y":y, "scale":scale}.items()) or abs(previous.get("refreshRate",rate)-rate) > 1:
                    self.runner("eval", f'hl.monitor({{output="{name}", mode="{m["width"]}x{m["height"]}@{rate}", position="{x}x{y}", scale={scale}}})')
            # Configuration application may be asynchronous; verify actual geometry.
            for _ in range(30):
                actual = {m["name"]: m for m in self.monitors()}
                if all(self.output_matches(actual.get(m["name"], {}), m, rate)
                       and all(actual[m["name"]].get(k) == m[k] for k in ("x", "y")) for m in targets):
                    break
                time.sleep(.1)
            else:
                raise RuntimeError("The desktop could not apply the monitor size or position. Try Apply again.")
            self.remove(self.owned - desired)
            self.output_geometry = {name:{k:actual[name][k] for k in ("x","y")} for name in desired}
            self.persist_applied(layout)
        except Exception:
            self.remove(new)
            # Existing monitors can have changed; avoid claiming the old layout is active.
            self.applied = None
            raise
    @staticmethod
    def output_matches(actual, monitor, rate):
        return (actual.get("width") == monitor["width"] and actual.get("height") == monitor["height"]
                and abs(actual.get("scale", 0)-effective_scale(monitor)) < .001
                and abs(actual.get("refreshRate", rate)-rate) < 1
                and not actual.get("disabled", False))

    def reconcile_outputs(self, monitors):
        """Repair runtime rules lost on compositor reload without restarting capture."""
        if self.canvas.active: self.canvas.reconcile(monitors)
        if not self.applied: return
        actual = {m["name"]:m for m in monitors}
        rate = max(60, self.applied["fps"])
        changed = False
        for m in self.applied["monitors"]:
            name = self.prefix+m["id"]
            if name not in actual: raise RuntimeError("A virtual monitor disconnected. Choose Apply to restore it.")
            position = self.output_geometry.get(name, actual[name])
            if (self.output_matches(actual[name], m, rate)
                    and all(actual[name].get(k)==position[k] for k in ("x","y"))): continue
            changed = True
            self.runner("eval", f'hl.monitor({{output="{name}", mode="{m["width"]}x{m["height"]}@{rate}", position="{position["x"]}x{position["y"]}", scale={effective_scale(m)}}})')
        # Verify even when only an external reload, rather than Apply, caused drift.
        if not changed: return
        verified = {m["name"]:m for m in self.monitors()}
        if not all(self.output_matches(verified.get(self.prefix+m["id"],{}),m,rate) for m in self.applied["monitors"]):
            raise RuntimeError("A virtual monitor is still being restored. Wait a moment, then try again.")

    def redistribute_laptop_windows(self, workspace_ids, monitors):
        if self.canvas.active:
            if workspace_ids:
                # Journaled like the start migration, so Stop returns them to their workspaces.
                clients = json.loads(self.runner("-j", "clients"))
                self.canvas.adopt([c for c in clients if c.get("workspace", {}).get("id") in workspace_ids])
            return
        targets = [m["activeWorkspace"]["id"] for m in monitors if m["name"] in self.owned
                   and m.get("activeWorkspace",{}).get("id",0)>0]
        if not workspace_ids: return
        if not targets: raise RuntimeError("There is no active XR monitor for the windows from your laptop display.")
        clients = json.loads(self.runner("-j", "clients"))
        index = 0
        for client in clients:
            if client.get("workspace",{}).get("id") not in workspace_ids: continue
            address = client.get("address", "")
            if not re.fullmatch(r"0x[0-9a-fA-F]+", address): continue
            target = targets[index % len(targets)]; index += 1
            self.runner("eval", f'hl.dispatch(hl.dsp.window.move({{workspace="{target}", follow=false, window="address:{address}"}}))')

    def reconcile_laptop_workspaces(self, monitors, disabling=False):
        active = {m["name"] for m in internal(monitors)}
        workspaces = json.loads(self.runner("-j", "workspaces"))
        current = {name:{w["id"] for w in workspaces if w.get("monitor")==name and w["id"]>0} for name in active}
        lost = set().union(*(ids for name,ids in self.internal_workspaces.items() if name not in active)) if self.internal_workspaces else set()
        if disabling: lost.update(set().union(*current.values()) if current else set())
        self.redistribute_laptop_windows(lost, monitors)
        self.internal_workspaces = {} if disabling else current

    def _monitor_args(self):
        applied = self.applied
        if not isinstance(applied, dict):
            raise RuntimeError("Apply your layout first")
        args = [self.renderer, "--layout", str(self.directory / "viewer.tsv"), "--fps", str(applied["fps"]),
                "--workspace-curvature", str(applied.get("curvature", 0)), "--spacing", str(applied["spacing"]),
                "--pose-socket", str(self.pose_socket)]
        if applied.get("workspaceFollow", False):
            args += ["--workspace-follow"]
        if applied.get("workspaceDegrees", -1) >= 0:
            args += ["--workspace-degrees", str(applied["workspaceDegrees"])]
        return args

    def _canvas_args(self):
        # Never needs applied monitors: the window canvas has its own output.
        if not self.canvas.active:
            raise RuntimeError("Start the window canvas first")
        return [self.renderer, "--canvas", str(self.canvas.tsv), "--fps", str(self.canvas.settings()["fps"]),
                "--pose-socket", str(self.pose_socket)]

    def viewer_command(self, present, direct):
        args = self._canvas_args() if self.canvas_mode else self._monitor_args()
        if direct:
            return args + self.direct_arguments()
        if present:
            displays = detect(self.monitors())["displays"]
            if len(displays) != 1:
                raise RuntimeError("Connect one pair of VITURE glasses before opening the preview.")
            args += ["--display", displays[0]]
        return args

    def direct_arguments(self):
        headset = self.dedicated.output
        if not isinstance(headset, str):
            raise RuntimeError("The glasses are not ready for stereo. Stop XR, then try again.")
        args = ["--direct", headset, "--stereo"]
        if self.spectator_enabled:
            self.place_spectator()
            args += ["--spectator"]
        return args

    def start(self, present=False, direct=False):
        if not (self.applied or self.canvas.active):
            raise RuntimeError("Apply your layout first")
        if not Path(self.renderer).is_file():
            raise RuntimeError("XR runtime not installed. Open Setup & integrations and choose Install XR runtime")
        args = self.viewer_command(present, direct)
        if not direct:
            self.stop_viewer(release_outputs=False)
        self.direct = direct
        self.presenting = present
        self.rotate_viewer_log()
        env = os.environ.copy()
        # Temporary mirror for a Lua adapter that has not been reinstalled yet.
        env["OMARCHY_XR_MIRROR_STATE"] = str(self.directory / "pose.sock.controls")
        self.viewer = subprocess.Popen(args, stdout=self.log, stderr=self.log, env=env, preexec_fn=die_with_parent)
        threading.Thread(target=self._watch_viewer, args=(self.viewer,), daemon=True).start()
        time.sleep(.25)
        if self.viewer.poll() is not None:
            raise RuntimeError("The XR view could not start. Try again; if it keeps failing, reopen Studio.")

    def display_event(self, stage, error=None, output=None):
        # Append rather than overwrite so a later recovery attempt keeps the cause.
        event = {"time": boot_time(), "stage": stage}
        if error is not None:
            event["error"] = str(error)
        if not isinstance(output, str):
            output = getattr(self.dedicated, "output", None)
        if isinstance(output, str) and output:
            event["output"] = output
        path = self.directory / "display-events.jsonl"
        try:
            if path.exists() and path.stat().st_size > 256 * 1024:
                previous = self.directory / "display-events.jsonl.1"
                if previous.exists():
                    previous.unlink()
                path.replace(previous)
            with path.open("a", encoding="utf-8") as log:
                log.write(json.dumps(event) + "\n")
        except OSError:
            pass

    def start_dedicated(self):
        saved_output = self.original_output if self.stereo_active else None
        self.ensure_sdk()
        try:
            # Keep the applied monitors; this is a handoff into the stereo viewer.
            self.stop_viewer(release_outputs=False)
        except Exception as exc:
            # A stranded side-by-side mode must not block the next session once a display is back.
            self.display_event("stereo-restore-skipped", str(exc))
        displays = detect(self.monitors())["displays"]
        if len(displays) != 1:
            detail = self.restoration_error or "Connect one pair of VITURE glasses, then try again."
            raise RuntimeError(detail)
        current = next(m for m in self.monitors() if m["name"] == displays[0])
        family = f'{saved_output["width"]}x{saved_output["height"]}@' if isinstance(saved_output, dict) else ""
        if family and not any(str(mode).startswith(family) for mode in current.get("availableModes", [])):
            self.original_output = saved_output
        else:
            self.original_output = current
        if not self.sdk.process or self.sdk.process.poll() is not None:
            self.sdk.connect()
        self.run_stereo(displays)

    def run_stereo(self, displays):
        stage = "switching the glasses to stereo"
        try:
            self.display_event("stereo-start")
            self.stereo_active = True  # restore even if mode setting partially fails
            self.record_stereo()
            self.sdk.stereo(True)
            self.display_event("stereo-request-acknowledged")
            stage = "waiting for stereo video"
            # Wait for the device's new EDID before taking a snapshot for the handoff.
            for _ in range(100):
                monitors = self.monitors()
                target: dict[str, Any] = next((m for m in monitors if m["name"] == displays[0]), {})
                if any(mode.startswith("3840x1080") for mode in target.get("availableModes", [])):
                    break
                time.sleep(.1)
            else:
                raise RuntimeError("The glasses did not switch to stereo video. Reconnect them, then try again.")
            stage = "preparing the glasses display"
            self.display_event("stereo-edid-ready")
            self.dedicated.start(displays[0])
            stage = "opening the XR view"
            self.start(present=True, direct=True)
            stage = "checking the stereo image"
            self.sdk.verify_stereo()
            self.display_event("stereo-running")
            if self.laptop_off_enabled:
                stage = "turning off the laptop display"
                self.disable_laptop_display()
        except Exception as exc:
            message = f"Could not start stereo while {stage}. {exc}"
            self.display_event("stereo-start-failed", message)
            try:
                self.stop_viewer()
            except Exception as recovery:
                self.display_event("stereo-rollback-failed", recovery)
                raise RuntimeError(message + " Studio also could not restore the displays: " + str(recovery)) from exc
            raise RuntimeError(message) from exc

    def place_spectator(self):
        monitors = self.monitors()
        glasses = detect(monitors)["displays"]
        candidates = [m for m in monitors if not m["name"].startswith("OMXR-")
                      and m["name"] not in glasses and m["name"] != self.dedicated.output
                      and not m.get("disabled", False)]
        candidates.sort(key=lambda m: not m["name"].startswith("eDP"))
        if not candidates:
            raise RuntimeError("Connect or turn on a computer display before opening the recording window.")
        workspace = int(candidates[0]["activeWorkspace"]["id"])
        self.runner("eval", 'if omarchy_xr_spectator_rule then omarchy_xr_spectator_rule:set_enabled(false) end; '
                    'omarchy_xr_spectator_rule = hl.window_rule({name="omarchy-xr-spectator", '
                    'match={class="^omarchy-xr-spectator$"}, float=false, '
                    f'workspace="{workspace} silent"' + '})')

    def set_spectator(self, enabled):
        if type(enabled) is not bool:
            raise ValueError("Mono window setting must be on or off")
        if enabled:
            self.place_spectator()
        if self.direct and self.viewer and self.viewer.poll() is None:
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as connection:
                connection.sendto(b"spectator_on" if enabled else b"spectator_off", str(self.pose_socket))
        self.spectator_enabled = enabled
        self.save_presentation()

    def save_presentation(self):
        atomic_json(self.presentation_profile, {"spectator":self.spectator_enabled,"laptopOff":self.laptop_off_enabled,
                                                "renderMode":self.render_mode})

    def set_render_mode(self, mode):
        if mode not in ("monitors", "canvas"):
            raise ValueError("Choose Virtual monitors or Window canvas")
        if self.viewer and self.viewer.poll() is None:
            raise RuntimeError("Stop XR before switching the render mode.")
        if mode == "canvas" and self.controls_version() < 6:
            raise RuntimeError(CONTROLS_HINT)
        # Outputs of the other mode (Apply without a preview) never outlive the switch.
        if mode != self.render_mode and self.owned:
            self.stop_viewer()
        self.render_mode = mode
        self.save_presentation()

    def disable_laptop_display(self):
        if not self.direct or not self.stereo_active or not self.viewer or self.viewer.poll() is not None:
            raise RuntimeError("Start stereo before turning off the laptop display.")
        # A live process alone does not establish that the glasses are displaying frames.
        for _ in range(80):
            if self.viewer.poll() is not None: break
            try:
                stats=json.loads(Path(str(self.pose_socket)+".stats").read_text())
                if stats.get("pid")==self.viewer.pid and stats.get("fps",0)>0 and 0<=time.monotonic()-stats["time"]<8:
                    if self.applied or self.canvas.active:
                        monitors = self.monitors()
                        self.reconcile_outputs(monitors)
                        self.reconcile_laptop_workspaces(monitors, disabling=True)
                    self.laptop.start(self.viewer.pid,self.dedicated.output)
                    return
            except (OSError,ValueError,KeyError): pass
            time.sleep(.1)
        raise RuntimeError("Stereo video was not ready, so the laptop display was left on.")

    def set_laptop_off(self, enabled):
        if type(enabled) is not bool: raise ValueError("Laptop display setting must be on or off")
        if enabled and self.direct:
            self.disable_laptop_display()
        elif not enabled:
            self.laptop.stop()
        self.laptop_off_enabled=enabled
        self.save_presentation()

    def camera_control(self, action):
        if action not in CAMERA_ACTIONS and action not in CANVAS_ACTIONS:
            raise ValueError("Unknown camera action")
        if action in CANVAS_ACTIONS and not self.canvas_mode:
            raise ValueError("Only available in Window canvas mode")
        if not self.viewer or self.viewer.poll() is not None:
            raise RuntimeError("Start stereo or a preview before using view controls.")
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as connection:
            connection.sendto(action.encode(), str(self.pose_socket))

    def connect_tracking(self):
        if self.sdk.process and self.sdk.process.poll() is None:
            return "Head tracking is starting. Press R to recenter."
        try:
            self.sdk.connect()
        except RuntimeError as exc:
            return "Head tracking is unavailable, but mouse look still works. " + str(exc)
        return "Head tracking is starting. Press R to recenter."

    def present(self, layout):
        if len(detect(self.monitors())["displays"]) != 1:
            raise RuntimeError("Connect one pair of VITURE glasses before opening the preview.")
        # Start is one action: apply the current draft, then present every panel.
        self.stop_viewer()
        if self.canvas_mode:
            return self._present_canvas()
        self.apply(layout)
        tracking_message = self.connect_tracking()
        applied = self.applied
        self.start(present=True)
        if not applied:
            raise RuntimeError("Apply your layout first")
        return f"{len(applied['monitors'])} virtual monitors are open on the glasses. {tracking_message} Press Esc to close the preview."

    def _present_canvas(self):
        self.canvas.ensure(self.monitors())
        tracking_message = self.connect_tracking()
        self.start(present=True)
        return f"Window canvas is open on the glasses. {tracking_message} Press Esc to close the preview."

    def terminal(self, identity):
        if self.canvas_mode:
            if not self.canvas.active:
                raise RuntimeError("Start the window canvas first")
            # The window.open handler adopts it; silent keeps focus where it is.
            self.runner("eval", f'hl.exec_cmd("foot", {{workspace="name:{CANVAS_WORKSPACE} silent"}})')
            return
        if not self.applied or identity not in [m["id"] for m in self.applied["monitors"]]:
            raise RuntimeError("Apply this monitor before opening an app on it.")
        name = self.prefix + identity
        monitor = next((m for m in self.monitors() if m["name"] == name), None)
        if not monitor:
            raise RuntimeError("This virtual monitor is disconnected. Choose Apply to restore it.")
        workspace = int(monitor["activeWorkspace"]["id"])
        self.runner("eval", f'hl.exec_cmd("foot", {{workspace="{workspace} silent"}})')

    @staticmethod
    def controls_version():
        base = os.environ.get("OMARCHY_XR_RUNTIME")
        if not base:
            base = str(Path(os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}") / "omarchy-xr")
        try:
            return int((Path(base) / "controls.version").read_text().strip())
        except (OSError, ValueError):
            return 0

    def controls_hint(self):
        if not self.viewer or self.viewer.poll() is not None:
            return ""
        return CONTROLS_HINT if self.controls_version() < 6 else ""

    def reconcile_status(self, monitors):
        if (self.applied or self.canvas.active) and monitors is not None:
            try:
                self.reconcile_outputs(monitors)
                self.reconcile_laptop_workspaces(monitors)
                self.output_error = ""
            except Exception as exc:
                self.output_error = str(exc)
        elif self.applied or self.canvas.active:
            self.output_error = self.output_error or "Display status unavailable"

    def note_exited_viewer(self):
        if not self.viewer or self.viewer.poll() is None:
            return
        detail = f"XR viewer exited (code {self.viewer.returncode})"
        self.viewer_exit = "The XR view stopped unexpectedly. Start it again to retry."
        self.append_backend_log(detail)
        try:
            self.stop_viewer()
        except Exception as exc:
            self.restoration_error = str(exc)

    def glasses_status(self, monitors):
        try:
            glasses = detect(monitors if monitors is not None else [])
            if monitors is None:
                glasses["detectionError"] = "Display status unavailable"
        except Exception:
            glasses = detect([])
            glasses["detectionError"] = "Display status unavailable"
        glasses["dedicatedDisplay"] = self.dedicated.output if self.direct else None
        glasses["runtimeInstalled"] = runtime_installed(self.renderer)
        glasses["helperAvailable"] = HELPER.is_file()
        glasses.update(self.recovery.status())
        glasses["sdk"] = self.sdk.status()
        pending = self.sdk.state.get("displayError") or ""
        if isinstance(pending, str) and "restoration is pending" in pending and pending not in self.restoration_error:
            self.restoration_error = "; ".join(filter(None, (self.restoration_error, pending)))
        return glasses

    def performance_sample(self):
        if not self.viewer or self.viewer.poll() is not None:
            return {}
        try:
            sample = json.loads(Path(str(self.pose_socket) + ".stats").read_text())
            if sample.get("pid") == self.viewer.pid and 0 <= time.monotonic() - sample["time"] < 12:
                return sample
        except (OSError, ValueError, KeyError, TypeError):
            pass
        return {}

    def status(self):
        try:
            monitors = self.monitors()
        except Exception:
            monitors = None
        self.reconcile_status(monitors)
        self.note_exited_viewer()
        glasses = self.glasses_status(monitors)
        performance = self.performance_sample()
        laptop_status = self.laptop.status()
        try:
            laptop_status["available"] = bool(internal(monitors or [])) or laptop_status["off"]
        except Exception:
            laptop_status["available"] = laptop_status["off"]
        return {"laptopOffEnabled": self.laptop_off_enabled, "laptopDisplay": laptop_status, "spectatorEnabled": self.spectator_enabled, "performance": performance, "active": len(self.owned), "viewing": self.viewer is not None and self.viewer.poll() is None,
                "direct": self.direct, "stereo": self.stereo_active, "viewerExit": self.viewer_exit, "controlsHint": self.controls_hint(),
                "renderMode": self.render_mode, "canvasActive": self.canvas.active, "controlsVersion": self.controls_version(),
                "canvasWindows": performance.get("canvasWindows", 0) if self.canvas_mode else 0,
                "canvasState": performance.get("canvasState", "") if self.canvas_mode else "",
                "restorationError": "; ".join(filter(None, (self.restoration_error, self.output_error, self.viewer_exit))), "glasses": glasses}

    def append_backend_log(self, text):
        path = self.directory / "backend.log"
        try:
            if path.exists() and path.stat().st_size > 512 * 1024:
                previous = self.directory / "backend.log.1"
                if previous.exists():
                    previous.unlink()
                path.replace(previous)
            with path.open("a", encoding="utf-8") as log:
                log.write(text)
                if not text.endswith("\n"):
                    log.write("\n")
        except OSError:
            pass

    def rotate_viewer_log(self):
        if self.log:
            self.log.close()
            self.log = None
        path = self.directory / "viewer.log"
        previous = self.directory / "viewer.log.1"
        try:
            if path.exists():
                path.replace(previous)
        except OSError:
            pass
        self.log = path.open("w", encoding="utf-8")
        self.viewer_exit = ""


def read_saved(manager, warnings, saved):
    load, path, fallback, warning, label = saved
    try:
        return load()
    except (OSError, ValueError) as exc:
        quarantine(path)
        warnings.append(warning)
        manager.append_backend_log(f"{label}: {exc}")
        return fallback


def action_load(manager, _request):
    warnings: list[str] = []
    layout = read_saved(manager, warnings, (manager.load, manager.profile, default_layout(), "Layout file was invalid and was set aside", "layout load"))
    controls = read_saved(manager, warnings, (lambda: load_controls(manager.directory), manager.directory / "controls-settings.json", dict(DEFAULTS), "Control settings were invalid and were set aside", "controls load"))
    setups = read_saved(manager, warnings, (manager.setups, manager.directory / "setups.json", {"version": 1, "selected": "", "items": []}, "Saved setups were invalid and were set aside", "setups load"))
    canvas = read_saved(manager, warnings, (manager.canvas.load, manager.canvas.profile, dict(CANVAS_DEFAULTS), "Canvas settings were invalid and were set aside", "canvas load"))
    response = {"layout": layout, "controls": controls, "setups": setups, "canvas": canvas, "graphicsLimits": manager.hardware_limits(), "environment": manager.environment.snapshot(), "builtInSetups": built_in_setups()}
    if warnings:
        response["message"] = " ".join(warnings)
    return response


def action_environment(manager, request):
    manager.environment.set(request.get("environment"))
    return {"environment": manager.environment.snapshot(), "message": "Background updated."}


def action_import_environment(manager, request):
    identity = manager.environment.import_image(request.get("imagePath", ""), request.get("imageResolution", 4096))
    manager.environment.set({**manager.environment.config, "id": identity})
    return {"environment": manager.environment.snapshot(), "message": "Background imported."}


def action_text_size(_manager, request):
    size = request.get("textSize")
    if type(size) is not int or size not in (9, 10, 11, 12, 14, 16, 20):
        raise ValueError("Unsupported text size")
    subprocess.run(["omarchy-display-text-size", str(size)], check=True, capture_output=True, text=True, timeout=15)
    return {"message": "Desktop text size updated."}


def action_save_setup(manager, request):
    identity = request.get("setupId") if request.get("updateSetup") else None
    name = manager.save_setup(request.get("setupName"), request["layout"], identity)
    return {"setups": manager.setups(), "message": "Setup saved: " + name}


def action_use_setup(manager, request):
    return {**manager.use_setup(request.get("setupId")), "setups": manager.setups()}


def action_save_controls(manager, request):
    settings = save_controls(manager.directory, request["controls"], manager.runner)
    return {"controls": settings, "message": "Shortcuts and gestures saved."}


def action_save(manager, request):
    manager.save(request["layout"])
    return {"layout": manager.load(), "message": "Monitor layout saved."}


def action_apply(manager, request):
    if manager.canvas_mode:
        return {"canvas": manager.canvas.ensure(manager.monitors()), "message": "Window canvas ready."}
    manager.apply(request["layout"])
    return {"layout": manager.applied, "message": "Virtual monitors ready."}


def action_present_direct(manager, request):
    already_direct = manager.direct and manager.viewer is not None and manager.viewer.poll() is None
    if manager.canvas_mode:
        response = {"canvas": manager.canvas.ensure(manager.monitors()), "message": "Stereo active."}
    else:
        manager.apply(request["layout"])
        response = {"layout": manager.applied, "message": "Stereo active."}
    if not already_direct:
        manager.start_dedicated()
    return response


def action_render_mode(manager, request):
    manager.set_render_mode(request.get("renderMode"))
    return {"message": "Window canvas selected." if manager.canvas_mode else "Virtual monitors selected."}


def action_canvas_settings(manager, request):
    settings = manager.canvas.save(request.get("canvas"))
    # canvas.tsv is hot-reloaded; refresh and scale changes reach the live output.
    if manager.canvas.active:
        manager.canvas.reconcile(manager.monitors())
    return {"canvas": settings, "message": "Window canvas settings saved."}


def action_camera(manager, request):
    messages = {"recenter": "View recentered.", "fit": "Workspace fitted to view.", "fit_target": "Selected monitor fitted to view.", "zoom_in": "Zoomed in.", "zoom_out": "Zoomed out.",
                "overview": "Overview toggled.", "search": "Search opened.", "fill": "Fill toggled.", "arrange": "Windows arranged.",
                "undo": "Undone.", "redo": "Redone.", "pin": "Pin toggled.", "help": "Help toggled."}
    manager.camera_control(request["action"])
    return {"message": messages[request["action"]]}


def action_present(manager, request):
    return {"message": manager.present(request["layout"]), "layout": manager.applied}


def action_laptop_off(manager, request):
    manager.set_laptop_off(request.get("enabled"))
    message = "Laptop display will turn off during stereo." if manager.laptop_off_enabled else "Laptop display restored; automatic shutoff disabled."
    return {"message": message}


def action_restore_laptop(manager, _request):
    manager.laptop.stop()
    return {"message": "Laptop display restored."}


def action_spectator(manager, request):
    manager.set_spectator(request["enabled"])
    message = "Recording window enabled for stereo sessions." if manager.spectator_enabled else "Recording window disabled."
    return {"message": message}


def action_stop_viewer(manager, _request):
    manager.stop_viewer()
    return {"message": "XR view closed. Windows returned to your computer display."}


def action_start(manager, _request):
    manager.start()
    return {"message": "Preview opened."}


def action_stop(manager, _request):
    manager.cleanup()
    return {"message": "XR stopped and virtual monitors removed."}


def action_terminal(manager, request):
    manager.terminal(request["id"])
    return {"message": "Terminal opened on the selected monitor."}


def action_reinitialize(manager, _request):
    manager.stop_viewer()
    manager.sdk.disconnect()
    manager.recovery.start()
    return {}


def action_sdk_connect(manager, _request):
    if manager.recovery.status()["recovering"]:
        raise RuntimeError("Wait for the glasses connection recovery to finish.")
    if manager.stereo_active:
        raise RuntimeError("Stop stereo before reconnecting head tracking.")
    manager.sdk.connect()
    return {}


def action_sdk_disconnect(manager, _request):
    manager.stop_viewer()
    manager.sdk.disconnect()
    return {"message": "Head tracking disconnected."}


def action_sdk_restore(manager, _request):
    if manager.stereo_active:
        raise RuntimeError("Stop stereo before restoring the glasses video.")
    manager.sdk.restore()
    return {}


def action_status(_manager, _request):
    return {}


def perform(manager, request):
    action = request["action"]
    if action in CAMERA_ACTIONS or action in CANVAS_ACTIONS:
        return action_camera(manager, request)
    handler = {
        "load": action_load,
        "set_environment": action_environment,
        "import_environment": action_import_environment,
        "set_text_size": action_text_size,
        "save_setup": action_save_setup,
        "use_setup": action_use_setup,
        "save_controls": action_save_controls,
        "save": action_save,
        "apply": action_apply,
        "present_direct": action_present_direct,
        "present": action_present,
        "set_laptop_off": action_laptop_off,
        "restore_laptop": action_restore_laptop,
        "set_spectator": action_spectator,
        "set_render_mode": action_render_mode,
        "set_canvas_settings": action_canvas_settings,
        "stop_viewer": action_stop_viewer,
        "start": action_start,
        "stop": action_stop,
        "terminal": action_terminal,
        "reinitialize": action_reinitialize,
        "sdk_connect": action_sdk_connect,
        "sdk_disconnect": action_sdk_disconnect,
        "sdk_restore": action_sdk_restore,
        "status": action_status,
    }.get(action)
    if handler is None:
        raise ValueError("Unknown action")
    return handler(manager, request)


def serve(manager):
    for line in sys.stdin:
        request_id = None
        try:
            request = json.loads(line)
            request_id = request.get("requestId")
            response = perform(manager, request)
            print(json.dumps({"ok": True, "requestId": request_id, **response, **manager.status()}), flush=True)
        except Exception as exc:
            manager.append_backend_log(traceback.format_exc())
            try:
                extra = manager.status()
            except Exception as status_exc:
                manager.append_backend_log(traceback.format_exc())
                extra = {"statusError": str(status_exc)}
            print(json.dumps({"ok": False, "requestId": request_id, "message": str(exc) or "Something went wrong. Try again.", **extra}), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--renderer", required=True)
    args = parser.parse_args()
    directory = Path(os.environ.get("XDG_STATE_HOME", str(Path.home() / ".local/state"))) / "omarchy-xr"
    manager = None
    shutting_down = False
    def stop(*_):
        nonlocal shutting_down
        if shutting_down:
            return
        shutting_down = True
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        raise SystemExit(0)
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGHUP, stop)
    try:
        manager = Manager(directory, args.renderer, runtime=runtime_dir())
        serve(manager)
    finally:
        if manager:
            try:
                manager.cleanup()
            finally:
                manager.sdk.disconnect()


if __name__ == "__main__":
    main()
