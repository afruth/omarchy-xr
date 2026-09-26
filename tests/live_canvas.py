#!/usr/bin/env python3
"""Opt-in live Window Canvas harness (docs/infinite-canvas-plan.md §7 M2); needs a Hyprland session.

    python3 tests/live_canvas.py write FILE [--stage ADDRESS] [--all]   one .windows mailbox (§3.1)
    python3 tests/live_canvas.py watch FILE [--stage ADDRESS] [--all]   rewrite it every 500 ms
    python3 tests/live_canvas.py profile [NAME] [--seconds 40] [--stereo] [--size WxH] [--fov DEG] [--churn]
                                         [--all-in-view] [--shots DIR] [--dir DIR]
    python3 tests/live_canvas.py --smoke                                 make smoke-canvas

`profile` reuses tools/spike_canvas.py: a headless SPIKE-canvas output, the profile's test clients (the
staged tier on the canvas workspace, every other tier parked), the renderer windowed on that output with
`--canvas`, a steady pose, `fit` into Overview after ~20 s and back to Work two reports later. A stand-in
for Lua's `.tiers` consumer (the real adapter serves OMXR- outputs only) moves the renderer's slivers to
the right edge of the spike canvas workspace. The verdict compares every settled report from
`pose.sock.stats` with the settled ladder of tests/canvas_ladder.py (§4.4, M5): rate, tier, lanes and
place per window, fps within 1 of the rate, usedMpix <= effectiveMpix; plus <= 4 `screencast>>` events
after the first 8 s outside the 2.5 s after a view change the harness makes (sessions stay alive across
rate and place changes; windows entering or leaving the view start or stop theirs), no renderer GPU-memory growth
between the first settled report and the last, and with --churn (a 1080p client opens at 10 s and closes
at 16 s) no ladder tier raised twice within 2 s. Only SPIKE-canvas is created or removed; the renderer
never runs with --direct/--display, and the previously active window gets focus back.
"""
import argparse
import json
import os
import pathlib
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))
import canvas_ladder as ladder  # noqa: E402
import spike_canvas as sc  # noqa: E402

RENDERER = ROOT / "build" / "omarchy-xr"
EXCLUDED = {"omarchy-xr-spectator", "omarchy-xr-search"}
DEFAULT_PROFILE = "zoomed-in-near-parked"
OVERVIEW_AT = 20.0
BUDGET_MPIX = 300.0   # canvas.tsv default captureBudgetMpix
CHURN_OPEN, CHURN_CLOSE = 10.0, 16.0    # --churn: a 1080p client opens and closes during Work
SCREENCAST_AFTER, SCREENCAST_MAX, TRANSITION_HOLD = 8.0, 4, 2.5
TIER_RANK = {"near": 0, "far": 1, "overview": 1, "idle": 2}
Clients = dict[str, tuple[int, int]]   # address -> the client's size, for windows without a frame yet


def hex_text(text: str) -> str:
    """windows::encodeHex: lowercase hex of the UTF-8 bytes, at most 550 bytes (validHex), `-` when empty."""
    data = text.encode()[:550].decode(errors="ignore").encode()
    return data.hex() or "-"


def listed(everyone: bool) -> list[dict]:
    """Mapped clients for the mailbox: the spike class unless everyone; never the renderer or helpers."""
    rows = []
    for c in json.loads(sc.hyprctl("clients", "-j")):
        if not c.get("mapped", True) or c["class"] in EXCLUDED or c["title"].startswith("Omarchy XR"):
            continue
        if c["workspace"]["name"].startswith("special:") or min(c["size"]) < 1:
            continue
        if everyone or c["class"] == sc.CLASS:
            rows.append(c)
    return rows


def default_stage(rows: list[dict]) -> str | None:
    """The most recently focused client on the spike canvas workspace (focus_history_id 0 when it has focus)."""
    canvas = [c for c in rows if c["workspace"]["name"] == "spikecanvas"]
    return min(canvas, key=lambda c: c["focusHistoryID"])["address"] if canvas else None


def row(c: dict, stage: str | None) -> str:
    workspace = c["workspace"]["name"]
    place = ("stage" if c["address"] == stage else "park" if workspace == "spikepark"
             else "sliver" if workspace == "spikecanvas" else "off")
    (w, h), (x, y) = c["size"], c["at"]
    return (f'{c["address"]} {hex_text(c["class"])} {hex_text(c["title"])} {w} {h} {x} {y} {c["focusHistoryID"]} '
            f'{place} {int(c["floating"])} {c["pid"]} {int(c["xwayland"])} 1')


class Mailbox:
    """Writes `v1 <pid> <seq> <boottime s> <outX> <outY> <output>` plus 13-field rows with tmp+rename; seq only grows."""

    def __init__(self, path: pathlib.Path):
        self.path, self.seq = path, 0
        self.lock = threading.Lock()   # the Tiers stand-in rewrites it from its thread

    def write(self, stage: str | None = None, everyone: bool = False) -> int:
        with self.lock:
            return self._write(stage, everyone)

    def _write(self, stage: str | None, everyone: bool) -> int:
        rows = listed(everyone)
        stage = stage or default_stage(rows)
        boot = time.clock_gettime(time.CLOCK_BOOTTIME)
        self.seq = max(self.seq + 1, int(boot * 1000))
        header = f"v1 {os.getpid()} {self.seq} {int(boot)} {sc.OUT_X} 0 {sc.OUTPUT}"
        text = "\n".join([header, *(row(c, stage) for c in rows)]) + "\n"
        part = self.path.with_name(self.path.name + ".tmp")
        part.write_text(text)
        os.replace(part, self.path)
        return len(rows)


@dataclass
class Group:
    tier: str
    target: int
    members: list[dict]
    staged: bool


def spawn_profile(name: str) -> list[Group]:
    """Spawn a spike_canvas profile: the stage tier on the canvas workspace, every other tier parked (M2 has
    no slivers, so `pile` tiers park too). Row order in the mailbox follows spawn order."""
    groups = []
    for part in sc.PROFILES.get(name, name).split(";"):
        tier, count, size, draw, fps, placement, *_ = part.split(":")
        members = [sc.spawn(f"{tier}{i}", size, float(draw)) for i in range(int(count))]
        for i, w in enumerate(members):
            sc.place(w, "stage" if placement == "stage" else "park", i)
        groups.append(Group(tier, int(fps), members, placement == "stage"))
    sc.activate_canvas()
    return groups


class Pose:
    """A steady euler-nwu-v1 head pose at 100 Hz (like tests/live_focus.py) plus one-shot commands."""

    def __init__(self, path: pathlib.Path):
        self.path, self.stop = str(path), threading.Event()
        self.channel = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.thread = threading.Thread(target=self.feed, daemon=True)

    def feed(self) -> None:
        while not self.stop.is_set():
            self.send(f"euler-nwu-v1 {time.monotonic():.6f} 0 0 0")
            time.sleep(0.01)

    def send(self, command: str) -> None:
        try:
            self.channel.sendto(command.encode(), self.path)
        except OSError:
            pass

    def close(self) -> None:
        self.stop.set()
        if self.thread.is_alive():
            self.thread.join(timeout=1)
        self.channel.close()


def launch(temp: pathlib.Path, extra: list[str], log) -> subprocess.Popen:
    """The renderer windowed (never --direct/--display), mapped on the spike canvas workspace through the
    spike class rule so it neither tiles into the user's workspace nor takes focus."""
    return subprocess.Popen([*launch_cmd(temp), *extra], stdout=log, stderr=subprocess.STDOUT, env=renderer_env())


def launch_cmd(temp: pathlib.Path) -> list[str]:
    return [str(RENDERER), "--canvas", str(temp / "canvas.tsv"), "--canvas-windows-file", str(temp / "windows.tsv")]


def renderer_env() -> dict[str, str]:
    return {**os.environ, "SDL_VIDEO_WAYLAND_WMCLASS": sc.CLASS}


def float_renderer(pid: int, size: str) -> None:
    """Resize the renderer window and put it in the bottom-right corner of SPIKE-canvas."""
    w, h = (int(v) for v in size.split("x"))
    for _ in range(100):
        window = next((c for c in sc.clients() if c["pid"] == pid), None)
        if window:
            addr = window["address"]
            sc.lua(f'hl.dispatch(hl.dsp.window.resize({{window="address:{addr}", x={w}, y={h}}})) '
                   f'hl.dispatch(hl.dsp.window.move({{window="address:{addr}", x={sc.OUT_X + sc.OUT_W - w}, '
                   f'y={sc.OUT_H - h}}}))')
            return
        time.sleep(0.05)
    raise RuntimeError("renderer window did not map")


def kill_clients() -> None:
    """SIGTERM the spike clients and wait until they are unmapped, so none moves to another output when
    SPIKE-canvas goes; SIGKILL whatever is left after 5 s. sc.teardown() then only removes the output."""
    for sig in (signal.SIGTERM, signal.SIGKILL):
        pids = {c["pid"] for c in sc.clients()}
        for pid in pids:
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + 5
        while sc.clients() and time.monotonic() < deadline:
            time.sleep(0.1)
        if not sc.clients():
            return


def cleanup(user_window: str | None) -> None:
    kill_clients()
    sc.teardown()
    refocus(user_window)


def refocus(address: str | None) -> None:
    if address:
        try:
            sc.lua(f'hl.dispatch(hl.dsp.focus({{window="address:{address}"}}))')
        except RuntimeError as error:
            print("focus restore:", error)


def active_window() -> str | None:
    return json.loads(sc.hyprctl("activewindow", "-j") or "{}").get("address")


class Tiers:
    """Stand-in for Lua's `.tiers` consumer (config/xr-controls.lua applyTiers) on SPIKE-canvas: every 250 ms
    it reads `<pose>.controls.tiers` (`v1 <pid> <seq> <stamp> [<address> sliver]...`); on a new seq it moves
    newly listed spike clients to the 8 px strip at the right edge of the canvas workspace (24 px apart in
    address order, no_follow_mouse set) and parks the slivers no longer listed. A missing or stale file
    means no slivers and forgets the seq, so the heartbeat brings them back. After a move it rewrites the
    window list, like Lua: the renderer captures a sliver damage-driven only once its row says so.
    Only spike clients are ever touched."""

    def __init__(self, path: pathlib.Path, stage: str | None, mailbox: Mailbox | None = None):
        self.path, self.stage, self.mailbox = path, stage, mailbox
        self.owner, self.seq, self.moves = "", 0, 0
        self.slivers: list[str] = []
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.loop, daemon=True)

    def read(self) -> list[str] | None:
        """The listed slivers, [] for a missing or stale file, None when there is nothing new to apply."""
        try:
            f = self.path.read_text().split()
        except OSError:
            self.seq = 0
            return []
        if len(f) < 4 or f[0] != "v1" or len(f) % 2 or not f[2].isdigit() or not f[3].isdigit():
            return None
        if time.clock_gettime(time.CLOCK_BOOTTIME) - int(f[3]) > 2:
            self.seq = 0
            return []
        if f[1] == self.owner and int(f[2]) <= self.seq:
            return None
        self.owner, self.seq = f[1], int(f[2])
        return sorted((a for a, place in zip(f[4::2], f[5::2]) if place == "sliver"), key=lambda a: int(a, 16))

    def apply(self, wanted: list[str]) -> None:
        spike = {int(c["address"], 16): c["address"] for c in sc.clients()}
        stage = int(self.stage, 16) if self.stage else None
        keep = [spike[int(a, 16)] for a in wanted if int(a, 16) in spike and int(a, 16) != stage]
        for address in self.slivers:
            if address not in keep and int(address, 16) in spike:
                sc.unsliver(address)
                self.moves += 1
        for index, address in enumerate(keep):
            if address not in self.slivers:
                sc.sliver(address, index)
                self.moves += 1
        self.slivers = keep

    def loop(self) -> None:
        while not self.stop.wait(0.25):
            try:
                wanted = self.read()
                if wanted is not None and wanted != self.slivers:
                    self.apply(wanted)
                    if self.mailbox:
                        self.mailbox.write(self.stage)
            except (RuntimeError, ValueError) as error:
                print("tiers:", error, flush=True)

    def close(self) -> None:
        self.stop.set()
        if self.thread.is_alive():
            self.thread.join(timeout=2)


class Screencasts:
    """Counts Hyprland `screencast>>` events (socket2) with their monotonic time: the recording indicator
    must stay steady while rates and places change, so sessions stay alive."""

    def __init__(self) -> None:
        self.events: list[tuple[float, str]] = []
        self.channel: socket.socket | None = None
        base = pathlib.Path(os.environ.get("XDG_RUNTIME_DIR", "/run/user/%d" % os.getuid()))
        path = base / "hypr" / os.environ.get("HYPRLAND_INSTANCE_SIGNATURE", "") / ".socket2.sock"
        try:
            self.channel = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.channel.connect(str(path))
        except OSError as error:
            print(f"screencast events unavailable ({error})", flush=True)
            self.channel = None
            return
        threading.Thread(target=self.loop, daemon=True).start()

    def loop(self) -> None:
        pending = b""
        while self.channel:
            try:
                data = self.channel.recv(65536)
            except OSError:
                return
            if not data:
                return
            pending += data
            *lines, pending = pending.split(b"\n")
            self.events += [(time.monotonic(), line.decode(errors="replace")) for line in lines if line.startswith(b"screencast")]

    def after(self, t: float, marks: tuple[float, ...] = ()) -> list[tuple[float, str]]:
        """`screencast>>` events from t on (screencastv2 repeats them with the owner and is not counted),
        leaving out those within TRANSITION_HOLD s after a mark: a window entering the view starts a
        session and one leaving it stops ~0.5 s later (Hyprland's stop timer runs out without a copy)."""
        return [(x, text) for x, text in self.events if x >= t and text.startswith("screencast>>")
                and not any(m <= x <= m + TRANSITION_HOLD for m in marks)]

    def close(self) -> None:
        channel, self.channel = self.channel, None
        if channel:
            channel.shutdown(socket.SHUT_RDWR)
            channel.close()


def _kib(value: str) -> int:
    number, *unit = value.split()
    scale = {"B": 1 / 1024, "KiB": 1, "MiB": 1024, "GiB": 1 << 20}.get(unit[0] if unit else "B", 1)
    return int(float(number) * scale)


def gpu_memory(pid: int) -> tuple[str, int]:
    """The renderer's GPU memory in KiB: DRM fdinfo drm-total-* (i915, one sum per drm-client-id), else
    drm-resident-*, else the process RSS."""
    totals: dict[str, dict[str, int]] = {}
    for info in pathlib.Path(f"/proc/{pid}/fdinfo").glob("*"):
        try:
            fields = dict(line.split(":", 1) for line in info.read_text().splitlines() if ":" in line)
        except (OSError, ValueError):
            continue
        if "drm-client-id" in fields:
            client = totals.setdefault(fields["drm-client-id"].strip(), {"total": 0, "resident": 0})
            client["total"] = sum(_kib(v) for k, v in fields.items() if k.startswith("drm-total-"))
            client["resident"] = sum(_kib(v) for k, v in fields.items() if k.startswith("drm-resident-"))
    for kind in ("total", "resident"):
        if any(c[kind] for c in totals.values()):
            return f"drm-{kind}", sum(c[kind] for c in totals.values())
    try:
        status = pathlib.Path(f"/proc/{pid}/status").read_text()
    except OSError:
        return "rss", 0
    return "rss", next((int(line.split()[1]) for line in status.splitlines() if line.startswith("VmRSS:")), 0)


class LogTail:
    """New renderer log lines with the monotonic time they were seen (the log itself has no clock)."""

    def __init__(self, path: pathlib.Path):
        self.path, self.offset, self.pending = path, 0, ""
        self.ladder: list[tuple[float, str]] = []

    def poll(self) -> None:
        try:
            with self.path.open() as f:
                f.seek(self.offset)
                text = f.read()
                self.offset = f.tell()
        except OSError:
            return
        *lines, self.pending = (self.pending + text).split("\n")
        now = time.monotonic()
        self.ladder += [(now, line.removeprefix("Canvas: ladder ")) for line in lines if line.startswith("Canvas: ladder ")]


class Churn:
    """--churn: one extra 1080p client opens (parked) at CHURN_OPEN s of Work and closes at CHURN_CLOSE s;
    the window list is rewritten both times, and the user's window gets focus back after the spawn."""

    def __init__(self, mailbox: Mailbox, stage: str | None, user_window: str | None, sizes: Clients):
        self.mailbox, self.stage, self.user_window, self.sizes = mailbox, stage, user_window, sizes
        self.window: dict | None = None
        self.state = "waiting"

    def step(self, elapsed: float) -> bool:
        """True when the client opened or closed in this call."""
        if self.state == "waiting" and elapsed >= CHURN_OPEN:
            self.window = sc.spawn("churn0", "1920x1080")
            sc.place(self.window, "park")
            refocus(self.user_window)
            self.sizes[self.window["address"]] = (1920, 1080)
            self.mailbox.write(self.stage)
            self.state = "open"
            print(f"{elapsed:5.1f}s churn: opened {self.window['address']}", flush=True)
            return True
        if self.state == "open" and elapsed >= CHURN_CLOSE and self.window:
            os.kill(self.window["pid"], signal.SIGTERM)
            deadline = time.monotonic() + 5
            while any(c["address"] == self.window["address"] for c in sc.clients()) and time.monotonic() < deadline:
                time.sleep(0.05)
            self.mailbox.write(self.stage)
            self.state = "closed"
            print(f"{elapsed:5.1f}s churn: closed", flush=True)
            return True
        return False


@dataclass
class Run:
    """Reports from pose.sock.stats, tagged with the harness phase: work -> overview -> back -> done.
    A transition is sent right after a report, so the second report after it is steady. Each report also
    carries where the spike clients are (`_clients`) and the renderer's GPU memory (`_memory`)."""
    seconds: float
    pose: Pose
    stats: pathlib.Path
    shots: pathlib.Path | None
    size: str
    renderer: int = 0
    churn: Churn | None = None
    started: float = field(default_factory=time.monotonic)
    phase: str = "work"
    since: int = 0
    reports: list[dict] = field(default_factory=list)
    marks: list[float] = field(default_factory=list)   # view changes the harness made (fit, churn)

    def elapsed(self) -> float:
        return time.monotonic() - self.started

    def poll(self) -> None:
        if self.churn and self.churn.step(self.elapsed()):
            self.marks.append(time.monotonic())
        try:
            report = json.loads(self.stats.read_text())
        except (OSError, json.JSONDecodeError):
            return
        if self.reports and report["time"] == self.reports[-1]["time"]:
            return
        report["phase"], report["settled"] = self.phase, len(self.reports) - self.since >= 1
        report["_clients"] = {int(c["address"], 16): (c["workspace"]["name"], c["at"][0]) for c in sc.clients()}
        report["_memory"] = gpu_memory(self.renderer) if self.renderer else ("rss", 0)
        self.reports.append(report)
        print(f'{self.elapsed():5.1f}s {report["canvasState"]:8} fps {report["fps"]:.1f} tiers {report["tiers"]}{budget_text(report)}', flush=True)
        self.advance(report["canvasState"])

    def switch(self, phase: str, want: str, state: str) -> None:
        if state != want:
            self.pose.send("fit")
            self.marks.append(time.monotonic())
        self.phase, self.since = phase, len(self.reports)

    def advance(self, state: str) -> None:
        after = len(self.reports) - self.since
        if self.phase == "work" and self.elapsed() >= OVERVIEW_AT:
            self.shot("work")
            self.switch("overview", "overview", state)
        elif self.phase == "overview" and after >= 3 and (self.steady() or after >= 8):
            self.shot("overview")
            self.switch("back", "work", state)
        elif self.phase == "back" and after >= 3 and self.elapsed() >= self.seconds and self.steady():
            self.phase = "done"

    def steady(self) -> bool:
        """The last three reports carry the same rates, so the fps of the last two was measured at them
        (after a view change the budget may still admit windows one by one)."""
        rates = [{w["output"]: w["rateHz"] for w in r["captures"]} for r in self.reports[-3:]]
        return rates[0] == rates[1] == rates[2]

    def shot(self, name: str) -> None:
        """The renderer window only (bottom-right of SPIKE-canvas, see float_renderer)."""
        if self.shots:
            self.shots.mkdir(parents=True, exist_ok=True)
            w, h = (int(v) for v in self.size.split("x"))
            region = f"{sc.OUT_X + sc.OUT_W - w},{sc.OUT_H - h} {w}x{h}"
            subprocess.run(["grim", "-g", region, str(self.shots / f"{name}.png")], check=False)


def budget_text(report: dict) -> str:
    """The M5 budget block, when the renderer writes one (used/effective Mpix/s, slivers)."""
    b = report.get("budget")
    if not b:
        return ""
    return f' budget {b["usedMpix"]:.0f}/{b["effectiveMpix"]:.0f} Mpix/s slivers {b["slivers"]}'


def windows_by_tier(report: dict) -> dict[str, list[dict]]:
    tiers: dict[str, list[dict]] = {}
    for w in report["captures"]:
        tiers.setdefault(w["tier"], []).append(w)
    return tiers


def model_windows(report: dict, sizes: Clients) -> list[ladder.Window]:
    """The report's windows as ladder inputs. The rank proxy is the native width (equal for equal window
    sizes, like the renderer's angular width on the ring); ties among equal sizes are the renderer's own
    Near choice (its hysteresis and MRU), so the renderer's tier only breaks ties. A window without a frame
    yet takes its client size."""
    out = []
    for w in report["captures"]:
        width, height = (w["nativeWidth"], w["nativeHeight"]) if w["nativeWidth"] else sizes.get(w["output"], (1, 1))
        out.append(ladder.Window(w["output"], width, height, staged=w["tier"] == "focused", visible=w["visible"],
                                 angular=float(width), mru=TIER_RANK.get(w["tier"], 3),
                                 demand_w=max(1, w["width"]), demand_h=max(1, w["height"])))
    return out


def fps_failure(w: dict) -> str | None:
    """Focused >= 58 fps, idle <= 0.5 fps, every other window within 1 fps of its rate (no dropped captures;
    a client drawing slower, such as a 30 fps video, is still delivered at the rate, parked or sliver)."""
    rate, fps = w["rateHz"], w["fps"]
    ok = fps >= 58 if w["tier"] == "focused" else fps <= 0.5 if not rate else abs(fps - rate) <= 1
    return None if ok else f'{w["tier"]} {w["output"]} {rate} Hz at {fps:.1f} fps'


def place_failure(w: dict, report: dict) -> str | None:
    """A sliver must really live on the canvas workspace at the right edge, a parked window on the park one."""
    where = report["_clients"].get(int(w["output"], 16))
    if not where or w["place"] == "stage":
        return None
    workspace, x = where
    if w["place"] == "sliver" and (workspace != "spikecanvas" or x != sc.OUT_X + sc.OUT_W - sc.SLIVER):
        return f'sliver {w["output"]} is on {workspace} at x {x}'
    if w["place"] == "park" and workspace != "spikepark":
        return f'parked {w["output"]} is on {workspace}'
    return None


def model_failures(report: dict, sizes: Clients, zoomed_out: bool) -> list[str]:
    """Every window at the settled ladder's tier, rate, lanes and place for the renderer's effective budget."""
    budget = report.get("budget") or {}
    if not budget:
        return ["report has no budget block"]
    want = ladder.plan(model_windows(report, sizes), zoomed_out, budget["effectiveMpix"], panic=budget["panic"])
    out = [] if budget["usedMpix"] <= budget["effectiveMpix"] + 1e-6 else [f'used {budget["usedMpix"]:.1f} > {budget["effectiveMpix"]:.1f} Mpix/s']
    for w in report["captures"]:
        d = want[w["output"]]
        got = (w["tier"], w["rateHz"], w["inFlight"], w["place"])
        if got != (d.tier, d.rate_hz, d.in_flight, d.place):
            out.append(f'{w["output"]} {"/".join(map(str, got))}, model {d.tier}/{d.rate_hz}/{d.in_flight}/{d.place}')
        out += [f for f in (fps_failure(w), place_failure(w, report)) if f]
    return out


def work_failures(report: dict, staged: bool, sizes: Clients) -> list[str]:
    focused = len(windows_by_tier(report).get("focused", []))
    out = [] if report["canvasState"] == "work" else [f'work report in state {report["canvasState"]}']
    out += [f"focused windows: {focused}"] if staged and focused != 1 else []
    return out + model_failures(report, sizes, zoomed_out=False)


def view_failures(report: dict) -> list[str]:
    """--all-in-view: every window must be in view, so the run exercises the S1c row for its window count."""
    out = [w["output"] for w in report["captures"] if not w.get("visible")]
    return [f"{len(out)} windows out of view ({', '.join(out)}); widen --fov/--size"] if out else []


def overview_failures(report: dict, sizes: Clients) -> list[str]:
    """Visible windows at the overview ladder rate; the staged window keeps 60 (§4.4 Focused: always 60 Hz)."""
    out = [] if report["canvasState"] == "overview" else [f'overview report in state {report["canvasState"]}']
    return out + model_failures(report, sizes, zoomed_out=True)


def table(label: str, report: dict) -> None:
    for tier, ws in sorted(windows_by_tier(report).items()):
        fps = [w["fps"] for w in ws]
        rate = sorted({w["rateHz"] for w in ws})
        places = "/".join(sorted({w["place"] for w in ws}))
        print(f"| {label} | {tier} | {len(ws)} | {'/'.join(map(str, rate))} | {min(fps):.1f} | {sum(fps) / len(fps):.1f} | {places} |")
    b = report.get("budget") or {}
    if b:
        print(f'| {label} | budget | used {b["usedMpix"]:.1f} / effective {b["effectiveMpix"]:.1f} (set {b["setMpix"]:.0f}, '
              f'calibration {b["calibration"]:.2f}, gpu steps {b["gpuSteps"]}{", panic" if b["panic"] else ""}) | '
              f'request->ready p50 {b["readyP50Ms"]:.1f} ms | VRAM ~{b["vramMB"]:.0f} MB | slivers {b["slivers"]} | |')


def window_lines(report: dict, titles: dict[str, str]) -> None:
    """Per-window rows for small sets (which client is Near, a sliver, idle)."""
    if len(report["captures"]) <= 12:
        print("  " + ", ".join(f'{titles.get(w["output"], w["output"])} {w["tier"]} {w["rateHz"]} Hz {w["fps"]:.1f} fps {w["place"]}'
                               for w in report["captures"]))


def ladder_raises(lines: list[tuple[float, str]]) -> list[str]:
    """`Canvas: ladder` lines (logged once per change): a tier's step raised twice within 2 s is oscillation.
    A tier that leaves the summary forgets its step, so coming back (Overview -> Work) is no raise."""
    last: dict[str, int] = {}
    raised: dict[str, float] = {}
    out = []
    for t, text in lines:
        steps = {} if text == "focused only" else {p.split()[0]: int(p.split()[1]) for p in text.split(", ")}
        for tier, hz in steps.items():
            if tier in last and hz > last[tier]:
                if t - raised.get(tier, -1e9) < 2:
                    out.append(f"ladder {tier} raised twice within {t - raised[tier]:.1f} s (to {hz} Hz)")
                raised[tier] = t
        last = steps
    return out


def memory_failures(first: dict, last: dict) -> list[str]:
    """GPU memory (fdinfo, else RSS) and the governor's VRAM estimate between two reports; growth fails."""
    (kind, a), (_, b) = first["_memory"], last["_memory"]
    va, vb = first.get("budget", {}).get("vramMB", 0), last.get("budget", {}).get("vramMB", 0)
    print(f"Renderer {kind} {a / 1024:.0f} -> {b / 1024:.0f} MB, governor VRAM estimate {va:.0f} -> {vb:.0f} MB")
    return [f"renderer {kind} grew {a / 1024:.0f} -> {b / 1024:.0f} MB"] if b > a * 1.1 + 32 * 1024 else []


@dataclass
class Observed:
    """What the run saw besides the reports: Hyprland CPU, the renderer log, screencast events, churn."""
    cpu: float
    log: str
    screencasts: list[tuple[float, str]]
    ladder: list[tuple[float, str]]
    churn: bool
    all_in_view: bool = False


def verdict(run: Run, groups: list[Group], seen: Observed, sizes: Clients) -> list[str]:
    staged = any(g.staged for g in groups)
    back = [r for r in run.reports if r["phase"] == "back" and r["settled"]]
    work, overview = back[-2:], [r for r in run.reports if r["phase"] == "overview" and r["settled"]][-1:]
    print("| state | tier | windows | rate Hz | fps min | fps avg | place |\n|---|---|---|---|---|---|---|")
    for r in overview:
        table("overview", r)
    for i, r in enumerate(work):
        table(f"work {i + 1}", r)
    titles = {m["address"]: m["title"] for g in groups for m in g.members}
    for r in overview + work[-1:]:
        window_lines(r, titles)
    print(f"Hyprland CPU {seen.cpu:.1f} %, renderer present fps " + ", ".join(f'{r["fps"]:.1f}' for r in run.reports[-3:])
          + f", steady-state screencast events (after {SCREENCAST_AFTER:.0f} s, outside view changes): {len(seen.screencasts)}")
    failures = [] if len(work) == 2 else [f"{len(work)} settled Work reports after Overview"]
    failures += [] if overview else ["no settled Overview report"]
    for r in work:
        failures += work_failures(r, staged, sizes) + (view_failures(r) if seen.all_in_view else [])
    for r in overview:
        failures += overview_failures(r, sizes)
    failures += memory_failures(back[0], run.reports[-1]) if back else []
    failures += ladder_raises(seen.ladder) if seen.churn else []
    if len(seen.screencasts) > SCREENCAST_MAX:
        failures.append(f"{len(seen.screencasts)} steady-state screencast events (> {SCREENCAST_MAX})")
    if seen.cpu > 20:
        failures.append(f"Hyprland CPU {seen.cpu:.1f} % > 20 %")
    if "OpenGL rendering error" in seen.log:
        failures.append("renderer log has an OpenGL rendering error")
    return failures


def drive(run: Run, renderer: subprocess.Popen, log: pathlib.Path, tail: LogTail) -> None:
    deadline = run.seconds + 60
    while run.phase != "done":
        if renderer.poll() is not None:
            raise RuntimeError(f"renderer exited {renderer.returncode}\n{log.read_text()}")
        if run.elapsed() > deadline:
            raise RuntimeError(f"no settled Work reports after {deadline:.0f} s")
        run.poll()
        tail.poll()
        time.sleep(0.01)


def stop(renderer: subprocess.Popen | None) -> int | None:
    if not renderer:
        return None
    renderer.terminate()
    try:
        return renderer.wait(timeout=10)
    except subprocess.TimeoutExpired:
        renderer.kill()
        return renderer.wait()


def measure(args: argparse.Namespace, temp: pathlib.Path, groups: list[Group]) -> list[str]:
    stage = next((g.members[0]["address"] for g in groups if g.staged), None)
    sizes: Clients = {m["address"]: (int(m["size"].split("x")[0]), int(m["size"].split("x")[1])) for g in groups for m in g.members}
    mailbox, log, pose = Mailbox(temp / "windows.tsv"), temp / "renderer.log", Pose(temp / "pose.sock")
    mailbox.write(stage)
    extra = ["--pose-socket", str(temp / "pose.sock"), *(["--stereo"] if args.stereo else []),
             *(["--fov", str(args.fov)] if args.fov else [])]
    hz, hyprland = os.sysconf("SC_CLK_TCK"), sc.hyprland_pid()
    before, started = sc.cpu_ticks(hyprland), time.monotonic()
    renderer, tiers, casts, tail = None, Tiers(temp / "pose.sock.controls.tiers", stage, mailbox), Screencasts(), LogTail(log)
    try:
        with log.open("w") as out:
            renderer = launch(temp, extra, out)
        float_renderer(renderer.pid, args.size)
        refocus(args.user_window)
        pose.thread.start()
        tiers.thread.start()
        churn = Churn(mailbox, stage, args.user_window, sizes) if args.churn else None
        run = Run(args.seconds, pose, pathlib.Path(str(temp / "pose.sock") + ".stats"), args.shots, args.size, renderer.pid, churn)
        drive(run, renderer, log, tail)
        cpu = 100 * (sc.cpu_ticks(hyprland) - before) / hz / (time.monotonic() - started)
    finally:
        code = stop(renderer)
        pose.close()
        tiers.close()
        casts.close()
    tail.poll()
    seen = Observed(cpu, log.read_text(), casts.after(run.started + SCREENCAST_AFTER, tuple(run.marks)), tail.ladder, args.churn, args.all_in_view)
    print(f"Tiers stand-in: {tiers.moves} sliver/park moves; screencast events: {len(casts.after(0))} in the whole run ("
          + ", ".join(f"{t - run.started:.1f}s {text.removeprefix('screencast>>')}" for t, text in casts.after(0))
          + "); view changes at " + ", ".join(f"{m - run.started:.1f}s" for m in run.marks))
    failures = verdict(run, groups, seen, sizes)
    return failures + ([] if code == 0 else [f"renderer exit {code}"])


def profile(args: argparse.Namespace) -> int:
    args.user_window = active_window()
    try:
        with tempfile.TemporaryDirectory(prefix="xr-canvas-") as directory:
            if args.dir:
                args.dir.mkdir(parents=True, exist_ok=True)
                for stale in ("canvas-memory.tsv", "pose.sock.stats"):
                    (args.dir / stale).unlink(missing_ok=True)
            sc.setup()
            groups = spawn_profile(args.name)
            print(f"{args.name}: {sum(len(g.members) for g in groups)} windows, "
                  + ", ".join(f"{g.tier} {len(g.members)}" for g in groups), flush=True)
            time.sleep(2)
            failures = measure(args, args.dir or pathlib.Path(directory), groups)
    finally:
        cleanup(args.user_window)
    for failure in failures:
        print("FAIL", failure)
    print(f"{args.name}: " + ("passed" if not failures else f"{len(failures)} failures"))
    return 1 if failures else 0


def smoke(_args: argparse.Namespace) -> int:
    """make smoke-canvas: 1 staged + 4 parked clients, `--smoke-test --stereo --canvas` must exit 0 with region frames."""
    user_window = active_window()
    try:
        with tempfile.TemporaryDirectory(prefix="xr-canvas-smoke-") as directory:
            temp = pathlib.Path(directory)
            sc.setup()
            stage = sc.spawn("smoke-stage", "1280x720")
            sc.place(stage, "stage")
            for i in range(4):
                sc.place(sc.spawn(f"smoke-park{i}", "960x600", 30), "park", i)
            sc.activate_canvas()
            Mailbox(temp / "windows.tsv").write(stage["address"])
            with (temp / "renderer.log").open("w") as out:
                code = subprocess.run([*launch_cmd(temp), "--smoke-test", "--stereo"], stdout=out, stderr=subprocess.STDOUT,
                                      env=renderer_env(), timeout=60).returncode
            log = (temp / "renderer.log").read_text()
    finally:
        cleanup(user_window)
    print(log)
    # M3: the staged window must be served by the region source (finishCanvas prints its frame count).
    region = "(region " in log
    print(f"Canvas smoke: renderer exit {code}, region frames {'yes' if region else 'none'}")
    return 0 if code == 0 and region else 1


def write(args: argparse.Namespace) -> int:
    count = Mailbox(pathlib.Path(args.file)).write(args.stage, args.all)
    print(f"{args.file}: {count} windows")
    return 0


def watch(args: argparse.Namespace) -> int:
    mailbox = Mailbox(pathlib.Path(args.file))
    try:
        while True:
            mailbox.write(args.stage, args.all)
            time.sleep(0.5)
    except KeyboardInterrupt:
        return 0


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--smoke", action="store_true", help="make smoke-canvas")
    sub = p.add_subparsers(dest="cmd")
    for name in ("write", "watch"):
        s = sub.add_parser(name)
        s.add_argument("file")
        s.add_argument("--stage", help="address of the staged row (default: the spike canvas workspace's last focus)")
        s.add_argument("--all", action="store_true", help="every client, not only the spike class")
    s = sub.add_parser("profile")
    s.add_argument("name", nargs="?", default=DEFAULT_PROFILE, help=", ".join(sc.PROFILES))
    s.add_argument("--seconds", type=float, default=40, help="minimum run length")
    s.add_argument("--stereo", action="store_true")
    s.add_argument("--size", default="1280x720", help="renderer window size on SPIKE-canvas")
    s.add_argument("--fov", type=float, help="renderer --fov (vertical degrees; more windows in view when wider)")
    s.add_argument("--all-in-view", action="store_true", help="fail when a settled Work report has a window out of view")
    s.add_argument("--churn", action="store_true", help=f"open a 1080p client at {CHURN_OPEN:.0f} s, close it at {CHURN_CLOSE:.0f} s")
    s.add_argument("--shots", type=pathlib.Path, help="save renderer screenshots (work, overview) here")
    s.add_argument("--dir", type=pathlib.Path, help="keep canvas.tsv, windows.tsv, the log and memory here")
    return p


def main() -> int:
    args = parser().parse_args()
    missing = [str(p) for p in (RENDERER, sc.CAPTURE) if not p.exists()]
    if missing and (args.smoke or args.cmd == "profile"):
        print("missing " + ", ".join(missing) + "; run make all spike-canvas", file=sys.stderr)
        return 2
    if args.smoke:
        return smoke(args)
    commands = {"write": write, "watch": watch, "profile": profile}
    if args.cmd not in commands:
        parser().print_help()
        return 2
    return commands[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
