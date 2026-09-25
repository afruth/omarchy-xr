#!/usr/bin/env python3
"""Opt-in live Window Canvas harness (docs/infinite-canvas-plan.md §7 M2); needs a Hyprland session.

    python3 tests/live_canvas.py write FILE [--stage ADDRESS] [--all]   one .windows mailbox (§3.1)
    python3 tests/live_canvas.py watch FILE [--stage ADDRESS] [--all]   rewrite it every 500 ms
    python3 tests/live_canvas.py profile [NAME] [--seconds 40] [--stereo] [--size WxH] [--shots DIR]
    python3 tests/live_canvas.py --smoke                                 make smoke-canvas

`profile` reuses tools/spike_canvas.py: a headless SPIKE-canvas output, the profile's test clients (the
staged tier on the canvas workspace, every other tier parked), the renderer windowed on that output with
`--canvas`, a steady pose, `fit` into Overview after ~20 s and back to Work two reports later, then
asserts the S1b rates from `pose.sock.stats`. Only SPIKE-canvas is created or removed; the renderer never
runs with --direct/--display, and the previously active window gets focus back.
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
import spike_canvas as sc  # noqa: E402

RENDERER = ROOT / "build" / "omarchy-xr"
EXCLUDED = {"omarchy-xr-spectator", "omarchy-xr-search"}
DEFAULT_PROFILE = "zoomed-in-near-parked"
OVERVIEW_AT = 20.0
BUDGET_MPIX = 300.0   # canvas.tsv default captureBudgetMpix


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
    place = "stage" if c["address"] == stage else "park" if c["workspace"]["name"] == "spikepark" else "off"
    (w, h), (x, y) = c["size"], c["at"]
    return (f'{c["address"]} {hex_text(c["class"])} {hex_text(c["title"])} {w} {h} {x} {y} {c["focusHistoryID"]} '
            f'{place} {int(c["floating"])} {c["pid"]} {int(c["xwayland"])} 1')


class Mailbox:
    """Writes `v1 <pid> <seq> <boottime s> <outX> <outY> <output>` plus 13-field rows with tmp+rename; seq only grows."""

    def __init__(self, path: pathlib.Path):
        self.path, self.seq = path, 0

    def write(self, stage: str | None = None, everyone: bool = False) -> int:
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


@dataclass
class Run:
    """Reports from pose.sock.stats, tagged with the harness phase: work -> overview -> back -> done.
    A transition is sent right after a report, so the second report after it is steady."""
    seconds: float
    pose: Pose
    stats: pathlib.Path
    shots: pathlib.Path | None
    size: str
    started: float = field(default_factory=time.monotonic)
    phase: str = "work"
    since: int = 0
    reports: list[dict] = field(default_factory=list)

    def elapsed(self) -> float:
        return time.monotonic() - self.started

    def poll(self) -> None:
        try:
            report = json.loads(self.stats.read_text())
        except (OSError, json.JSONDecodeError):
            return
        if self.reports and report["time"] == self.reports[-1]["time"]:
            return
        report["phase"], report["settled"] = self.phase, len(self.reports) - self.since >= 1
        self.reports.append(report)
        print(f'{self.elapsed():5.1f}s {report["canvasState"]:8} fps {report["fps"]:.1f} tiers {report["tiers"]}', flush=True)
        self.advance(report["canvasState"])

    def switch(self, phase: str, want: str, state: str) -> None:
        if state != want:
            self.pose.send("fit")
        self.phase, self.since = phase, len(self.reports)

    def advance(self, state: str) -> None:
        after = len(self.reports) - self.since
        if self.phase == "work" and self.elapsed() >= OVERVIEW_AT:
            self.shot("work")
            self.switch("overview", "overview", state)
        elif self.phase == "overview" and after >= 2:
            self.shot("overview")
            self.switch("back", "work", state)
        elif self.phase == "back" and after >= 3 and self.elapsed() >= self.seconds:
            self.phase = "done"

    def shot(self, name: str) -> None:
        """The renderer window only (bottom-right of SPIKE-canvas, see float_renderer)."""
        if self.shots:
            self.shots.mkdir(parents=True, exist_ok=True)
            w, h = (int(v) for v in self.size.split("x"))
            region = f"{sc.OUT_X + sc.OUT_W - w},{sc.OUT_H - h} {w}x{h}"
            subprocess.run(["grim", "-g", region, str(self.shots / f"{name}.png")], check=False)


def windows_by_tier(report: dict) -> dict[str, list[dict]]:
    tiers: dict[str, list[dict]] = {}
    for w in report["captures"]:
        tiers.setdefault(w["tier"], []).append(w)
    return tiers


def work_failures(report: dict, staged: bool) -> list[str]:
    tiers, out = windows_by_tier(report), []
    if report["canvasState"] != "work":
        out.append(f'work report in state {report["canvasState"]}')
    if staged and len(tiers.get("focused", [])) != 1:
        out.append(f'focused windows: {len(tiers.get("focused", []))}')
    rules = {"focused": (58, 1e9), "near": (23, 1e9), "far": (9, 11), "idle": (0, 0.5)}
    for tier, (low, high) in rules.items():
        for w in tiers.get(tier, []):
            if not low <= w["fps"] <= high or (tier == "idle" and w["rateHz"]):
                out.append(f'{tier} {w["output"]} {w["rateHz"]} Hz at {w["fps"]:.1f} fps')
    return out


def overview_rate(visible: list[dict]) -> int:
    pixels = sum(w["nativeWidth"] * w["nativeHeight"] for w in visible)
    return 10 if pixels * 10 <= BUDGET_MPIX * 1e6 else 6


def overview_failures(report: dict) -> list[str]:
    """Every visible window at the overview rate; the staged window keeps 60 (§4.4 Focused: always 60 Hz)."""
    visible = [w for w in report["captures"] if w["visible"] and w["tier"] != "focused"]
    out = [] if report["canvasState"] == "overview" else [f'overview report in state {report["canvasState"]}']
    out += [f'focused {w["output"]} at {w["fps"]:.1f} fps' for w in report["captures"] if w["tier"] == "focused" and w["fps"] < 58]
    rate = overview_rate(visible)
    for w in visible:
        if w["tier"] != "overview" or w["rateHz"] != rate or abs(w["fps"] - rate) > 1:
            out.append(f'overview {w["output"]} {w["tier"]} {w["rateHz"]} Hz at {w["fps"]:.1f} fps (want {rate})')
    return out


def table(label: str, report: dict) -> None:
    for tier, ws in sorted(windows_by_tier(report).items()):
        fps = [w["fps"] for w in ws]
        rate = sorted({w["rateHz"] for w in ws})
        print(f"| {label} | {tier} | {len(ws)} | {'/'.join(map(str, rate))} | {min(fps):.1f} | {sum(fps) / len(fps):.1f} |")


def verdict(run: Run, groups: list[Group], cpu: float, log: str) -> list[str]:
    staged = any(g.staged for g in groups)
    work = [r for r in run.reports if r["phase"] == "back" and r["settled"]][-2:]
    overview = [r for r in run.reports if r["phase"] == "overview" and r["settled"]][-1:]
    print("| state | tier | windows | rate Hz | fps min | fps avg |\n|---|---|---|---|---|---|")
    for r in overview:
        table("overview", r)
    for i, r in enumerate(work):
        table(f"work {i + 1}", r)
    print(f"Hyprland CPU {cpu:.1f} %, renderer present fps "
          + ", ".join(f'{r["fps"]:.1f}' for r in run.reports[-3:]))
    failures = [] if len(work) == 2 else [f"{len(work)} settled Work reports after Overview"]
    failures += [] if overview else ["no settled Overview report"]
    for r in work:
        failures += work_failures(r, staged)
    for r in overview:
        failures += overview_failures(r)
    if cpu > 20:
        failures.append(f"Hyprland CPU {cpu:.1f} % > 20 %")
    if "OpenGL rendering error" in log:
        failures.append("renderer log has an OpenGL rendering error")
    return failures


def drive(run: Run, renderer: subprocess.Popen, log: pathlib.Path) -> None:
    deadline = run.seconds + 40
    while run.phase != "done":
        if renderer.poll() is not None:
            raise RuntimeError(f"renderer exited {renderer.returncode}\n{log.read_text()}")
        if run.elapsed() > deadline:
            raise RuntimeError(f"no settled Work reports after {deadline:.0f} s")
        run.poll()
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
    Mailbox(temp / "windows.tsv").write(stage)
    log, pose = temp / "renderer.log", Pose(temp / "pose.sock")
    extra = ["--pose-socket", str(temp / "pose.sock"), *(["--stereo"] if args.stereo else [])]
    hz, hyprland = os.sysconf("SC_CLK_TCK"), sc.hyprland_pid()
    before, started = sc.cpu_ticks(hyprland), time.monotonic()
    renderer = None
    try:
        with log.open("w") as out:
            renderer = launch(temp, extra, out)
        float_renderer(renderer.pid, args.size)
        refocus(args.user_window)
        pose.thread.start()
        run = Run(args.seconds, pose, pathlib.Path(str(temp / "pose.sock") + ".stats"), args.shots, args.size)
        drive(run, renderer, log)
        cpu = 100 * (sc.cpu_ticks(hyprland) - before) / hz / (time.monotonic() - started)
    finally:
        code = stop(renderer)
        pose.close()
    failures = verdict(run, groups, cpu, log.read_text())
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
