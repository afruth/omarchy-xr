# Window canvas M0 spike — results

Plan: [`infinite-canvas-plan.md`](infinite-canvas-plan.md) §3.6. Run on 2026-09-25.

**Verdict: GREEN.** Every go/no-go item passed: S1–S5, S7, S9–S11, including the manual mouse
sweep and the glasses legibility check. Only the optional power/eDP measurement is open. The chosen capture policy (60 Hz focused / 24 Hz × 4 / 10 Hz / idle,
and 10 Hz when zoomed out) meets every target on this iGPU; see S1b. On this machine, Hyprland can capture individual windows by
address, including windows on a hidden workspace. Focusing a window by address and typing into it
works. A Lua guard keeps the canvas workspace in place. The plan needs the amendments listed at
the end: the capture ladder, a simpler park tier, a required SUPER+F rebind, and scale 1.0 by
default.

## Setup

- Hyprland 0.56.2 (Lua config). Laptop with a TigerLake-H **GT1** iGPU (the weakest Intel Xe
  variant) and an RTX 3050 Mobile. All capture and rendering in these tests ran on the iGPU
  (`Mesa Intel(R) UHD Graphics (TGL GT1)`). AC power. The OMXR monitor-mode outputs stayed
  applied during the tests, but the viewer was not running.
- `make spike-canvas` builds `build/spike-window-capture`. The protocols are vendored:
  `protocols/hyprland-toplevel-export-v1.xml` and
  `protocols/wlr-foreign-toplevel-management-unstable-v1.xml`. The second is needed only
  because the v2 export XML references `zwlr_foreign_toplevel_handle_v1`, so it must be linked.
  - The capture mode captures windows over one Wayland connection and imports them with
    `GpuCapture`. Options: 1–4 staggered requests in flight per window, per-window
    `ignore_damage`, and a synthetic stereo load (every texture drawn twice into a 3840×1080
    FBO at 60 Hz).
  - `--client WxH TITLE [FPS]` opens an animated vsync'd SDL/GL test window.
- `tools/spike_canvas.py` drives the tests:
  - It creates a headless `SPIKE-canvas` output (2560×1440, x = 20000) with the workspaces
    `spikecanvas` and `spikepark`, plus a window rule for the class `omarchy-xr-spike-client`.
  - It places windows as stage (real coordinates), pile (an 8 px sliver at the right edge) or
    park (the hidden workspace).
  - It samples `/proc` CPU for Hyprland and every client.
  - `teardown` removes the clients and the output. The spike's window and workspace rules stay
    registered until the next config reload, and they match only the spike's class and names.

## Results

### S1: capture under load (all DMA-BUF, zero failures in every run)

Hot windows are 1920×1080 clients drawing at vsync. Warm windows are 1280×720, captured at 10 Hz.
Pile/park windows are 960×600 and not captured. "Hot fps" counts distinct frames per hot window.

| Run | Hot / warm / other | Clients draw at | In flight | Canvas Hz | Hot fps | Request→ready | Hyprland CPU | Synthetic stereo render p50 / worst p99 |
|---|---|---|---|---|---|---|---|---|
| F | 1 / 0 / 0 | vsync | 1 | 60 | **59.8** | 16 ms | 4 % | 1.2 / 8 ms |
| A | 4 / 0 / 0 | vsync | 1 | 60 | 32 | 33 ms | 7 % | 1.1 / 6 ms |
| K | 4 / 0 / 0 | vsync | 2 | 60 | 47 | 43 ms | 7 % | 2.5 / 16 ms |
| L | 4 / 0 / 0 | vsync | 3 | 60 | 53 | 56 ms | 10 % | 3.7 / 37 ms |
| I | 4 / 0 / 0 | vsync | 1 | 120 | 42 | 24 ms | 8 % | 2.0 / 14 ms |
| *plan target* | 4 / 16 / 30 | all vsync (stress) | 1 | 60 | 21 | 45–60 ms | 23 % | 2–4 / 27 ms |
| M | 4 / 16 / 30 | warm 10, other 2 | 2 | 60 | 29 | 69 ms | 19 % | 2.9 / 47 ms |
| **O** | 2 / 8 / 40 | warm 30, other 2 | 2 | 60 | **54** | 35 ms | 12 % | 1.9 / 13 ms |
| **P** | 2 / 8 / 40 | warm 30, other 2 | 2 | **120** | **59.5** | 21 ms | 15 % | 2.3 / 16 ms |
| **Q** | 1 / 16 / 33 | warm 10, other 2 | 2 | 60 | **58.4** | 21 ms | 10 % | 2.4 / 10 ms |
| R | 2 / 8 / 40 (40 parked) | warm 30, other 2 | 2 | 60 | 55 | 35 ms | 8 % | 2.2 / 15 ms |
| **S** | 2 / 8 / 40 (warm + 40 parked, warm pulled) | warm 30, other 2 | 2 | 60 | **55** | 34 ms | **7 %** | 2.5 / 21 ms |
| T | 1 / 16 / 33 (all parked, warm pulled) | warm 10, other 2 | 2 | 60 | 58 | 19 ms | 7 % | 2.3 / 29 ms |

Findings:

- **Latency, not raw throughput, limits the rate.** Each request waits for the window's next commit
  and then for the canvas output's next frame. That is typically 1–2 output frames, and with a
  single request in flight it caps a window at about 30 fps once there is more than one.
  - Two staggered requests in flight fix this for 1–2 hot windows.
  - A 120 Hz canvas output halves the wait: 59.5 fps and 21 ms in run P.
- **The plan's hot set (4 × 1080p at 60 Hz + 16 warm) does not fit this iGPU.** It reached
  21–29 fps per hot window, and waits rose to 45–70 ms as the compositor's render thread queued
  exports.
  - 1–2 hot windows plus 8–16 warm windows at 10 Hz with dozens of idle windows is comfortable:
    54–60 fps.
  - Warm windows always held exactly 10 Hz.
- **Import cost is small.** Ready→imported (with `glFinish`, native size) is 0.5–2 ms p50 when
  the GPU is quiet, and up to ~10 ms p50 / 30 ms p99 when 50 GL clients saturate the GT1.
- Client CPU is negligible when clients are mostly idle: 2–6 % in total for 40–50 windows.
- **Not measured:** eDP frame-time p99 (it needs `debug:overlay` and reading by eye) and package
  watts (RAPL needs root).

### S2: pile vs park, promotion

| State | Capture without `ignore_damage` | Capture with `ignore_damage=1` | Client's own draw rate |
|---|---|---|---|
| Pile (8 px sliver on the visible canvas workspace) | 60 fps, 9 ms wait | — | 60 |
| Park (hidden workspace), SDL test client | stalls (1 frame, then 3 s timeout) | **60 fps** | ~20 |
| Park, real `mpv` (30 fps testsrc) | 19.5 fps | 60 distinct capture timestamps | ~20 |
| Park, real `foot` (text updating every 50 ms) | 19.7 fps | 60 | ~20 |
| Promotion park → canvas | new frames start < 0.5 s after the move; full rate within 1 s | | |

- **The plan is wrong on this point: parked windows are not frozen.** Hyprland throttles hidden
  clients to about 20 Hz of frame callbacks. It does not suspend them.
- Capture with `ignore_damage=1` works for parked windows. That makes "park + pull" a cheap,
  uniform warm/cold tier (run S), and pile slivers are needed only for hot windows.

### S3: input routing (automated part)

- `hl.dispatch(hl.dsp.focus({window="address:0x…"}))`, then `bring_to_top`, then `cursor.move`
  focused a staged `foot` on the canvas output within **6.5 ms**. `wtype hello-canvas` arrived in
  that window.
- Across 60 synthetic cursor warps over the pile slivers (`no_follow_mouse` set with
  `hl.dsp.window.set_prop({window=…, prop="no_follow_mouse", value="1"})`), focus never left the
  staged window. **Caveat:** these were warps. A real-mouse sweep with the laptop screen off is
  still manual (see "Not yet run").
- The user's focus was restored by address afterwards.

### S3: verified Lua API names (Hyprland 0.56.2)

- **Dispatchers:**
  - `hl.dsp.window.{move, resize, float, bring_to_top, alter_zorder, set_prop, fullscreen, fullscreen_state, pin, center, close, tag}`
  - `hl.dsp.focus({window=…|workspace=…})`
  - `hl.dsp.cursor.move({x,y})`
  - `hl.dsp.workspace.{move, rename, toggle_special, swap_monitors}`
- **Arguments:** `window="address:0x…"` targets any window. `move({x,y})` and `resize({x,y})`
  take absolute global coordinates. `move({workspace="name:…", follow=false})` moves a window
  to a workspace.
- **Queries:** `hl.get_windows`, `hl.get_window`, `hl.get_workspace_windows`, `hl.get_monitors`,
  and `hl.get_cursor_pos()`, which returns `{x,y}`.
- **Rules:** `hl.workspace_rule({workspace, monitor, persistent})`.
- **`hl.window_rule` fields** accepted: `float`, `no_anim`, `border_size`, `no_shadow`,
  `no_blur`, `rounding`, `no_dim`, `no_focus`, `no_follow_mouse`, `suppress_event`,
  `decorate`, `opacity`, `pin`, `workspace`.
  - **`no_border` does not exist**; use `border_size=0`.
- **Events** (`hl.on`): `window.open`, `window.close`, `window.destroy`, `window.title`,
  `window.class`, `window.active`, `window.fullscreen`, `window.move_to_workspace`,
  `window.urgent`, `workspace.active`, `monitor.focused`.
  - `window.move` and `window.resize` do not exist, as the plan assumed.
- **`hyprctl eval '<lua>'`** runs Lua in the compositor. That is how the spike driver works.

### S9: workspace guard

A Lua `hl.on("workspace.active")` guard puts `spikecanvas` back when another workspace shows up on
the canvas output. Tested by focusing a canvas window and then focusing the non-existent
workspace 17:

- The guard ran **within the dispatch**: every 10 ms poll afterwards already showed `spikecanvas`.
- The empty workspace 17 was garbage-collected.
- Capture of the canvas window continued at 59 fps.
- The SUPER+SHIFT+n (move window away) and scratchpad cases were not scripted.

### S10: fullscreen

- **Fullscreen requested by the client** (SDL `SetWindowFullscreen`, like a browser's F11): with
  `suppress_event="fullscreen maximize"` the window stayed at 1280×720, with fullscreen state 0.
  **Pass.**
- **Fullscreen dispatched by the compositor** (`hl.dsp.window.fullscreen`, i.e. Omarchy's
  SUPER+F) is **not** blocked by `suppress_event`. The window went fullscreen (state 2,
  2560×1440). The `window.fullscreen` event does fire, so a guard can revert it. The plan must
  treat rebinding SUPER+F in canvas mode as required, with the event guard as a backstop.

### S4: pull cost

Pulling 8–16 parked windows at 10 Hz with `ignore_damage=1` cost *less* Hyprland CPU than keeping
them as live slivers: 7 % against 10–12 % (runs S/T against O/Q). GPU cost was not isolated.

### S1b: the chosen capture policy (tier profiles)

| View | Tier | Rate | Where the window lives |
|---|---|---|---|
| Zoomed in | Focused window | 60 Hz, 2 requests in flight | The stage, live on the canvas workspace |
| Zoomed in | Other visible windows | **24 Hz, at most 4** | Sliver on the canvas workspace (or parked and pulled; both measured) |
| Zoomed in | Remaining visible windows | 10 Hz | Parked, pulled with `ignore_damage=1` |
| Zoomed in | Not visible | idle (no capture; last texture kept) | Parked |
| Zoomed out | Every visible window | 10 Hz | Parked, pulled |

Run with `tools/spike_canvas.py tiers <profile>`. Client sizes:

- focused: 1920×1080
- other visible (near/far): 1280×720
- idle: 960×600

"Realistic" means the clients draw at 60/30/10/2 fps by tier; "stress" means every client draws
at vsync. Each run lasted 15 s with the synthetic stereo render on. "Staggered" means the first
requests of windows with the same rate are spread evenly over one period.

| Profile | Canvas Hz | Focused (60) | Near (24) | Far / overview (10) | Request→ready p50 | Hyprland CPU | Render p50 / worst p99 |
|---|---|---|---|---|---|---|---|
| zoomed-in (1 + 4 + 6 + 39 idle) | 60 | **59.5** | **23.9** | **10.0** | 25 / 26 / 29 ms | 7 % | 3.3 / 14 ms |
| zoomed-in, stress | 60 | 59.5 | 23.9 | 10.0 | 25 / 26 / 29 ms | 10 % | 2.1 / 14 ms |
| zoomed-in, stress, staggered | 60 | **59.9** | **24.0** | **10.0** | 23 / 19 / 26 ms | 10 % | 5.0 / **8.9 ms** |
| zoomed-in, near tier parked + pulled | 60 | 59.4 | 23.9 | 10.0 | 23 / 23 / 27 ms | 7 % | 1.8 / 18 ms |
| zoomed-in, stress | 120 | 59.8 | 23.9 | 10.0 | 15 / 16 / 18 ms | 13 % | 1.4 / 10 ms |
| zoomed-out, 30 visible + 20 idle | 60 | — | — | 10.0 | 44 ms | 6 % | 1.8 / 42 ms |
| zoomed-out, 30 visible + 20 idle, **staggered** | 60 | — | — | **10.0** | **18 ms** | 7 % | 1.8 / **7.5 ms** |
| zoomed-out, stress (30 + 20) | 60 | — | — | 10.0 | 45 ms | 9 % | 1.7 / 42 ms |
| zoomed-out, 40 visible, staggered | 60 | — | — | **10.0** | 41 ms | 9 % | 2.1 / **7.4 ms** |
| zoomed-out, 50 visible, staggered | 60 | — | — | 9.2 ✗ | 106 ms | 10 % | 2.6 / 70 ms ✗ |
| zoomed-out, 50 visible at 6 Hz, staggered | 60 | — | — | 6.0 | 26 ms | 9 % | 2.7 / 22 ms |

Findings:

- **The policy meets every target on this iGPU**, including when every client renders at vsync.
  - The focused window holds 59.5–59.9 fps, near windows 23.9–24.0 and far windows 10.0.
  - Hyprland CPU is 7–13 %.
- **Requests must be spread over time.** Starting every 10 Hz request at the same moment causes
  bursts: 42 ms render spikes in the overview. Spreading their phases evenly brings the worst
  p99 down to 7–9 ms and cuts request→ready by 2.5×. The scheduler must assign each window its
  own phase.
- **The overview ceiling is about 400 captures/s of 720p windows (≈ 370 Mpix/s)** on the GT1:
  40 windows at 10 Hz is clean, 50 is not. Hyprland exports full-size buffers only, so the cost
  scales with window pixels, not thumbnail size.
  - Rule for the governor: overview rate per window = `min(10, budget / Σ(window pixels / 720p))`,
    with the budget starting at 350 720p-equivalents/s.
  - 50 windows then run at about 6–7 Hz; the 6 Hz run was clean apart from a 22 ms p99 in one
    second.
- Parking the near tier and pulling it performs the same as keeping it on slivers. The only
  window that needs to stay live on the canvas workspace is the focused one, so the sliver strip
  can go.
- 120 Hz still lowers latency (15 ms against 25 ms) for +3 % Hyprland CPU. It is optional, not
  required.

### S1c: normal working set (≤ 6 windows) and the adaptive rate ladder

With few windows open there is headroom to start above the S1b rates and step down as more windows
open. Measured with the focused window at 1920×1080 and 60 Hz (2 in flight), plus five others.
Clients draw at vsync (stress). "Mpix/s" is the total exported pixel rate:
Σ width × height × rate.

| Other windows | Rate | Placement | Mpix/s | Focused fps | Others fps | Worst p99 | Result |
|---|---|---|---|---|---|---|---|
| 5 × 1080p | 60 | live (sliver) | 746 | 37 | 34 | 35 ms | ✗ |
| 6 × 1080p, overview | 60 | live | 746 | — | 37 | 27 ms | ✗ |
| 5 × 1080p | 30 | live | 435 | 45 | 29.9 | 20 ms | ✗ |
| 5 × 1080p | 30 | parked + pulled | 435 | 48 | 29.9 | 25 ms | ✗ |
| 6 × 1080p, overview | 30 | live | 373 | — | 29.9 | 14 ms | ~ borderline |
| 5 × 1080p | **20** | parked + pulled | 331 | **59.6** | **20.0** | **1.4 ms** | ✓ |
| 3 × 1080p | **30** | parked + pulled | 310 | **59.9** | **30.0** | 15 ms | ✓ |
| 5 × 720p | **40** | live | 308 | **59.7** | **39.9** | 15 ms | ✓ |
| 5 × 720p | **30** | parked + pulled | 262 | **59.9** | **29.9** | 4.9 ms | ✓ |

Findings:

- **Exported pixels per second is the only budget that matters.** Every run at ≤ ~330 Mpix/s met
  its targets, and every run at ≥ 435 Mpix/s did not. Window count on its own does not predict the
  outcome. The ceiling is ~350–370 Mpix/s on the TGL GT1, consistent with S1b's overview ceiling
  (40 × 720p × 10 Hz = 370).
- **Pulled parked clients draw at the pull rate.** A parked client pulled at 30 Hz drew at
  28.9–29.9 fps; unpulled parked clients get ~20 Hz of frame callbacks. So park + pull works for
  every tier up to ~30 Hz. Above that, keep the window live on the canvas workspace.

**Ladder rule (for the M5 governor):**

- Rate steps are 60 › 40 › 30 › 24 › 20 › 15 › 10 › 6.
- The focused window always gets 60 Hz, 2 in flight.
- Other visible windows are assigned in order of visual importance: angular size in view, then
  most recent use. Each gets the highest step that keeps Σ(w × h × rate) ≤ **budget**.
  - The budget defaults to **300 Mpix/s**, which leaves headroom below the measured
    330–370 Mpix/s.
  - Tier caps still apply: the 4 nearest windows get at most 40 Hz while zoomed in; the rest
    get ≤ 10 Hz; everything is ≤ 10 Hz zoomed out; off-screen windows are idle.
- Lower a window's rate as soon as the sum is over budget. Raise it only after 2 s under
  90 % of the budget, so rates don't oscillate as windows open and close.
- The budget can be tuned at runtime. It should self-calibrate: shrink when measured
  request→ready p50 exceeds 2 output frames, and grow slowly when it stays under 1.

What that gives with 1080p windows on this machine:

| Windows | Rates |
|---|---|
| 2 | focused 60, other 60 |
| 4 | focused 60, others 24–30 |
| 6 | focused 60, others 15–20 |
| 11 | focused 60, 4 near at ~15, the rest at 10 |
| overview of 40 × 720p | 10 Hz each |

### S3: real-mouse sweep (manual)

Run with `tools/spike_canvas.py sweep`. The setup is a staged 1920×1080 window, plus four pile
windows as 8 px slivers at the right edge of the spike output. The output is set apart from the
other displays (x = 20000), so the cursor can't leave it; that stands in for "laptop off". Timing
starts once the mouse moves. The first 30 s have `no_follow_mouse=1` on the slivers, the last
30 s have it off. The cursor was sampled every 20 ms, and the trace is kept.

| Phase | Samples on the sliver strip | Focus changes | Focus share |
|---|---|---|---|
| `no_follow_mouse=1` | 81 | **0** | the staged window 100 % |
| `no_follow_mouse=0` | 1128 | 23 | the staged window 0.3 %, pile windows 99.7 % |

**Pass.** `no_follow_mouse` on non-staged windows is required and sufficient for pointer motion.
A *click* on a sliver still focuses it, so M3 needs the Lua warp-back at the stage band edge (or
slivers moved outside the reachable area) as planned.

### S5: output teardown race (nested Hyprland)

This ran in a nested Hyprland 0.56.2 (started inside a window with a minimal Lua config), so the
live session was never at risk:

- Two windows were captured for 45 s: one at 60 Hz with 2 requests in flight, one at 30 Hz with
  `ignore_damage=1`.
- Over the same period, 40 cycles of `output create headless` → `hl.monitor` → `output remove`.
- In every other cycle, the captured window was first moved onto the output that was about to be
  removed, and that workspace was focused.

Results: 40/40 cycles, **zero capture failures, no compositor crash**, and both captures kept
running (49.6 and 29.3 distinct fps). **Pass.** A `pause_captures` datagram around output changes
is still cheap insurance, but it is not required.

### S7: search prompt (Quickshell layer shell)

Prototype: `tools/spike_search/shell.qml`, a `PanelWindow` on the canvas output with
`WlrLayershell.keyboardFocus: Exclusive` and namespace `omarchy-xr-search`. The test:

1. Focus a staged canvas window.
2. Launch the prompt: it mapped in **222 ms** from a cold `quickshell` start.
3. Type `term` with `wtype`.
4. Warp the cursor 100 times over the stage for 10 s.
5. Type `inal`, then press Enter.

The prompt received `terminal` intact and reported `search-accept terminal`. When it closed,
Hyprland returned focus to the staged window by itself. **Pass: D4 confirmed.** In production the
prompt should stay loaded inside the Studio plugin (hidden, not relaunched), so the cold-start
cost disappears.

### S11: text legibility (glasses)

This was tested on the VITURE Pro 2 used as a plain `DP-1` display (1920×1080 at 120 Hz), with a
text-heavy terminal at the default font, cycling the display scale through 1.0, 1.25 and 1.5,
30 s each. The plan's R = 2.4 is chosen for about 1 window pixel per glasses pixel, which is what
a direct display shows. The user's verdict: **all three are comfortable; the smallest text at 1.0
is very legible.**

**D3: the canvas output default is scale 1.0.** It shows the most content, and it is also the
cheapest to capture, because exports are window size × scale. Scale 1.25 is kept as a setting.

Caveat: the viewer resamples onto the cylinder, which softens text slightly compared with the
direct display. Recheck in M2, when the canvas renders in the viewer.

## Not run

| # | Why | Status |
|---|---|---|
| eDP frame-time p99, package watts | Needs `debug:overlay` / root for RAPL. | Optional: `sudo turbostat --show PkgWatt,GFXWatt` during a `tiers zoomed-in` run. Hyprland CPU (7–13 %) is recorded instead. |
| S6 ext-image-copy, S8 restart under lease | Informational only. | Skipped. Toplevel export passed everything, and the plan makes the live mode switch single-process. |

Side note: on 2026-09-25 the glasses once failed to enter DisplayPort mode over USB-C: the USB
side came up, but the `card2-DP-1` connector reported disconnected. The UCSI firmware errors
already logged at boot suggest the laptop's USB-C controller. A reboot fixed it. This is not
related to the spike, but worth a line in the troubleshooting docs.

## Plan amendments

1. **Capture policy (decided, verified in S1b/S1c).** The rates below are floors and caps; the
   actual rates come from the pixel-budget ladder in S1c, so a small canvas (≤ 6 windows) starts
   higher and steps down as windows open:
   - Zoomed in: the focused window at 60 Hz with 2 requests in flight; ≤ 4 other visible windows
     at 24 Hz; the remaining visible windows at 10 Hz; off-screen windows idle.
   - Zoomed out: every visible window at 10 Hz, capped by the 350 720p-equivalents/s budget.
   - Every window gets its own phase within its period.
2. **Canvas output at 60 Hz by default.** The policy holds at 60 Hz; 120 Hz is an option that
   cuts latency by ~10 ms for +3 % Hyprland CPU.
3. **Park stays live, so tier the windows by capture policy instead of freezing them.**
   - Only the focused (staged) window lives on the visible canvas workspace.
   - Every other window is **parked and pulled** (`ignore_damage=1`) at its tier's rate: 24, 10,
     or idle. A pulled parked client draws at the pull rate, which works up to ~30 Hz.
   - A window the ladder puts above 30 Hz (small canvases) goes back onto the canvas workspace as
     a sliver.
   - This simplifies §3.3: the 8 px sliver strip is dropped, so there is less compositor work and
     no hidden window the pointer can reach.
   - The risk entry "battery drain from pile clients" becomes "parked clients still draw at
     ~20 Hz". Suspending them is not available from the Lua API.
4. **Rebinding SUPER+F in canvas mode is required, not optional (D5).** `suppress_event` handles
   only fullscreen requested by the client. Add a `window.fullscreen` guard that reverts the state
   and triggers Fill.
5. Use `border_size=0` instead of `no_border`. Link the wlr-foreign-toplevel protocol code
   alongside the toplevel-export XML (Makefile and `package-release.py`).
6. The capture request loop must keep a request outstanding at all times. The current
   `DesktopCapture` model (request, wait, import, then request again) halves the frame rate for
   toplevel export.
