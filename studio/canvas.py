"""Window canvas session: its own journaled output, workspace rules and per-window migration.

Never touches layout.json or viewer.tsv; formats: docs/infinite-canvas-plan.md §3.3, §4.3, §6.5.
"""
import json
import math
import os
import re
import time
from typing import Any

from atomic_file import atomic_write
from glasses import detect

CANVAS_WORKSPACE = "omxr-canvas"
PARK_WORKSPACE = "omxr-park"
EXCLUDED_CLASSES = ("omarchy-xr-spectator", "omarchy-xr-search")
# The per-window set_prop overrides xr-controls.lua applies to canvas windows; "unset" drops them.
DECOR_PROPS = ("border_size", "rounding", "no_anim", "no_shadow", "no_blur", "no_dim")
DEFAULTS: dict[str, Any] = {"fps": 60, "radius": 2.4, "gapPx": 60, "dimUnmatched": 0.35, "labelDeg": 0.8,
                            "outputScale": 1.0, "refresh": 60, "captureBudgetMpix": 300, "adoptPolicy": "all",
                            "takeoverKeys": True, "exclude": []}
WIDTH, HEIGHT = 2560, 1440
ADDRESS = re.compile(r"0x[0-9a-fA-F]+")
# (key, low, high, whole, message): the parseSettings ranges of src/canvas_model.hpp.
RANGES = (("fps", 1, 120, True, "Canvas capture rate must be 1–120 fps"),
          ("radius", 1, 10, False, "Canvas radius must be 1–10 m"),
          ("gapPx", 0, 500, False, "Window gap must be 0–500 pixels"),
          ("dimUnmatched", 0, 1, False, "Search dimming must be 0–100 percent"),
          ("labelDeg", .1, 5, False, "Label size must be 0.1–5°"),
          ("captureBudgetMpix", 50, 2000, False, "Capture budget must be 50–2000 Mpix/s"))
# canvas.tsv header "# canvas v1 <TSV_FIELDS…> <adoptPolicy> <takeover> <refresh>": fields 1–7 below, 8 the adopt policy
# (Lua matches it positionally), 9 the key takeover, 10 the output refresh (the renderer's latency threshold).
TSV_FIELDS = ("fps", "radius", "gapPx", "dimUnmatched", "labelDeg", "outputScale", "captureBudgetMpix")


def _number(value, whole=False):
    # Booleans are ints in Python; reject them like _curvature does.
    return type(value) in ((int,) if whole else (int, float)) and math.isfinite(value)


def validate(settings):
    if not isinstance(settings, dict) or set(settings) - set(DEFAULTS):
        raise ValueError("Unknown canvas setting")
    result = {**DEFAULTS, **settings}
    for key, low, high, whole, message in RANGES:
        if not _number(result[key], whole) or not low <= result[key] <= high:
            raise ValueError(message)
    if not _number(result["outputScale"]) or result["outputScale"] not in (1.0, 1.25):
        raise ValueError("Canvas output scale must be 1 or 1.25")
    if not _number(result["refresh"], True) or result["refresh"] not in (60, 120):
        raise ValueError("Canvas output refresh must be 60 or 120 Hz")
    if result["adoptPolicy"] not in ("all", "empty"):
        raise ValueError("Choose whether the canvas adopts all windows or starts empty")
    if type(result["takeoverKeys"]) is not bool:
        raise ValueError("Canvas key takeover must be on or off")
    exclude = result["exclude"]
    if (not isinstance(exclude, list) or len(exclude) > 64
            or not all(isinstance(t, str) and re.fullmatch(r"[A-Za-z0-9._-]{1,80}", t) for t in exclude)):
        raise ValueError("Exclusions must be up to 64 app classes or process ids (letters, digits, . _ -)")
    result["exclude"] = list(exclude)
    return result


def _pair(value):
    return isinstance(value, list) and len(value) == 2 and all(type(v) is int for v in value)


def _origin_valid(origin):
    return (isinstance(origin, dict) and type(origin.get("workspace")) is int and type(origin.get("floating")) is bool
            and _pair(origin.get("size")) and _pair(origin.get("at")))


def _journal_valid(data):
    return (isinstance(data, dict) and isinstance(data.get("session"), str) and isinstance(data.get("windows"), dict)
            and all(isinstance(a, str) and ADDRESS.fullmatch(a) and _origin_valid(o) for a, o in data["windows"].items()))


def _window(address):
    return f'window="address:{address}"'


def _undecorate(address):
    return " ".join(f'hl.dispatch(hl.dsp.window.set_prop({{{_window(address)}, prop="{p}", value="unset"}}))' for p in DECOR_PROPS)


def excluded(client, tokens):
    """canvas.json `exclude`: an app class or a process id."""
    return client.get("class") in tokens or str(client.get("pid")) in tokens


class CanvasSession:
    def __init__(self, manager):
        self.manager = manager
        self.runner = manager.runner
        self.profile = manager.directory / "canvas.json"
        self.journal = manager.directory / "canvas-session.json"
        self.tsv = manager.directory / "canvas.tsv"
        self.position = None

    @property
    def name(self):
        return self.manager.prefix + "canvas"

    @property
    def active(self):
        return self.name in self.manager.owned

    def present(self):
        # Also a canvas output recovered from an earlier Studio run (another prefix).
        return any(name.endswith("-canvas") for name in self.manager.owned)

    def load(self):
        return validate(json.loads(self.profile.read_text())) if self.profile.exists() else dict(DEFAULTS)

    def settings(self):
        try:
            return self.load()
        except (OSError, ValueError) as exc:
            self.profile.replace(self.profile.with_name(self.profile.name + ".corrupt"))
            self.manager.append_backend_log(f"canvas load: {exc}")
            return dict(DEFAULTS)

    def save(self, settings):
        settings = validate(settings)
        atomic_write(self.profile, json.dumps(settings, indent=2) + "\n")
        self.publish(settings)
        return settings

    def publish(self, settings=None):
        s = settings or self.settings()
        header = " ".join(format(s[key], "g") for key in TSV_FIELDS)
        # Studio's backend and its Quickshell parent are never adopted.
        rows = [*s["exclude"], str(os.getpid()), str(os.getppid())]
        # Field 9 switches the optional window-key takeovers (read by the renderer, announced to Lua in .mode).
        takeover = 1 if s["takeoverKeys"] else 0
        atomic_write(self.tsv, f"# canvas v1 {header} {s['adoptPolicy']} {takeover} {s['refresh']}\n" + "".join(f"exclude {t}\n" for t in rows))

    def monitor_rule(self, settings, x):
        return (f'hl.monitor({{output="{self.name}", mode="{WIDTH}x{HEIGHT}@{settings["refresh"]}", '
                f'position="{x}x0", scale={format(settings["outputScale"], "g")}}})')

    @staticmethod
    def output_matches(actual, settings):
        return (actual.get("width") == WIDTH and actual.get("height") == HEIGHT
                and abs(actual.get("scale", 0) - settings["outputScale"]) < .001
                and abs(actual.get("refreshRate", settings["refresh"]) - settings["refresh"]) < 1
                and not actual.get("disabled", False))

    def ensure(self, existing):
        settings = self.settings()
        created = self.name not in {m["name"] for m in existing}
        try:
            if created:
                self.create(existing, settings)
            self.install_rules()
            if created:
                self.activate()
            self.publish(settings)
            if created and settings["adoptPolicy"] == "all":
                self.migrate()
            elif not created:
                self.reconcile(self.manager.monitors())
        except Exception:
            if created:
                self.discard()
            raise
        return settings

    def discard(self):
        # Undo a failed start without hiding its cause (place_monitors pattern).
        try:
            self.remove()
        except Exception as exc:
            self.manager.restoration_error = "; ".join(filter(None, (self.manager.restoration_error, str(exc))))

    def create(self, existing, settings):
        x = self.manager.desktop_origin(existing)
        self.runner("eval", self.monitor_rule(settings, x))
        # Journal intent first so a crash during creation can be recovered.
        self.manager.owned.add(self.name)
        self.manager.record()
        self.position = x
        self.runner("output", "create", "headless", self.name)
        for _ in range(30):
            actual: dict[str, Any] = next((m for m in self.manager.monitors() if m["name"] == self.name), {})
            if self.output_matches(actual, settings):
                return
            time.sleep(.1)
        raise RuntimeError("The desktop could not create the window canvas. Try Start again.")

    def install_rules(self):
        # Rule handles only have set_enabled, so a reinstall disables the previous set first.
        decor = "float=true, no_anim=true, border_size=0, rounding=0, no_shadow=true, no_blur=true, no_dim=true, " \
                'suppress_event="fullscreen maximize"'
        rules = [f'hl.workspace_rule({{workspace="name:{w}", monitor="{self.name}", persistent=true}})'
                 for w in (CANVAS_WORKSPACE, PARK_WORKSPACE)]
        rules += [f'hl.window_rule({{name="omarchy-xr-{w.removeprefix("omxr-")}", match={{workspace="name:{w}"}}, {decor}}})'
                  for w in (CANVAS_WORKSPACE, PARK_WORKSPACE)]
        self.runner("eval", "if omarchy_xr_canvas_rules then for _,r in ipairs(omarchy_xr_canvas_rules) do "
                    "r:set_enabled(false) end end omarchy_xr_canvas_rules={" + ", ".join(rules) + "}")

    def retire_rules(self):
        self.runner("eval", "if omarchy_xr_canvas_rules then for _,r in ipairs(omarchy_xr_canvas_rules) do "
                    "r:set_enabled(false) end end omarchy_xr_canvas_rules=nil")

    def activate(self):
        """Show the canvas workspace on the canvas output, then give focus back."""
        focused = next((m for m in self.manager.monitors() if m.get("focused")), None)
        back = focused.get("activeWorkspace", {}).get("name") if focused else None
        self.runner("eval", f'hl.dispatch(hl.dsp.focus({{workspace="name:{CANVAS_WORKSPACE}"}}))')
        if isinstance(back, str) and back:
            self.runner("eval", f"hl.dispatch(hl.dsp.focus({{workspace={json.dumps(back)}}}))")

    def regular(self, client):
        workspace = client.get("workspace") or {}
        return (client.get("mapped") is True and type(workspace.get("id")) is int and workspace["id"] > 0
                and workspace.get("name") not in (CANVAS_WORKSPACE, PARK_WORKSPACE)
                and isinstance(client.get("address"), str) and bool(ADDRESS.fullmatch(client["address"]))
                and client.get("class") not in EXCLUDED_CLASSES
                and client.get("pid") not in (os.getpid(), os.getppid())
                and not str(client.get("title", "")).startswith("Omarchy XR")
                and _pair(client.get("size")) and _pair(client.get("at")))

    def read_journal(self):
        if not self.journal.exists():
            return None
        try:
            data = json.loads(self.journal.read_text())
            if _journal_valid(data):
                return data
        except (OSError, ValueError):
            pass
        self.journal.replace(self.journal.with_name(self.journal.name + ".corrupt"))
        self.manager.append_backend_log("canvas session journal was invalid and was set aside")
        return None

    def migrate(self):
        self.adopt(json.loads(self.runner("-j", "clients")))

    def adopt(self, clients):
        """Journal every regular window's origin before the first dispatch, then park it."""
        tokens = set(self.settings()["exclude"])
        regular = [c for c in clients if self.regular(c) and not excluded(c, tokens)]
        if not regular:
            return
        data = self.read_journal() or {"session": os.environ.get("HYPRLAND_INSTANCE_SIGNATURE", ""), "windows": {}}
        for c in regular:
            data["windows"].setdefault(c["address"], {"workspace": c["workspace"]["id"], "floating": bool(c.get("floating")),
                                                      "size": list(c["size"]), "at": list(c["at"])})
        atomic_write(self.journal, json.dumps(data, indent=2) + "\n")
        for c in regular:
            window = _window(c["address"])
            self.runner("eval", f'hl.dispatch(hl.dsp.window.move({{{window}, workspace="name:{PARK_WORKSPACE}", follow=false}}))')
            if not c.get("floating"):
                # Invisible on the hidden park workspace; keeps the tiled size as the canvas size.
                w, h = c["size"]
                self.runner("eval", f'hl.dispatch(hl.dsp.window.float({{{window}, action="float"}})) '
                            f'hl.dispatch(hl.dsp.window.resize({{{window}, x={w}, y={h}}}))')

    def return_window(self, client, origin):
        window = _window(client["address"])
        on_canvas = (client.get("workspace") or {}).get("name") in (CANVAS_WORKSPACE, PARK_WORKSPACE)
        self.runner("eval", _undecorate(client["address"]))
        if on_canvas:
            self.runner("eval", f'hl.dispatch(hl.dsp.window.move({{{window}, workspace="{origin["workspace"]}", follow=false}}))')
        if not origin["floating"] and client.get("floating"):
            self.runner("eval", f'hl.dispatch(hl.dsp.window.float({{{window}, action="tile"}}))')
        elif origin["floating"] and on_canvas:
            (w, h), (x, y) = origin["size"], origin["at"]
            self.runner("eval", f'hl.dispatch(hl.dsp.window.resize({{{window}, x={w}, y={h}}})) '
                        f'hl.dispatch(hl.dsp.window.move({{{window}, x={x}, y={y}}}))')

    def fallback_workspace(self):
        monitors = self.manager.monitors()
        glasses = set(detect(monitors)["displays"])
        for m in monitors:
            if not m["name"].startswith("OMXR-") and m["name"] not in glasses and not m.get("disabled", False):
                return str(m["activeWorkspace"]["id"])
        raise RuntimeError("There is no computer display available for your XR windows. Turn on a display, then choose Stop again.")

    def restore(self):
        """Return every window to its origin, one by one, before the canvas output goes away."""
        journal = self.read_journal()
        if journal is None and not self.present():
            return
        origins = journal["windows"] if journal else {}
        failures, fallback = [], None
        for client in json.loads(self.runner("-j", "clients")):
            address = client.get("address", "")
            try:
                if address in origins:
                    self.return_window(client, origins[address])
                elif (client.get("workspace") or {}).get("name") in (CANVAS_WORKSPACE, PARK_WORKSPACE) and ADDRESS.fullmatch(address):
                    fallback = fallback or self.fallback_workspace()
                    self.runner("eval", f'hl.dispatch(hl.dsp.window.move({{{_window(address)}, workspace="{fallback}", follow=false}})) '
                                + _undecorate(address))
            except Exception as exc:
                failures.append(str(exc))
        if failures:
            raise RuntimeError("Some windows could not leave the window canvas. Choose Stop again. " + "; ".join(failures))
        self.journal.unlink(missing_ok=True)

    def remove(self):
        present = self.present()
        self.restore()
        # Disabled first: the persistent canvas workspaces must not reappear on the laptop.
        if present:
            self.retire_rules()
        if self.active:
            self.manager.remove({self.name})
        self.position = None

    def recover(self):
        """Windows leave a crashed session's canvas before its output is removed."""
        journal = self.read_journal()
        present = self.present()
        if journal is None and not present:
            return
        try:
            if journal and journal["session"] != os.environ.get("HYPRLAND_INSTANCE_SIGNATURE", ""):
                self.journal.unlink(missing_ok=True)  # addresses of another compositor session
            else:
                self.restore()
            if present:
                self.retire_rules()
        except Exception as exc:
            self.manager.restoration_error = "; ".join(filter(None, (self.manager.restoration_error, str(exc))))

    def reconcile(self, monitors):
        """Repair the output mode and rules lost on compositor reload; idempotent."""
        if not self.active:
            return
        actual = next((m for m in monitors if m["name"] == self.name), None)
        if actual is None:
            raise RuntimeError("The window canvas output disconnected. Choose Start to restore it.")
        settings = self.settings()
        workspaces = json.loads(self.runner("-j", "workspaces"))
        strays = [w for w in workspaces if w.get("name") in (PARK_WORKSPACE, CANVAS_WORKSPACE) and w.get("monitor") != self.name]
        if not strays and self.output_matches(actual, settings):
            return
        x = self.position if self.position is not None else actual.get("x", 0)
        self.runner("eval", self.monitor_rule(settings, x))
        self.install_rules()
        # Park first so the canvas workspace ends up visible on the canvas output.
        for w in sorted(strays, key=lambda w: w.get("name") == CANVAS_WORKSPACE):
            self.runner("eval", f'hl.dispatch(hl.dsp.workspace.move({{workspace={int(w["id"])}, monitor="{self.name}"}}))')
