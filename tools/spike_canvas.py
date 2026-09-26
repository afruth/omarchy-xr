#!/usr/bin/env python3
"""Window canvas M0 spike driver (docs/infinite-canvas-plan.md §3.6).

Creates a headless SPIKE-canvas output with a canvas and a park workspace, spawns animated
test clients, arranges them as stage/pile/park windows through the Hyprland Lua API, and runs
build/spike-window-capture against them while sampling CPU of Hyprland and every client.

    tools/spike_canvas.py setup
    tools/spike_canvas.py s1 [--seconds 20] [--park-pile]
    tools/spike_canvas.py s2
    tools/spike_canvas.py teardown

Nothing here touches OMXR outputs, layout.json or the Studio journal.
"""
import argparse
import json
import os
import pathlib
import signal
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
CAPTURE = ROOT / "build" / "spike-window-capture"
OUTPUT = "SPIKE-canvas"
OUT_X, OUT_W, OUT_H = 20000, 2560, 1440
SLIVER = 8
SLIVER_FLOOR = 64  # the sliver stack stops this far above the bottom edge (Lua's SLIVER_FLOOR)
REFRESH = int(os.environ.get("SPIKE_REFRESH", "60"))
CLASS = "omarchy-xr-spike-client"
PULL = set()  # addresses (without 0x) captured with ignore_damage=1
STATE = pathlib.Path(os.environ.get("SPIKE_STATE", "/tmp/omarchy-xr-spike"))
# Reused by tests/live_canvas.py (the M2 harness); keep these names and their behaviour stable.
__all__ = ["CLASS", "OUTPUT", "OUT_X", "OUT_W", "OUT_H", "PROFILES", "SLIVER", "STATE", "activate_canvas", "clients", "cpu_ticks",
           "hyprctl", "hyprland_pid", "lua", "monitors", "place", "setup", "sliver", "spawn", "teardown", "unsliver"]


def hyprctl(*args):
    result = subprocess.run(["hyprctl", *args], capture_output=True, text=True, check=False)
    out = result.stdout.strip()
    if result.returncode or out.startswith("Error"):
        raise RuntimeError(f"hyprctl {' '.join(args)}: {out} {result.stderr.strip()}")
    return out


def lua(code):
    return hyprctl("eval", code)


def clients():
    return [c for c in json.loads(hyprctl("clients", "-j")) if c["class"] == CLASS]


def monitors():
    return json.loads(hyprctl("monitors", "-j"))


def setup():
    STATE.mkdir(parents=True, exist_ok=True)
    if not any(m["name"] == OUTPUT for m in monitors()):
        hyprctl("output", "create", "headless", OUTPUT)
        time.sleep(0.5)
    lua(f'hl.monitor({{output="{OUTPUT}", mode="{OUT_W}x{OUT_H}@{REFRESH}", position="{OUT_X}x0", scale=1}})')
    lua(f'hl.workspace_rule({{workspace="name:spikecanvas", monitor="{OUTPUT}", persistent=true}}) '
        f'hl.workspace_rule({{workspace="name:spikepark", monitor="{OUTPUT}", persistent=true}})')
    lua(f'hl.window_rule({{match={{class="{CLASS}"}}, float=true, workspace="name:spikecanvas silent", '
        'no_anim=true, border_size=0, no_shadow=true, no_blur=true, rounding=0, no_dim=true, '
        'suppress_event="fullscreen maximize"})')
    activate_canvas()


def activate_canvas():
    """Show the canvas workspace on the spike output, then give focus back."""
    focused = next((m for m in monitors() if m["focused"]), None)
    back = focused["activeWorkspace"]["name"] if focused else None
    lua('hl.dispatch(hl.dsp.focus({workspace="name:spikecanvas"}))')
    if back:
        lua(f'hl.dispatch(hl.dsp.focus({{workspace={json.dumps(back)}}}))')


def spawn(title, size, fps=0):
    log = open(STATE / f"client-{title}.log", "w")
    proc = subprocess.Popen([str(CAPTURE), "--client", size, title, *([str(fps)] if fps else [])], stdout=log,
                            stderr=subprocess.STDOUT, start_new_session=True)
    for _ in range(100):
        for c in clients():
            if c["title"] == title and c["pid"] == proc.pid:
                return {"title": title, "address": c["address"], "pid": proc.pid, "size": size}
        time.sleep(0.05)
    raise RuntimeError(f"client {title} did not map")


def place(window, tier, index=0):
    addr = window["address"]
    w, h = (int(v) for v in window["size"].split("x"))
    if tier == "park":
        lua(f'hl.dispatch(hl.dsp.window.move({{window="address:{addr}", workspace="name:spikepark", follow=false}}))')
        return
    if tier == "canvas-restore":
        lua(f'hl.dispatch(hl.dsp.window.move({{window="address:{addr}", workspace="name:spikecanvas", follow=false}}))')
        tier = "pile"
    x = OUT_X if tier == "stage" else OUT_X + OUT_W - SLIVER
    y = 0 if tier == "stage" else min(OUT_H - SLIVER_FLOOR, index * 24)
    if tier == "stage":
        info = next((c for c in clients() if c["address"] == addr), None)
        if staged(info, (w, h), (x, y)):
            return
        ensure_on_canvas(info)
    lua(f'hl.dispatch(hl.dsp.window.resize({{window="address:{addr}", x={w}, y={h}}})) '
        f'hl.dispatch(hl.dsp.window.move({{window="address:{addr}", x={x}, y={y}}}))')


def sliver(address, index):
    """M5 sliver (Lua's applyTiers): the 8 px strip at the right edge of the canvas workspace, stacked 24 px
    apart, with no_follow_mouse so the pointer never focuses it."""
    w = f"address:{address}"
    lua(f'hl.dispatch(hl.dsp.window.move({{window="{w}", workspace="name:spikecanvas", follow=false}})) '
        f'hl.dispatch(hl.dsp.window.move({{window="{w}", x={OUT_X + OUT_W - SLIVER}, y={min(OUT_H - SLIVER_FLOOR, index * 24)}}})) '
        f'hl.dispatch(hl.dsp.window.set_prop({{window="{w}", prop="no_follow_mouse", value="1"}}))')


def unsliver(address):
    """Back to the park workspace, no_follow_mouse unset (Lua's park path)."""
    w = f"address:{address}"
    lua(f'hl.dispatch(hl.dsp.window.move({{window="{w}", workspace="name:spikepark", follow=false}})) '
        f'hl.dispatch(hl.dsp.window.set_prop({{window="{w}", prop="no_follow_mouse", value="unset"}}))')


def staged(info, size, at):
    """True when the client already sits on the canvas workspace at the stage rect (place() is idempotent)."""
    return bool(info) and info["workspace"]["name"] == "spikecanvas" and tuple(info["size"]) == size and tuple(info["at"]) == at


def ensure_on_canvas(info):
    """Move the client to the canvas workspace without following it."""
    if info and info["workspace"]["name"] != "spikecanvas":
        addr = info["address"]
        lua(f'hl.dispatch(hl.dsp.window.move({{window="address:{addr}", workspace="name:spikecanvas", follow=false}}))')


def cpu_ticks(pid):
    try:
        fields = pathlib.Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return int(fields[11]) + int(fields[12])
    except (OSError, IndexError, ValueError):
        return 0


def hyprland_pid():
    return int(subprocess.run(["pgrep", "-x", "Hyprland"], capture_output=True, text=True).stdout.split()[0])


def client_fps(title, since):
    rows = []
    for line in (STATE / f"client-{title}.log").read_text().splitlines():
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if row.get("t", 0) >= since:
            rows.append(row["fps"])
    return round(sum(rows) / len(rows), 1) if rows else None


def client_clock(title):
    """Seconds the client has been running according to its own log (last line)."""
    lines = (STATE / f"client-{title}.log").read_text().splitlines()
    for line in reversed(lines):
        try:
            return json.loads(line)["t"]
        except (json.JSONDecodeError, KeyError):
            continue
    return 0


def measure(targets, seconds, render, extra=()):
    """Run the capture tool; return its summary, per-second rows and CPU percentages."""
    hz = os.sysconf("SC_CLK_TCK")
    watch = {"hyprland": hyprland_pid()}
    cmd = [str(CAPTURE), "--seconds", str(seconds), *extra]
    if render:
        cmd += ["--render", str(render)]
    cmd += [f'{t[0]["address"].removeprefix("0x")}:{t[1]}' + (t[2] if len(t) > 2 else "") for t in targets]
    cmd = [c + ":pull" if c.split(":")[0] in PULL else c for c in cmd]
    before = {k: cpu_ticks(p) for k, p in watch.items()}
    started = time.monotonic()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    watch["capture"] = proc.pid
    before["capture"] = 0
    out, err = proc.communicate()
    elapsed = time.monotonic() - started
    after = {k: cpu_ticks(p) for k, p in watch.items() if k != "capture"}
    rows = [json.loads(line) for line in out.splitlines() if line.startswith("{")]
    summary = next((r for r in rows if r.get("summary")), None)
    cpu = {k: round(100 * (after[k] - before[k]) / hz / elapsed, 1) for k in after}
    return {"summary": summary, "rows": [r for r in rows if not r.get("summary")], "cpu": cpu, "stderr": err.strip()}


def clients_cpu(windows, seconds):
    hz = os.sysconf("SC_CLK_TCK")
    before = {w["title"]: cpu_ticks(w["pid"]) for w in windows}
    time.sleep(seconds)
    return {t: round(100 * (cpu_ticks(w["pid"]) - before[t]) / hz / seconds, 1)
            for w in windows for t in [w["title"]]}


def s1(args):
    setup()
    hot = [spawn(f"hot{i}", "1920x1080") for i in range(args.hot)]
    warm = [spawn(f"warm{i}", "1280x720", args.warm_fps) for i in range(args.warm)]
    pile = [spawn(f"pile{i}", "960x600", args.pile_fps) for i in range(args.pile)]
    if hot:
        place(hot[0], "stage")
    for i, w in enumerate(hot[1:] + warm + pile):
        place(w, "pile", i)
    if args.park_pile:
        for w in pile:
            place(w, "park")
    if args.park_warm:
        for w in warm:
            place(w, "park")
            PULL.add(w["address"].removeprefix("0x"))
    activate_canvas()
    time.sleep(3)
    everyone = hot + warm + pile
    result = {}
    cpu_thread = {}

    def sample():
        cpu_thread["clients"] = clients_cpu(everyone, max(1, args.seconds - 2))

    thread = threading.Thread(target=sample)
    thread.start()
    since = {w["title"]: client_clock(w["title"]) for w in everyone}
    result["capture"] = measure([(w, 60) for w in hot] + [(w, 10) for w in warm], args.seconds, args.render,
                                (["--pull"] if args.pull else []) + ["--inflight", str(args.inflight)])
    thread.join()
    result["client_cpu_total"] = {
        "hot": round(sum(cpu_thread["clients"][w["title"]] for w in hot), 1),
        "counts": [len(hot), len(warm), len(pile)],
        "warm": round(sum(cpu_thread["clients"][w["title"]] for w in warm), 1),
        "pile": round(sum(cpu_thread["clients"][w["title"]] for w in pile), 1),
    }
    result["client_fps"] = {w["title"]: client_fps(w["title"], since[w["title"]] + 1) for w in everyone}
    result["layout"] = ("park-pile" if args.park_pile else "pile-slivers") + ("+park-warm-pull" if args.park_warm else "")
    print(json.dumps(result, indent=1))


def s2(args):
    """Pile vs park: frame rate while parked and time to fresh frames after promotion."""
    setup()
    win = spawn("s2", "1280x720")
    place(win, "pile")
    activate_canvas()
    time.sleep(2)
    out = {"pile": measure([(win, 60)], 4, 0)["summary"]}
    place(win, "park")
    time.sleep(2)
    out["park"] = measure([(win, 60)], 4, 0)["summary"]
    out["park_client_fps"] = client_fps("s2", client_clock("s2") - 3)
    out["park_pull"] = measure([(win, 60)], 3, 0, ["--pull"])["summary"]
    # Promotion: start capturing, then move the window back mid-run; rows show the transition.
    proc = subprocess.Popen([str(CAPTURE), "--seconds", "4", f'{win["address"].removeprefix("0x")}:60'],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    time.sleep(1.5)
    moved = time.monotonic()
    place(win, "canvas-restore")
    stdout, _ = proc.communicate()
    out["promotion_rows"] = [json.loads(l) for l in stdout.splitlines() if l.startswith("{")]
    out["promotion_moved_at_s"] = 1.5
    out["client_fps_after"] = client_fps("s2", client_clock("s2") - 1.5)
    del moved
    print(json.dumps(out, indent=1))


def active_address():
    return json.loads(hyprctl("activewindow", "-j") or "{}").get("address")


def s3_s10(_args):
    """Stage a terminal, focus it by address, type into it, sweep the cursor over pile slivers,
    then ask it to go fullscreen with suppress_event active. Restores the user's focus."""
    setup()
    user_window = active_address()
    typed = STATE / "typed.txt"
    typed.unlink(missing_ok=True)
    proc = subprocess.Popen(["foot", f"--app-id={CLASS}", "--title=s3-stage", "sh", "-c", f"cat > {typed}"],
                            start_new_session=True)
    for _ in range(100):
        stage = next((c for c in clients() if c["title"] == "s3-stage"), None)
        if stage:
            break
        time.sleep(0.05)
    stage = {"title": "s3-stage", "address": stage["address"], "pid": proc.pid, "size": "1200x800"}
    pile = [spawn(f"s3pile{i}", "960x600") for i in range(3)]
    place(stage, "stage")
    for i, w in enumerate(pile):
        place(w, "pile", i)
        lua(f'hl.dispatch(hl.dsp.window.set_prop({{window="address:{w["address"]}", prop="no_follow_mouse", value="1"}}))')
    activate_canvas()
    out = {}
    events = STATE / "events.txt"
    events.unlink(missing_ok=True)
    lua('omarchy_xr_spike_events = { hl.on("window.fullscreen", function(w) local f=io.open('
        + json.dumps(str(events)) + ', "a") f:write("fullscreen\\n") f:close() end) }')
    # Focus by address, then warp into the window like the planned .hover v4 flow.
    t0 = time.monotonic()
    lua(f'hl.dispatch(hl.dsp.window.bring_to_top({{window="address:{stage["address"]}"}})) '
        f'hl.dispatch(hl.dsp.focus({{window="address:{stage["address"]}"}})) '
        f'hl.dispatch(hl.dsp.cursor.move({{x={OUT_X + 300}, y=300}}))')
    out["focus_ms"] = round((time.monotonic() - t0) * 1000, 1)
    out["focused_after_stage"] = active_address() == stage["address"]
    subprocess.run(["wtype", "hello-canvas\n"], check=False)
    time.sleep(0.3)
    out["typed"] = typed.read_text() if typed.exists() else None
    # Sweep the cursor across the pile slivers and the stage edge.
    changes = 0
    for step in range(60):
        x = OUT_X + OUT_W - SLIVER + (step % SLIVER)
        lua(f'hl.dispatch(hl.dsp.cursor.move({{x={x}, y={100 + step * 10}}}))')
        time.sleep(0.02)
        if active_address() != stage["address"]:
            changes += 1
    out["focus_lost_during_sweep"] = changes
    # S10: request fullscreen on the staged window.
    lua(f'hl.dispatch(hl.dsp.focus({{window="address:{stage["address"]}"}})) hl.dispatch(hl.dsp.window.fullscreen({{window="address:{stage["address"]}"}}))')
    time.sleep(0.3)
    info = next(c for c in clients() if c["address"] == stage["address"])
    out["fullscreen_after_dispatch"] = {"fullscreen": info.get("fullscreen"), "fullscreenClient": info.get("fullscreenClient"), "size": info["size"]}
    out["fullscreen_event_seen"] = events.exists() and "fullscreen" in events.read_text()
    lua("for _,s in ipairs(omarchy_xr_spike_events or {}) do s:remove() end omarchy_xr_spike_events=nil")
    if user_window:
        lua(f'hl.dispatch(hl.dsp.focus({{window="address:{user_window}"}}))')
    proc.terminate()
    print(json.dumps(out, indent=1))


PROFILES = {
    # name: count : client size : client draw fps (0 = vsync) : capture fps (0 = idle) : placement [: flags]
    # Zoomed in on one window: it runs at 60, up to four other visible windows at 24,
    # the remaining visible ones at 10, everything off-screen idle.
    "zoomed-in": "focus:1:1920x1080:0:60:stage:i2;near:4:1280x720:30:24:pile;far:6:1280x720:10:10:park:pull;"
                 "idle:39:960x600:2:0:park",
    # Same windows, but every client draws at vsync (worst case: video everywhere).
    "zoomed-in-stress": "focus:1:1920x1080:0:60:stage:i2;near:4:1280x720:0:24:pile;far:6:1280x720:0:10:park:pull;"
                        "idle:39:960x600:0:0:park",
    # Near tier parked and pulled too (only the focused window stays live on the canvas workspace).
    "zoomed-in-near-parked": "focus:1:1920x1080:0:60:stage:i2;near:4:1280x720:30:24:park:pull;"
                             "far:6:1280x720:10:10:park:pull;idle:39:960x600:2:0:park",
    # Overview: everything visible at 10 Hz, nothing is focused.
    # Normal working set, at most 6 windows (1080p). Rate ladder to find how high a small canvas can start.
    "w6-all60": "focus:1:1920x1080:0:60:stage:i2;other:5:1920x1080:0:60:pile:i2",
    "w6-30": "focus:1:1920x1080:0:60:stage:i2;other:5:1920x1080:0:30:pile:i2",
    "w6-30-parked": "focus:1:1920x1080:0:60:stage:i2;other:5:1920x1080:0:30:park:pull:i2",
    "w6-overview-60": "view:6:1920x1080:0:60:pile:i2",
    "w6-overview-30": "view:6:1920x1080:0:30:pile:i2",
    "zoomed-out": "view:30:1280x720:10:10:park:pull;idle:20:960x600:2:0:park",
    "zoomed-out-50": "view:50:1280x720:10:10:park:pull",
    "zoomed-out-stress": "view:30:1280x720:0:10:park:pull;idle:20:960x600:0:0:park",
    # M5 pixel-budget ladder (§4.4, S1c): a staged 1080p window and N-1 other 1080p windows, every client
    # drawing at vsync. The capture fps is the ladder's expectation for the other windows at the default
    # 300 Mpix/s budget: 2 -> 40 (a live sliver), 4 -> 24, 6 -> 15 near + 10 far, 11 -> 10 near + 6 far.
    # tests/live_canvas.py derives its verdict from tests/canvas_ladder.py, not from these numbers.
    "ladder-2": "focus:1:1920x1080:0:60:stage:i2;other:1:1920x1080:0:40:park:pull:i2",
    "ladder-4": "focus:1:1920x1080:0:60:stage:i2;other:3:1920x1080:0:24:park:pull:i2",
    "ladder-6": "focus:1:1920x1080:0:60:stage:i2;other:5:1920x1080:0:15:park:pull:i2",
    "ladder-11": "focus:1:1920x1080:0:60:stage:i2;other:10:1920x1080:0:10:park:pull:i2",
    # The 30 fps video case: a 1080p client drawing at 30 fps next to one other 1080p window; the two share
    # Near at 40 Hz as live slivers, so every video frame is captured. (A smaller video window ranks below
    # the 1080p ones by angular size and runs Far at 10 Hz, or leaves the view in the windowed renderer.)
    "ladder-video": "focus:1:1920x1080:0:60:stage:i2;video:1:1920x1080:30:40:park:pull;other:1:1920x1080:0:40:park:pull:i2",
}


def tiers(args):
    """Run a capture profile made of tiers; report distinct fps per tier against its target."""
    setup()
    spec = PROFILES.get(args.profile, args.profile)
    groups = []
    pile_index = 0
    for part in spec.split(";"):
        name, count, size, draw, fps, placement, *flags = part.split(":")
        members = [spawn(f"{name}{i}", size, float(draw)) for i in range(int(count))]
        for w in members:
            place(w, placement, pile_index)
            pile_index += placement == "pile"
        suffix = "".join(":" + f for f in flags)
        groups.append({"name": name, "fps": int(fps), "members": members, "suffix": suffix})
    activate_canvas()
    time.sleep(3)
    everyone = [w for g in groups for w in g["members"]]
    cpu = {}
    thread = threading.Thread(target=lambda: cpu.update(clients_cpu(everyone, max(1, args.seconds - 2))))
    thread.start()
    since = {w["title"]: client_clock(w["title"]) for w in everyone}
    targets = [(w, g["fps"], g["suffix"]) for g in groups if g["fps"] for w in g["members"]]
    run = measure(targets, args.seconds, args.render)
    thread.join()
    unique = run["summary"]["unique_fps"]
    waits = {}
    for w in run["summary"]["windows"]:
        waits.setdefault(w["addr"], []).append(w["wait_p50"])
    report = {"profile": args.profile, "canvas_hz": REFRESH, "hyprland_cpu": run["cpu"]["hyprland"], "tiers": []}
    for g in groups:
        rates = [unique.get(w["address"].removeprefix("0x"), 0) for w in g["members"]] if g["fps"] else []
        wait = [x for w in g["members"] for x in waits.get(w["address"].removeprefix("0x"), [])]
        report["tiers"].append({
            "tier": g["name"], "count": len(g["members"]), "target": g["fps"],
            "fps_avg": round(sum(rates) / len(rates), 1) if rates else None,
            "fps_min": round(min(rates), 1) if rates else None,
            "wait_p50": round(sum(wait) / len(wait), 1) if wait else None,
            "client_cpu": round(sum(cpu.get(w["title"], 0) for w in g["members"]), 1),
            "client_draw_fps": round(sum(client_fps(w["title"], since[w["title"]] + 1) or 0 for w in g["members"]) / len(g["members"]), 1),
        })
    rows = [r for r in run["rows"] if "render_p50" in r][2:]
    if rows:
        report["render_p50"] = round(sum(r["render_p50"] for r in rows) / len(rows), 2)
        report["render_p99_max"] = max(r["render_p99"] for r in rows)
    print(json.dumps(report))


def sweep(args):
    """S3 manual: the user sweeps the real mouse while the cursor is confined to the spike output.
    First half with no_follow_mouse on the pile slivers, second half without it."""
    setup()
    user_window = active_address()
    stage = spawn("sweep-stage", "1920x1080", 30)
    pile = [spawn(f"sweep-pile{i}", "960x600", 5) for i in range(4)]
    place(stage, "stage")
    for i, w in enumerate(pile):
        place(w, "pile", i * 10)

    def follow(value):
        for w in pile:
            lua(f'hl.dispatch(hl.dsp.window.set_prop({{window="address:{w["address"]}", prop="no_follow_mouse", value="{value}"}}))')

    follow(1)
    activate_canvas()
    time.sleep(args.lead)
    lua(f'hl.dispatch(hl.dsp.focus({{window="address:{stage["address"]}"}})) '
        f'hl.dispatch(hl.dsp.cursor.move({{x={OUT_X + 900}, y=500}}))')
    names = {w["address"]: w["title"] for w in [stage, *pile]}
    # Start timing only once the user is actually moving the mouse (up to 2 minutes).
    home = hyprctl("cursorpos")
    deadline = time.monotonic() + 120
    while hyprctl("cursorpos") == home and time.monotonic() < deadline:
        time.sleep(0.05)
    trace = open(STATE / "sweep-trace.tsv", "w")
    phases = []
    for label, value, seconds in (("no_follow_mouse=1", 1, args.seconds / 2), ("no_follow_mouse=0", 0, args.seconds / 2)):
        follow(value)
        if active_address() != stage["address"]:
            lua(f'hl.dispatch(hl.dsp.focus({{window="address:{stage["address"]}"}}))')
        end = time.monotonic() + seconds
        samples = changes = on_sliver = 0
        focused = {}
        xs = []
        last = stage["address"]
        while time.monotonic() < end:
            cursor = hyprctl("cursorpos")
            x, y = (float(v) for v in cursor.split(","))
            xs.append(x)
            trace.write(f"{label}\t{time.monotonic():.3f}\t{x - OUT_X:.0f}\t{y:.0f}\n")
            on_sliver += x >= OUT_X + OUT_W - SLIVER
            active = active_address()
            focused[names.get(active, active)] = focused.get(names.get(active, active), 0) + 1
            if active != last:
                changes += 1
                last = active
            samples += 1
            time.sleep(0.02)
        phases.append({"phase": label, "samples": samples, "focus_changes": changes,
                       "cursor_x_range": [min(xs) - OUT_X, max(xs) - OUT_X] if xs else None,
                       "samples_on_sliver_strip": on_sliver, "focused_share": focused})
    trace.close()
    if user_window:
        lua(f'hl.dispatch(hl.dsp.focus({{window="address:{user_window}"}}))')
    teardown()
    print(json.dumps(phases, indent=1))


def teardown(_args=None):
    for c in clients():
        try:
            os.kill(c["pid"], signal.SIGTERM)
        except ProcessLookupError:
            pass
    time.sleep(0.5)
    if any(m["name"] == OUTPUT for m in monitors()):
        hyprctl("output", "remove", OUTPUT)


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("setup")
    p1 = sub.add_parser("s1")
    p1.add_argument("--seconds", type=int, default=20)
    p1.add_argument("--render", type=int, default=60)
    p1.add_argument("--park-pile", action="store_true")
    p1.add_argument("--park-warm", action="store_true", help="park warm windows and pull them with ignore_damage")
    p1.add_argument("--hot", type=int, default=4)
    p1.add_argument("--pull", action="store_true", help="capture with ignore_damage=1")
    p1.add_argument("--inflight", type=int, default=1)
    p1.add_argument("--warm", type=int, default=16)
    p1.add_argument("--pile", type=int, default=30)
    p1.add_argument("--warm-fps", type=float, default=0, help="cap warm clients' draw rate (0 = vsync)")
    p1.add_argument("--pile-fps", type=float, default=0, help="cap pile clients' draw rate (0 = vsync)")
    sub.add_parser("s2")
    pt = sub.add_parser("tiers")
    pt.add_argument("profile", help="profile name (%s) or a raw tier spec" % ", ".join(PROFILES))
    pt.add_argument("--seconds", type=int, default=15)
    pt.add_argument("--render", type=int, default=60)
    sub.add_parser("s3")
    ps = sub.add_parser("sweep")
    ps.add_argument("--seconds", type=int, default=60)
    ps.add_argument("--lead", type=int, default=15)
    sub.add_parser("teardown")
    args = parser.parse_args()
    {"setup": lambda a: setup(), "s1": s1, "s2": s2, "s3": s3_s10, "tiers": tiers, "sweep": sweep, "teardown": teardown}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
