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

from laptop_display import LaptopDisplay, internal
from workspace_presets import built_in_setups
from environment import Environment
from glasses import Recovery, detect
from sdk import SDK
from dedicated import Dedicated
import socket
from input_settings import load_controls, save_controls
from graphics_limits import detect as detect_graphics_limits, validate_dimensions


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


def effective_scale(monitor):
    # Omarchy monitor widget: align the requested scale to whole logical pixels.
    divisor = math.gcd(monitor["width"] * 120, monitor["height"] * 120)
    units = min(round(monitor.get("scale", 1) * 120), divisor)
    while divisor % units:
        units += 1
    return units / 120


def validate(layout, check_gaps=True):
    if not isinstance(layout, dict) or layout.get("version") != 1:
        raise ValueError("Unsupported layout version")
    if type(layout.get("fps")) is not int or not 1 <= layout["fps"] <= 120:
        raise ValueError("Capture rate must be 1–120 fps")
    monitors = layout.get("monitors")
    if not isinstance(monitors, list) or not monitors:
        raise ValueError("Add at least one monitor")
    def curvature(value):
        if type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 100:
            raise ValueError("Curvature must be 0–100 percent")
    curvature(layout.get("curvature", 0))
    if type(layout.get("workspaceFollow", False)) is not bool:
        raise ValueError("Workspace wrapping must be on or off")
    degrees=layout.get("workspaceDegrees", -1)
    if type(degrees) not in (int,float) or not math.isfinite(degrees) or (degrees != -1 and not 0<=degrees<=360):
        raise ValueError("Workspace wrap must be 0–360 degrees")
    seen = set()
    for m in monitors:
        if not isinstance(m, dict) or (not isinstance(m.get("id"), str) or not re.fullmatch(r"[a-zA-Z0-9_-]{1,40}", m["id"])):
            raise ValueError("Invalid monitor identity")
        if m["id"] in seen:
            raise ValueError("Duplicate monitor identity")
        seen.add(m["id"])
        curvature(m.get("curvature", 0))
        scale = m.get("scale", 1)
        if type(scale) not in (int, float) or scale not in (1, 1.25, 1.6, 2, 3, 4):
            raise ValueError("Unsupported monitor scale")
        brightness = m.get("brightness", 100)
        if type(brightness) is not int or not 1 <= brightness <= 100:
            raise ValueError("Brightness must be 1–100 percent")
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
        self.presentation_profile = self.directory / "presentation.json"
        try:
            presentation = json.loads(self.presentation_profile.read_text())
            self.spectator_enabled = presentation.get("spectator", False) is True
            self.laptop_off_enabled = presentation.get("laptopOff", False) is True
        except (OSError, ValueError, AttributeError):
            self.spectator_enabled = False
            self.laptop_off_enabled = False
        self.environment = Environment(self.directory)
        self.graphics_limits = None
        self.profile = self.directory / "layout.json"
        self.journal = self.directory / "outputs.json"
        self.owned = set()
        self.prefix = "OMXR-" + uuid.uuid4().hex[:8] + "-"
        self.applied = None
        self.output_geometry = {}
        self.internal_workspaces = {}
        self.output_error = ""
        self.recovery = Recovery()
        self.sdk = SDK(self.directory)
        self.dedicated = Dedicated(self.directory, self.renderer)
        self.direct = False
        self.stereo_active = False
        self.original_output = None
        self.restoration_error = ""
        self.viewer = None
        self.presenting = False
        self.log = None
        self.laptop = LaptopDisplay(self.directory, self.runner)
        try: self.laptop.recover()
        except Exception as exc: self.laptop.error = str(exc)
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

    def setups(self):
        path = self.directory / "setups.json"
        if not path.exists():
            return {"version": 1, "selected": "", "items": []}
        data = json.loads(path.read_text())
        if not isinstance(data, dict) or data.get("version") != 1 or not isinstance(data.get("items"), list):
            raise ValueError("Saved setups file is invalid")
        seen = set()
        for item in data["items"]:
            if not isinstance(item, dict) or not isinstance(item.get("id"), str) or item["id"] in seen:
                raise ValueError("Invalid saved setup identity")
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
            raise ValueError("Saved setup no longer exists")
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
            raise ValueError("Setup no longer exists")
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
        return {"layout": layout, "layoutDirty": False, "message": "Setup selected: " + item["name"]}

    def stop_viewer(self):
        failures = []
        try: self.laptop.stop()
        except Exception as exc: failures.append("Laptop display: " + str(exc))
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
        if self.stereo_active:
            try:
                self.sdk.stereo(False)  # request old EDID family before re-detection
            except Exception as exc:
                failures.append(str(exc))
        try:
            self.dedicated.stop()
        except Exception as exc:
            failures.append(str(exc))
        if self.stereo_active:
            try:
                if self.original_output:
                    original = self.original_output
                    name = original["name"]
                    for _ in range(100):
                        actual = next((m for m in self.monitors() if m["name"] == name), {})
                        if any(mode.startswith(f'{original["width"]}x{original["height"]}@') for mode in actual.get("availableModes", [])):
                            break
                        time.sleep(.1)
                    else:
                        raise RuntimeError("The previous display mode family has not reconnected")
                    self.sdk.restore_rate()
                    # Refresh-rate changes trigger a second link negotiation.
                    for _ in range(150):
                        actual = next((m for m in self.monitors() if m["name"] == name), {})
                        modes = actual.get("availableModes", [])
                        if any(mode.startswith(f'{original["width"]}x{original["height"]}@') and abs(float(mode.split("@")[1].removesuffix("Hz"))-original["refreshRate"])<1 for mode in modes):
                            break
                        time.sleep(.1)
                    else:
                        raise RuntimeError("The previous refresh rate has not reconnected")
                    mode = f'{original["width"]}x{original["height"]}@{original["refreshRate"]}'
                    position = f'{original["x"]}x{original["y"]}'
                    self.runner("eval", 'hl.monitor({output='+json.dumps(name)+', mode='+json.dumps(mode)+', position='+json.dumps(position)+', scale='+str(original["scale"])+ '})')
                self.sdk.verify_restore()
                self.stereo_active = False
                self.original_output = None
            except Exception as exc:
                failures.append(str(exc))
        self.direct = False
        self.restoration_error = "; ".join(failures)
        if failures:
            raise RuntimeError("XR display restoration needs retry: " + self.restoration_error)

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
        try:
            self.stop_viewer()
        finally:
            self.remove(list(self.owned))
            self.applied = None

    def persist_applied(self, layout):
        self.applied = json.loads(json.dumps(layout))
        self.save(layout)
        content = f'# settings {layout["fps"]} {layout.get("curvature", 0)} {layout["spacing"]} {layout.get("workspaceDegrees", -1)} {int(layout.get("workspaceFollow", False))}\n' + "".join(f'{self.prefix}{m["id"]}\t{m["x"]}\t{m["y"]}\t{m["width"]}\t{m["height"]}\t{m.get("curvature", 0)}\t{m.get("brightness", 100)}\n' for m in layout["monitors"])
        target = self.directory / "viewer.tsv"
        temp = target.with_suffix(".tmp")
        temp.write_text(content)
        temp.replace(target)

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
        raise RuntimeError("Hyprland did not stage the monitor layout update")

    def apply(self, layout):
        layout = add_gutters(layout)
        validate_dimensions(layout, self.hardware_limits())
        if self.applied and self.viewer and self.viewer.poll() is None and not self.direct:
            previous_sizes = {m["id"]:(m["width"],m["height"]) for m in self.applied["monitors"]}
            if any(m["id"] in previous_sizes and previous_sizes[m["id"]] != (m["width"],m["height"]) for m in layout["monitors"]):
                raise RuntimeError("Close the windowed/fullscreen preview before resizing monitors. Dedicated stereo supports live resizing.")
        geometry = lambda config: [{key: m[key] for key in ("id", "width", "height", "x", "y")} | {"scale": m.get("scale", 1)} for m in config["monitors"]]
        rate = max(60, layout["fps"])
        if self.applied and geometry(self.applied) == geometry(layout) and max(60, self.applied["fps"]) == rate:
            actual = {m["name"]:m for m in self.monitors()}
            if all(self.output_matches(actual.get(self.prefix + m["id"], {}), m, rate) for m in layout["monitors"]):
                # Presentation settings never reconfigure the compositor's outputs.
                self.persist_applied(layout)
                return
        new = []
        existing = self.monitors()
        other = [m for m in existing if m["name"] not in self.owned]
        # Preserve the desktop origin while its built-in panel is temporarily off.
        other += [m for m in self.laptop.saved() if m["name"] not in {o["name"] for o in other}]
        if not other:
            raise RuntimeError("Keep at least one existing display for the editor and viewer")
        base_x = max(self.output_rect(m)[2] for m in other) + 100
        min_x = min(m["x"] for m in layout["monitors"])
        min_y = min(m["y"] for m in layout["monitors"])
        desired = {self.prefix + m["id"] for m in layout["monitors"]}
        existing_names = {m["name"] for m in existing}
        try:
            targets = [{**m, "name":self.prefix+m["id"], "scale":effective_scale(m),
                        "x":base_x+m["x"]-min_x, "y":m["y"]-min_y} for m in layout["monitors"]]
            existing = self.stage_conflicting_outputs(existing, targets)
            for m in layout["monitors"]:
                name = self.prefix + m["id"]
                scale = effective_scale(m)
                if name in existing_names and name not in self.owned:
                    raise RuntimeError("Output name collision: " + name)
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
                previous = next((o for o in existing if o["name"] == name), {})
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
                raise RuntimeError("Hyprland did not apply the requested resolution, scale and position")
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
        if not self.applied: return
        actual = {m["name"]:m for m in monitors}
        rate = max(60, self.applied["fps"])
        changed = False
        for m in self.applied["monitors"]:
            name = self.prefix+m["id"]
            if name not in actual: raise RuntimeError("XR output disconnected: " + name)
            position = self.output_geometry.get(name, actual[name])
            if (self.output_matches(actual[name], m, rate)
                    and all(actual[name].get(k)==position[k] for k in ("x","y"))): continue
            changed = True
            self.runner("eval", f'hl.monitor({{output="{name}", mode="{m["width"]}x{m["height"]}@{rate}", position="{position["x"]}x{position["y"]}", scale={effective_scale(m)}}})')
        # Verify even when only an external reload, rather than Apply, caused drift.
        if not changed: return
        verified = {m["name"]:m for m in self.monitors()}
        if not all(self.output_matches(verified.get(self.prefix+m["id"],{}),m,rate) for m in self.applied["monitors"]):
            raise RuntimeError("XR output mode restoration pending")

    def redistribute_laptop_windows(self, workspace_ids, monitors):
        targets = [m["activeWorkspace"]["id"] for m in monitors if m["name"] in self.owned
                   and m.get("activeWorkspace",{}).get("id",0)>0]
        if not workspace_ids: return
        if not targets: raise RuntimeError("No visible XR workspace for laptop windows")
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

    def start(self, present=False, direct=False):
        if not self.applied:
            raise RuntimeError("Apply your layout first")
        if not Path(self.renderer).is_file():
            raise RuntimeError("Renderer not installed. Run make install-studio")
        args = [self.renderer, "--layout", str(self.directory / "viewer.tsv"), "--fps", str(self.applied["fps"]),
                "--workspace-curvature", str(self.applied.get("curvature", 0)), "--spacing", str(self.applied["spacing"]),
                "--pose-socket", str(self.directory / "pose.sock")]
        if self.applied.get("workspaceFollow", False):
            args += ["--workspace-follow"]
        if self.applied.get("workspaceDegrees", -1)>=0:
            args += ["--workspace-degrees", str(self.applied["workspaceDegrees"])]
        if direct:
            args += ["--direct", self.dedicated.output, "--stereo"]
            if self.spectator_enabled:
                self.place_spectator()
                args += ["--spectator"]
        elif present:
            displays = detect(self.monitors())["displays"]
            if len(displays) != 1:
                raise RuntimeError("Connect exactly one active VITURE display before opening on glasses")
            args += ["--display", displays[0]]
        if not direct:
            self.stop_viewer()
        self.direct = direct
        self.presenting = present
        self.log = (self.directory / "viewer.log").open("w")
        self.viewer = subprocess.Popen(args, stdout=self.log, stderr=self.log)
        time.sleep(.25)
        if self.viewer.poll() is not None:
            raise RuntimeError("Viewer could not start. See " + str(self.directory / "viewer.log"))

    def display_event(self, stage, error=None):
        # Append rather than overwrite so a later recovery attempt keeps the cause.
        event = {"time": time.time(), "stage": stage}
        if error is not None:
            event["error"] = str(error)
        try:
            event["monitors"] = self.monitors()
        except Exception as exc:
            event["monitorError"] = str(exc)
        with (self.directory / "display-events.jsonl").open("a") as log:
            log.write(json.dumps(event) + "\n")

    def start_dedicated(self):
        self.stop_viewer()
        displays = detect(self.monitors())["displays"]
        if len(displays) != 1:
            raise RuntimeError("Connect exactly one VITURE video output")
        self.original_output = next(m for m in self.monitors() if m["name"] == displays[0])
        if not self.sdk.process or self.sdk.process.poll() is not None:
            self.sdk.connect()
        stage = "SDK stereo request"
        try:
            self.display_event("stereo-start")
            self.stereo_active = True  # restore even if mode setting partially fails
            self.sdk.stereo(True)
            self.display_event("stereo-request-acknowledged")
            stage = "waiting for stereo EDID"
            # Wait for the device's new EDID before taking a snapshot for the handoff.
            for _ in range(100):
                monitors = self.monitors()
                target = next((m for m in monitors if m["name"] == displays[0]), {})
                if any(mode.startswith("3840x1080") for mode in target.get("availableModes", [])):
                    break
                time.sleep(.1)
            else:
                raise RuntimeError("The glasses did not advertise their stereo video mode")
            stage = "leasing glasses display"
            self.display_event("stereo-edid-ready")
            self.dedicated.start(displays[0])
            stage = "starting renderer"
            self.start(present=True, direct=True)
            stage = "verifying stereo scanout"
            self.sdk.verify_stereo()
            self.display_event("stereo-running")
            if self.laptop_off_enabled:
                stage = "disabling laptop display"
                self.disable_laptop_display()
        except Exception as exc:
            message = f"Stereo startup failed at {stage}: {exc}"
            self.display_event("stereo-start-failed", message)
            try:
                self.stop_viewer()
            except Exception as recovery:
                self.display_event("stereo-rollback-failed", recovery)
                raise RuntimeError(message + "; recovery also failed: " + str(recovery)) from exc
            raise RuntimeError(message) from exc

    def place_spectator(self):
        monitors = self.monitors()
        glasses = detect(monitors)["displays"]
        candidates = [m for m in monitors if not m["name"].startswith("OMXR-")
                      and m["name"] not in glasses and m["name"] != self.dedicated.output
                      and not m.get("disabled", False)]
        candidates.sort(key=lambda m: not m["name"].startswith("eDP"))
        if not candidates:
            raise RuntimeError("Connect a computer display for the mono window")
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
                connection.sendto(b"spectator_on" if enabled else b"spectator_off", str(self.directory / "pose.sock"))
        self.spectator_enabled = enabled
        self.save_presentation()

    def save_presentation(self):
        atomic_json(self.presentation_profile, {"spectator":self.spectator_enabled,"laptopOff":self.laptop_off_enabled})

    def disable_laptop_display(self):
        if not self.direct or not self.stereo_active or not self.viewer or self.viewer.poll() is not None:
            raise RuntimeError("Start stereo before disabling the laptop display")
        # A live process alone does not establish that the glasses are displaying frames.
        for _ in range(80):
            if self.viewer.poll() is not None: break
            try:
                stats=json.loads((self.directory/"pose.sock.stats").read_text())
                if stats.get("pid")==self.viewer.pid and stats.get("fps",0)>0 and 0<=time.monotonic()-stats["time"]<8:
                    if self.applied:
                        monitors = self.monitors()
                        self.reconcile_outputs(monitors)
                        self.reconcile_laptop_workspaces(monitors, disabling=True)
                    self.laptop.start(self.viewer.pid,self.dedicated.output)
                    return
            except (OSError,ValueError,KeyError): pass
            time.sleep(.1)
        raise RuntimeError("Stereo frames not confirmed; laptop display left on")

    def set_laptop_off(self, enabled):
        if type(enabled) is not bool: raise ValueError("Laptop display setting must be on or off")
        if enabled and self.direct:
            self.disable_laptop_display()
        elif not enabled:
            self.laptop.stop()
        self.laptop_off_enabled=enabled
        self.save_presentation()

    def camera_control(self, action):
        if action not in ("recenter", "fit", "fit_target", "zoom_in", "zoom_out"):
            raise ValueError("Unknown camera action")
        if not self.viewer or self.viewer.poll() is not None:
            raise RuntimeError("Open the XR viewer first")
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as connection:
            connection.sendto(action.encode(), str(self.directory / "pose.sock"))

    def present(self, layout):
        if len(detect(self.monitors())["displays"]) != 1:
            raise RuntimeError("Connect exactly one active VITURE display before opening on glasses")
        # Start is one action: apply the current draft, then present every panel.
        self.stop_viewer()
        self.apply(layout)
        tracking_message = "Head tracking is connecting; R recenters."
        if not self.sdk.process or self.sdk.process.poll() is not None:
            try:
                self.sdk.connect()
            except RuntimeError as exc:
                tracking_message = "Mouse look available. " + str(exc)
        self.start(present=True)
        return f"{len(self.applied['monitors'])} desktops open on glasses. {tracking_message} Esc closes the viewer."

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
        if self.applied:
            try:
                monitors = self.monitors()
                self.reconcile_outputs(monitors)
                self.reconcile_laptop_workspaces(monitors)
                self.output_error = ""
            except Exception as exc:
                self.output_error = str(exc)
        if self.viewer and self.viewer.poll() is not None:
            try:
                self.stop_viewer()
            except Exception as exc:
                self.restoration_error = str(exc)
        try:
            glasses = detect(self.monitors())
        except Exception:
            glasses = detect([])
            glasses["detectionError"] = "Display status unavailable"
        glasses["dedicatedDisplay"] = self.dedicated.output if self.direct else None
        glasses.update(self.recovery.status())
        glasses["sdk"] = self.sdk.status()
        performance = {}
        if self.viewer and self.viewer.poll() is None:
            try:
                sample = json.loads((self.directory / "pose.sock.stats").read_text())
                if sample.get("pid") == self.viewer.pid and 0 <= time.monotonic()-sample["time"] < 12:
                    performance = sample
            except (OSError, ValueError, KeyError, TypeError):
                pass
        laptop_status=self.laptop.status()
        try: laptop_status["available"]=bool(internal(self.monitors())) or laptop_status["off"]
        except Exception: laptop_status["available"]=laptop_status["off"]
        return {"laptopOffEnabled":self.laptop_off_enabled,"laptopDisplay":laptop_status,"spectatorEnabled": self.spectator_enabled, "performance": performance, "active": len(self.owned), "viewing": self.viewer is not None and self.viewer.poll() is None,
                "direct": self.direct, "stereo": self.stereo_active, "restorationError": "; ".join(filter(None, (self.restoration_error,self.output_error))), "glasses": glasses}


def serve(manager):
    for line in sys.stdin:
        request_id = None
        try:
            request = json.loads(line)
            request_id = request.get("requestId")
            action = request["action"]
            if action == "load":
                response = {"layout": manager.load(), "controls": load_controls(manager.directory), "setups": manager.setups(), "graphicsLimits": manager.hardware_limits(), "environment":manager.environment.snapshot(), "builtInSetups":built_in_setups()}
            elif action == "set_environment":
                manager.environment.set(request.get("environment"))
                response = {"environment":manager.environment.snapshot(), "message":"Background updated."}
            elif action == "import_environment":
                identity = manager.environment.import_image(request.get("imagePath", ""),request.get("imageResolution",4096))
                manager.environment.set({**manager.environment.config,"id":identity})
                response = {"environment":manager.environment.snapshot(), "message":"Background imported."}
            elif action == "set_text_size":
                size = request.get("textSize")
                if type(size) is not int or size not in (9, 10, 11, 12, 14, 16, 20):
                    raise ValueError("Unsupported text size")
                subprocess.run(["omarchy-display-text-size", str(size)], check=True, capture_output=True, text=True, timeout=15)
                response = {"message": "Desktop text size updated."}
            elif action == "save_setup":
                name = manager.save_setup(request.get("setupName"), request["layout"], request.get("setupId") if request.get("updateSetup") else None)
                response = {"setups": manager.setups(), "message": "Setup saved: " + name}
            elif action == "use_setup":
                response = {**manager.use_setup(request.get("setupId")), "setups": manager.setups()}
            elif action == "save_controls":
                settings=save_controls(manager.directory,request['controls'],manager.runner)
                response={"controls": settings, "message": "Controls saved — applied live; viewer stays running"}
            elif action == "save":
                manager.save(request["layout"]); response = {"layout": manager.load(), "message": "Layout saved"}
            elif action == "apply":
                manager.apply(request["layout"]); response = {"layout": manager.applied, "message": "Virtual monitors ready."}
            elif action == "present_direct":
                already_direct = manager.direct and manager.viewer is not None and manager.viewer.poll() is None
                manager.apply(request["layout"])
                if not already_direct:
                    manager.start_dedicated()
                response = {"layout": manager.applied, "message": "Stereo active."}
            elif action in ("recenter", "fit", "fit_target", "zoom_in", "zoom_out"):
                manager.camera_control(action); response = {"message": {"recenter":"View recentered", "fit":"Workspace fitted", "fit_target":"Target monitor fit requested", "zoom_in":"Zoomed in", "zoom_out":"Zoomed out"}[action]}
            elif action == "present":
                response = {"message": manager.present(request["layout"]), "layout": manager.applied}
            elif action == "set_laptop_off":
                manager.set_laptop_off(request.get("enabled"))
                response = {"message":"Laptop display will turn off during stereo." if manager.laptop_off_enabled else "Laptop display restored; automatic shutoff disabled."}
            elif action == "restore_laptop":
                manager.laptop.stop()
                response = {"message":"Laptop display restored."}
            elif action == "set_spectator":
                manager.set_spectator(request["enabled"])
                response = {"message": "Mono window enabled for stereo sessions" if manager.spectator_enabled else "Mono window closed and disabled"}
            elif action == "stop_viewer":
                manager.stop_viewer(); response = {"message": "Viewer closed; virtual desktops kept running"}
            elif action == "start":
                manager.start(); response = {"message": "Live viewer opened"}
            elif action == "stop":
                manager.cleanup(); response = {"message": "Viewer stopped and virtual monitors removed"}
            elif action == "terminal":
                manager.terminal(request["id"]); response = {"message": "Terminal opened on selected monitor"}
            elif action == "reinitialize":
                manager.stop_viewer(); manager.sdk.disconnect(); manager.recovery.start(); response = {}
            elif action == "sdk_connect":
                if manager.recovery.status()["recovering"]:
                    raise RuntimeError("Wait for USB-C recovery to finish")
                if manager.stereo_active:
                    raise RuntimeError("Close the dedicated viewer before reconnecting the SDK")
                manager.sdk.connect(); response = {}
            elif action == "sdk_disconnect":
                manager.stop_viewer(); manager.sdk.disconnect(); response = {"message": "SDK disconnected"}
            elif action == "sdk_restore":
                if manager.stereo_active:
                    raise RuntimeError("Close the dedicated viewer before retrying display mode")
                manager.sdk.restore(); response = {}
            elif action == "status":
                response = {}
            elif action == "hide":
                response = {"message": "Studio hidden; virtual monitors and viewer stay running"}
            else:
                raise ValueError("Unknown action")
            print(json.dumps({"ok": True, "requestId": request_id, **response, **manager.status()}), flush=True)
        except Exception as exc:
            print(json.dumps({"ok": False, "requestId": request_id, "message": str(exc), **manager.status()}), flush=True)


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
            try:
                manager.cleanup()
            finally:
                manager.sdk.disconnect()


if __name__ == "__main__":
    main()
