# Omarchy XR — Window Canvas mode: end-to-end implementation plan (post-M0)

Repository: `/home/andreas/Projects/omarchy-xr` (working tree at `d3034d0` plus the uncommitted 30-monitor / notification-in-mono changes). Platform: Hyprland 0.56.2 (Lua config API, Aquamarine 0.15), VITURE glasses, `--fov 28` (vertical) at 16:9, i.e. ≈ 48° horizontal. Dev machine: TigerLake-H GT1 iGPU (the weakest Intel Xe), RTX 3050 Mobile unused for capture and rendering.

Conventions: **[verified]** = read in this repo, in the Hyprland `v0.56.2` source, or on the live system during the investigations and reviews; **[measured Sn]** = settled by the M0 spike, numbers in [`canvas-spike.md`](canvas-spike.md); **[decision Dn]** = the user must confirm (collected in §8.2). Review resolutions are in Appendix A. This revision supersedes the pre-M0 plan wherever the spike contradicted it (park tier, capture rates, output scale, SUPER+F).

---

## 1. Summary, goals, non-goals

### 1.1 Summary

Add a second *scene mode* to the renderer, **Window Canvas**, that renders every canvas window as its own quad on the shared cylinder (the existing `workspace.follow` geometry at 360°), independent of monitors. Windows are captured per toplevel with `hyprland_toplevel_export_v1` **[verified: advertised by 0.56.2 as v2]**, enumerated and steered through the existing Lua adapter (`config/xr-controls.lua`), and operated with the real mouse and keyboard through the existing "focus + cursor warp" contract, generalised from monitor names to window addresses.

The 3D canvas is unlimited (it wraps around the cylinder and adds rows); the Hyprland side is not the canvas. Canvas windows physically live on **one dedicated headless output** `OMXR-<8hex>-canvas`:

- exactly one **staged** (focused) window at real, non-overlapped coordinates on its single *visible* workspace (the "stage band") that the pointer and keyboard operate, captured at 60 Hz with two requests in flight;
- **every other window** on a hidden *park* workspace bound to the same output. Hyprland does **not** suspend them: hidden clients are throttled to ≈ 20 Hz of frame callbacks **[measured S2]**, and a capture with `ignore_damage=1` ("pull") makes the client draw at the pull rate **[measured S1c]**. So parked windows are captured at their tier's rate (24 / 10 Hz) or left idle with the last texture kept; the renderer decides, Hyprland only hosts;
- a **sliver** placement on the visible workspace (8 px inside the output's far edge, `no_follow_mouse`) exists only for a window the rate ladder puts above 30 Hz, which happens on small canvases (≤ 6 windows) — park + pull tops out at ≈ 30 Hz **[measured S1c]**.

Cylinder coordinates, camera, search, placement and persistence are renderer-side data — the part of phantomat that transfers. The mode plugs into `View::drawEye` (`src/main.cpp:790-804`), so direct stereo, the `--display`/windowed SDL path and the spectator inherit it from one code path. Monitor mode stays the default and is untouched.

### 1.2 Goals

- Selectable render mode `monitors` (today) | `canvas` (new), persisted by Studio; switchable when stopped in M3, live (one renderer process, `mode:` datagram) in M6.
- Unlimited windows around the cylinder with auto-placement, remembered placement, arrange/tidy, nudge, summon, pin.
- Fast navigation: Overview, search (title/class/category, fuzzy, MRU-weighted), land on a window, **Fill** (window sized to the field of view), MRU cycling with a visible list, directional neighbour focus, radar strip.
- Click and type into any canvas window with the real input devices, in stereo and in 2D; an XR cursor is drawn from day one.
- Works in `--direct --stereo`, `--display` (glasses flat), windowed, and in the spectator.
- Every existing feature keeps working in both modes: notifications, live controls, environments, tracking/prediction, gaze/dwell/halo, Studio lifecycle and recovery, stats, lease loss, cleanup.
- Capture and compositor cost bounded by a pixel-budget scheduler; 60 fps stereo with 50 windows open on the dev iGPU **[measured S1b: focused 59.5–59.9 fps, 4 near at 24, rest at 10, Hyprland CPU 7–13 %]**. 120 Hz canvas output is optional (−10 ms latency, +3 % CPU), not promised.

### 1.3 Non-goals

- No Hyprland plugin (phantomat-style hooks are ABI-bound; unsuitable for the marketplace installer).
- No synthetic input (virtual pointer/keyboard) in v1; a later latency upgrade (§3.5).
- No Hyprland `fullscreen`/`maximize` of canvas windows, ever: it blanks every other capture on that output **[verified: `Renderer.cpp:250-253` stops rendering windows behind a fullscreen one]** and resizes the staged window to the whole output. "Fullscreen" is *Fill* (§5.2). `suppress_event` blocks only client-requested fullscreen, not Omarchy's `SUPER+F` dispatch **[measured S10]**, so the rebind in §5.5 is required.
- No eye tracking; no per-window curvature; no migration of monitor setups into canvas layouts; no X11-specific work beyond what toplevel export gives.

---

## 2. Lessons from phantomat

Phantomat (`kaolti/phantomat`, BSD-3-Clause, fork of `hyprland-scroll-overview`) is a Hyprland plugin. It gets pixels and input for free by living inside the compositor; omarchy-xr is an external DRM-lease client. Its *model and pure algorithms* carry over; its rendering, input and frame-pacing paths do not.

### 2.1 Adopt (port the code, ≈ 600 lines of pure logic, BSD notice in `THIRD_PARTY_NOTICES.md`)

| Phantomat piece (source ref) | Destination | Why |
|---|---|---|
| Two-state model: 100 % "work" vs zoomed-out "navigate", dimming/labels driven by `transitionProgress` | `canvas::Camera` states (§5.2) | Cleanest mental model; maps onto the existing `Level` flicks. |
| Camera `(viewOffset, scale)` over a 2D canvas; zoom-about-point (`zoomCanvasAt` `:4469`), fit-to-bounds (`fitAllWindows` `:9793`), pan-by-key 22 %/zoom | `src/canvas_model.hpp` | Exactly the math needed; the canvas is 2D before it is wrapped. |
| Search (`Navigator.cpp`): fold (casefold + NFD strip), AND of tokens, contiguous `scoreToken` = `100+14·len−min(start,40)/2`, +70 prefix, +45 word start; weights title 1 / class 0.85 / category 0.55; MRU bonus `max(0,36−4·rank)`; −24 for the origin window; stable sort; keep previous selection | `src/canvas_search.hpp` | Proven ranking, tiny, no compositor dependency. |
| Category classifier (`Navigator.cpp:28-51`) | `src/canvas_search.hpp` | Used by arrange and search. |
| Auto-placement (`manageCanvasWindow` `:4549-4636`): session box → memory `claim()` → ring search 1..16 in 8 directions from the camera centre, gap 40, grid snap | `src/canvas_placement.hpp` | Good default, adapted to a periodic x axis. |
| Arrange (`arrangeCanvasWindows` `:4645-4903`): category groups, shared-title-token groups, size normalisation, `ceil(sqrt n)` grids, shelf-pack | `src/canvas_placement.hpp` | One-key clean desk. |
| Directional neighbour rules (`:6520-6610`, 45° rule, `13·gap²+offset²`) | `src/canvas_placement.hpp` | `SUPER+arrows` in canvas mode. |
| Memory TSV (`Memory.cpp`: `window\tclass\ttitle\tx\ty\tw\th\tseen`, `camera\t…`, `claim()` with a 45 s restore window then exact-title-only) | `src/canvas_memory.hpp` → `<state>/canvas-memory.tsv` | Windows come back where they were; rules already survived daily use. |
| Undo/redo snapshots (`checkpointCanvas`/`applyCanvasSnapshot`) | `canvas::Scene` | Safety net for arrange. |
| Fill toggle with three-case restore (`canvasToggleFill` `:4288`: untouched / moved / resized) and **fill = resize the real window** | Fill flow (§5.2) | Same semantics; keeps text native. |
| Alt-Tab: tap = flip, hold = list (`switchWindow` `:9600`, `finishSwitcher` `:9643`) | MRU switcher (§5.5) | Blind cycling is unusable in XR. |
| Pin (`canvasTogglePin` `:4227`), nudge (`:9856`), summon (`:9716`), `F1` help | §5.5 | Cheap, matter more with a 48° view. |
| Details: swallow releases of consumed presses, `hoverFocusSettling`, keep top-left of oversized windows in view, move the search target up so the palette never covers it, ignore late wheel leftovers (`:5027`) | scattered | Known annoyances avoided. |

### 2.2 Adapt (idea carries, code does not)

- **World = real coordinates.** Phantomat's canvas positions are real floating positions. Rejected for the canvas (off-output floating windows are never captured **[verified: `ScreenshareManager.cpp:35-38`]**, and the canvas would be bounded by output size), kept for the one interactive window: the staged window sits at a real, non-overlapped rectangle so pointer warps and focus use today's Lua code shape.
- **Frame pacing.** Phantomat throttles unseen windows by hooking `wl_surface.frame`. We cannot; the equivalents are (a) the *park* workspace, where Hyprland throttles hidden clients to ≈ 20 Hz and a pulled client draws at the pull rate **[measured S2/S1c]**, and (b) the capture-side rate ladder (§4.4). Suspending clients is not available from the Lua API.
- **Linked cameras.** Stereo eyes and the spectator already share `currentView()`; one canvas camera in `View`.
- **Overlay layering.** Phantomat composites palette/minimap after the lens. Here the palette, switcher, radar and help are **body-locked** quads placed with the notification lazy-follow logic (`notification_space.hpp` `findPlace`/`retargetAt`) at 0.9·R — not head-locked (vergence jump + 3DoF swim), not on the cylinder. In 2D/spectator they are drawn in screen space.
- **Fullscreen step-aside.** Phantomat removes its overlay so Hyprland renders natively. Ours is *Fill*: resize the real staged window to the FOV-derived pixel size and dolly the eye so it fills ≈ 90 % of the view.

### 2.3 Reject

- Compositor hooks, private-header access, render-pass patching (ABI-bound, rebuilt per Hyprland release).
- Barrel-lens post-process (stereo optics define the view; distortion fights head tracking).
- "Places" mode, group handling, X11 sync layer, per-monitor cameras.

---

## 3. Platform strategy

### 3.1 Window enumeration and the `.windows` mailbox

**Source of truth: the Lua adapter** (`config/xr-controls.lua`, installed by "Utilities → Setup & integrations"). It already subscribes to `workspace.active`/`monitor.focused` with `hl.on` and publishes tmp+rename mailboxes.

- Events used **[verified names, S3]**: `window.open`, `window.close`, `window.destroy`, `window.title`, `window.class`, `window.active`, `window.fullscreen`, `window.move_to_workspace`, `window.urgent`, `workspace.active`, `monitor.focused`. There is no `window.move`/`window.resize` event; client resizes are picked up by the heartbeat (the capture session tracks resize itself **[verified]**).
- Events only mark the list **dirty**; `publishWindows()` writes at most every 100 ms from the existing 33 ms hover timer, plus a 1 s heartbeat (no file I/O per title change on the compositor thread).
- Data comes from `hl.get_windows()` **[verified fields]**: `address`, `class`, `title`, `size`, `at`, `floating`, `focus_history_id`, `pid`, `xwayland`, `xdg_tag`, `stable_id`, workspace. Membership = "on the canvas workspace or the park workspace" (not "mapped anywhere"); other windows are listed with `canvas=0` so search can offer *bring to canvas*.
- Mailbox `pose.sock.controls.windows`:
  ```
  v1 <owner> <seq> <stamp>
  <address> <class-hex> <title-hex> <w> <h> <atX> <atY> <focus_history_id> <place: stage|sliver|park|off> <floating> <pid> <xwayland> <canvas 0/1>
  ```
  Class/title hex-encoded like the notification token (`live_controls.hpp` `decodeTarget`); ≤ 512 rows. `place` is where Hyprland hosts the window (§3.3); the capture rate is the renderer's business (§4.4). Renderer: `src/window_list.hpp` parser behind `LiveControls::updateWindows()` with the same owner/stamp/seq gates as `.pane`.

Alternative considered: `ext_foreign_toplevel_list_v1` + `hyprland_toplevel_mapping_manager_v1` in the renderer **[verified: advertised]**. Not chosen: the Lua side is needed anyway for staging, focus and rules, and `focus_history_id`/`at`/workspace exist only in Hyprland's own API.

**Exclusions** (never adopted): class `omarchy-xr-spectator`, the renderer's own SDL window (title prefix `Omarchy XR`), the search prompt `omarchy-xr-search`, the Studio panel and Quickshell services (matched by pid published by the backend in `canvas.tsv`), anything with a helper `xdg_tag`, `special:` workspaces.

### 3.2 Per-window capture protocol

**`hyprland_toplevel_export_v1` v2** (decided; all M0 capture tests passed on it with zero DMA-BUF failures **[measured S1]**) — vendored `protocols/hyprland-toplevel-export-v1.xml` plus `protocols/wlr-foreign-toplevel-management-unstable-v1.xml`, which is needed only because the v2 export XML references `zwlr_foreign_toplevel_handle_v1` and the generated code must link **[verified: `make spike-canvas`]**. Both go into the `Makefile` scanner rules and `package-release.py`.

- `capture_toplevel(frame, overlay_cursor, handle)` with `handle = uint32(address & 0xFFFFFFFF)` **[verified: `CViewQuery::byHandle`]**; works for windows on the hidden park workspace **[measured S2]**; no toplevel-list protocol needed.
- Frame flow identical in shape to wlr-screencopy (`buffer`/`linux_dmabuf`/`buffer_done` → `copy` → `ready|failed`), so `DesktopCapture::Impl`'s state machine, `GpuCapture` slot import and the SHM fallback are reused. **One change is mandatory**: the request loop keeps a request outstanding at all times (the focused window keeps two, staggered). Each request waits for the window's next commit and then for the canvas output's next frame, so today's request → wait → import → request cycle halves the frame rate **[measured S1: 4 hot windows reach 32 fps with one request in flight and 47 with two; 1–2 hot windows reach 54–59.5 with two]**.
- **An unknown or closed handle gets no answer [measured M2, Hyprland 0.56.2]**: no frame object is created and neither `failed` nor `buffer` arrives; sending `destroy` on that frame is a protocol error that closes the shared connection. The hub therefore follows each batch of requests with `wl_display.sync`; a frame with no event by then is an orphan, dropped without `destroy`, and three orphans in a row (or failures before any buffer) make `alive()` false.
- `ignore_damage=1` forces `damageMonitor(session monitor)` **[verified]** and makes a parked client render at the requested rate up to ≈ 30 Hz **[measured S2/S1c]**. It is the regular pull for every parked window, not a promotion-only tool. Pulling 8–16 parked windows costs *less* Hyprland CPU than live slivers (7 % vs 10–12 %) **[measured S4]**.
- Buffer size is always `window size × monitor scale` **[verified]**, so capture cost scales with window pixels, not thumbnail size; thumbnails are made renderer-side (`GpuCapture::present` multi-pass halving, `capture_scale.hpp`). Damage is always full-buffer **[verified]**. Ready → imported costs 0.5–2 ms p50 on a quiet GPU, up to ~10 ms p50 under saturation **[measured S1]**.
- Each session posts `screencast`/`screencastv2` IPC events with a 500 ms stop timer **[verified: `ScreenshareSession.cpp:137-152`]**: idle sessions are kept alive without `copy` and rate changes are hysteresis-limited (§4.4) so the omarchy-shell recording indicator does not flicker. **[measured M5]**: rate changes and park ↔ sliver moves post no event (0 in steady state in every M5 run), but the open frame does not hold the stop timer: a window leaving the view (going idle) posts `screencast>>0,window` ≈ 0.5 s later and `1,window` when it comes back, so a view change posts about one event per window entering or leaving the view. omarchy-shell's recording indicator does not read these events (it checks for `gpu-screen-recorder`), so nothing flickers there.

**`ext_image_copy_capture_v1`** was not measured (S6 skipped as informational once toplevel export passed everything). It lacks `ignore_damage`, on which the park tier depends, and has an output-teardown crash in 0.56.2 (Hyprland #16317, closed "not planned"). It is **not** a route.

**Region-capture fallback (D2):** moot — per-toplevel capture passed M0. Kept as a note only: `zwlr_screencopy_manager_v1.capture_output_region` behind `FrameSource` would bound the canvas by output pixel area.

### 3.3 Where windows physically live in Hyprland

One dedicated headless output **`OMXR-<8hex>-canvas`**, created by `studio/canvas.py` `CanvasSession` through its **own journaled path** (`owned.add` + `record()` + `hyprctl output create headless` + `hl.monitor{…}`; **no** `persist_applied()`/`save()`, so `layout.json`/`viewer.tsv` of monitor mode are never overwritten). Default mode `2560x1440@60`, scale **1.0** (D3 resolved by S11: all of 1.0/1.25/1.5 are comfortable in the glasses, 1.0 shows the most and is cheapest to capture; 1.25 stays a setting). 120 Hz is a setting: it cuts request→ready from 25 to 15 ms for +3 % Hyprland CPU **[measured S1b]**. Positioned rightmost by `desktop_origin()` (its existing 100 px gap keeps it clear of other outputs).

Two workspaces, both bound to that output by `hl.workspace_rule({workspace, monitor, persistent})` **[verified S3]**:

```
canvas workspace (always the active one on the canvas output)
+---------------------------------------------------------+-+
| STAGE BAND: exactly one floating window (the staged      |8|  <- sliver strip, normally
| window), raised, at real coordinates outX+0..stageW      |p|     empty: only a window the
|                                                          |x|     ladder rates > 30 Hz sits
|                                                          | |     at x = outX+outW-8,
|                                                          |#|     intersecting the output
|                                                          | |     by 8 px, no_follow_mouse
+---------------------------------------------------------+-+     (real from M5: `.tiers`)
park workspace (never active): every other window; throttled to ~20 Hz by Hyprland,
pulled by the renderer with ignore_damage=1 at its tier rate (24 / 10 Hz) or left idle
```

- **Stage band** = output size minus the strip (from M5 Lua clamps staging and Fill to output width − 8 px, so the staged window never covers a sliver). The staged window is `float`ed, sized to its canvas size (or the Fill size) and moved with `hl.dsp.window.move({x,y})` / `hl.dsp.window.resize({x,y})` (absolute global coordinates = output origin + offset) **[verified S3]**, then `hl.dsp.window.bring_to_top` and `hl.dsp.focus({window="address:0x…"})`. Stage + focus + `hl.dsp.cursor.move` took 6.5 ms and typed input arrived **[measured S3]**.
- **Park** (default for every non-staged window): hidden workspace on the same output. Clients are *not* suspended **[measured S2, contradicting the pre-M0 plan]**: unpulled they draw at ≈ 20 Hz, pulled they draw at the pull rate up to ≈ 30 Hz. Capture by address works there; the renderer keeps the last texture when idle. Promotion to the stage (or a sliver) delivers fresh frames < 0.5 s after the move and full rate within 1 s **[measured S2]**. Park + pull performs the same as live slivers for the 24 Hz tier and is cheaper in Hyprland CPU **[measured S1b/S4]**.
- **Sliver** (exception, real from M5): a non-staged window whose ladder rate exceeds 30 Hz (small canvases, §4.4) is moved to the visible workspace as an 8 px sliver (stacked 24 px apart at the right edge in address order) with `no_follow_mouse` set through `hl.dsp.window.set_prop({window=…, prop="no_follow_mouse", value="1"})` **[verified S3]**. Park ↔ sliver moves have ≥ 2 s hysteresis; staging on an explicit action is immediate.
- **Rules** (installed by Lua in canvas mode, snake_case **[verified fields S3]**): `float`, `no_anim`, `border_size=0` (there is no `no_border`), `rounding=0`, `no_shadow`, `no_blur`, `no_dim`, `suppress_event="fullscreen maximize"`, and `no_follow_mouse` for sliver windows via `set_prop`. Static rules only match at map time, so Lua **enforces** state (float/size/position/prop) on `window.open`, `window.move_to_workspace`, `.windows` diffs and after `hyprctl reload`; the backend reinstalls rules inside `reconcile_outputs()`.
- **Migration** at canvas start moves *windows* (per address: `hl.dsp.window.move({window="address:0x…", workspace="name:<park>", follow=false})` **[verified S3]**), never workspaces. `<state>/canvas-session.json` records per window: original workspace id, floating flag, size, position. `CanvasSession.restore()` reverses it window-by-window *before* the output is removed. **[decision D1: migrate all regular windows at start (default) vs start empty and bring windows explicitly]**
- **Guards** (Lua, canvas mode only): `workspace.active` on the canvas monitor with any workspace other than the canvas workspace (SUPER+1..9, scratchpad) → immediately re-focus the canvas workspace and move the intruding workspace/special to the laptop output **[measured S9: the guard runs inside the dispatch, captures continue at 59 fps, the empty workspace is garbage-collected]**; `window.move_to_workspace` of a canvas window to a non-canvas workspace (SUPER+SHIFT+n) = *remove from canvas* → restore its origin state; a window arriving on the canvas workspace from elsewhere = *adopt*; `window.fullscreen` on a canvas window → revert with `hl.dsp.window.fullscreen_state` and trigger Fill (§3.4). Output create/remove churn during captures caused no failures in 40/40 cycles **[measured S5, nested Hyprland]**; `pause_captures` around output changes stays as cheap insurance.

### 3.4 Input routing and interaction model

**Pointer/keyboard = the real devices, routed by Hyprland focus**, same shape as `warpPointer(name,px,py)` (`xr-controls.lua:316-340`) but keyed by window address:

1. Renderer hit-tests the head ray (or, in 2D, the mouse ray) against canvas quads → `targeting::Hit{output=address, u, v}`.
2. On an **explicit focus action** (not dwell; §5.6) `publishHover` writes `.hover` **v4**: `v4 pid serial 1 <address> u v pointerSerial px py`, where `px,py` are window-buffer pixels: `pixelX/zoom · pixelW/rect.w` (the hit's `pixelX` is in projected units **[verified: `targeting.hpp` `pixelX=u*width`]**). The mirror file (`OMARCHY_XR_MIRROR_STATE`) gets the same v4 line.
3. Lua `selectCanvasWindow()`: if the address is not staged → `stageWindow(address)` (previous staged window → park, or a sliver if its rate is > 30 Hz; target → stage band, raised, focused); then `hl.dsp.cursor.move({x=stageX+px/scale, y=stageY+py/scale})` **[verified S3]**.
4. Keyboard follows Hyprland focus, as today.

**Cursor confinement and the XR cursor (v1, M3):** `input:follow_mouse` is 1 on this system **[verified]**. Lua publishes the compositor cursor position every 33 ms (mailbox `.cursor`: `v1 owner seq x y stamp`, from `hl.get_cursor_pos()` → `{x,y}` **[verified S3]**). The renderer keeps a **virtual canvas cursor**: while the real cursor is inside the stage rectangle it maps 1:1 onto the staged window's quad and is drawn as a small cursor quad; when it leaves the stage band, Lua warps it back to the band edge and the renderer carries the excess delta into canvas space; when the virtual cursor enters another window's rect the renderer restages that window (`.hover` v4) and the real cursor lands at the matching point. Parked windows are unreachable by the pointer, and sliver windows are `no_follow_mouse`: in a 30 s real-mouse sweep over slivers focus never left the staged window (0 changes vs 23 without the prop) **[measured S3]**. A *click* on a sliver still focuses it, so the warp-back at the band edge is required, not optional.

**2D (windowed / `--display` flat / spectator host):** a click in the SDL window casts a ray from the pixel → restage + warp; the physical cursor then lives in the canvas output and the XR cursor shows where it is; a fixed chord (the recenter double-tap, or `SUPER+CTRL+G` twice) warps it back to the SDL window. `publishHover` is no longer gated on `direct` in canvas mode (today `main.cpp:1004`).

**Popups/menus** are clipped to the base surface in toplevel export **[verified: protocol text]**. Because the stage band is non-overlapped, the staged window is additionally captured with the *existing* `DesktopCapture` via `zwlr_screencopy_manager_v1.capture_output_region` on the canvas output (a `RegionSource` behind `FrameSource`) — this restores menus, tooltips, dropdowns and the native cursor for the one window that matters. **Shipped in M3**, not later.

**Dialogs/child windows:** a new window whose pid matches a canvas window and is dialog-sized (< 60 % of the parent) is placed centred over the parent, staged (focused tier) and focused immediately.

**Fill/"fullscreen" flow:** `SUPER+F` in canvas mode → Lua resizes the staged window to the FOV-derived fill size (§5.2) and the renderer dollies so it fills ≈ 90 % of the view; toggling restores with phantomat's three-case rule. Never Hyprland `fullscreen`. Two defences **[measured S10]**: (1) `suppress_event="fullscreen maximize"` stops fullscreen *requested by the client* (browser F11, players: the window stayed at its size, state 0); (2) fullscreen *dispatched by the compositor* (Omarchy's `SUPER+F` → `hl.dsp.window.fullscreen`) is **not** blocked by the rule (the window went to state 2 at 2560×1440), so `SUPER+F` **must** be rebound in canvas mode (§5.5) and a `window.fullscreen` guard reverts any fullscreen that slips through and triggers Fill instead.

### 3.5 Version requirements

- Hyprland ≥ 0.56 with the Lua config (already required by the marketplace installer); `hyprland_toplevel_export_manager_v1` ≥ v2 **[verified in 0.56.2]**. The renderer checks the global at startup and reports a clear error if `--canvas` is requested without it.
- XR controls Lua version **5 → 6**. `backend.controls_hint()` expects 6; Studio blocks canvas mode with a "reinstall integrations" hint when `controls.version < 6`; the renderer refuses `--canvas` when `<runtime>/controls.version` is < 6 (an old adapter only parses `.hover` `v2`/`v3` **[verified: `xr-controls.lua:309-312`]**).
- No new renderer packages: vendored protocol XML only (`hyprland-toplevel-export-v1.xml` and `wlr-foreign-toplevel-management-unstable-v1.xml`, the latter link-only). Search text input uses a Quickshell layer-shell prompt shipped with the Studio plugin (§5.4), no xkbcommon. Virtual pointer (`zwlr_virtual_pointer_manager_v1` v2 **[verified: advertised]**) is a later latency upgrade replacing the Lua warp round-trip.
- Hybrid iGPU/NVIDIA machine: `GpuCapture` must import on the same render node as `DirectOutput`'s GBM device. M0 captured and rendered entirely on the iGPU (`TGL GT1`) with zero import failures **[measured S1]**.

### 3.6 M0 go/no-go spike — DONE, verdict GREEN (2026-09-25)

Deliverables: `tools/spike_window_capture.cpp` (`make spike-canvas`, excluded from `all`/`check`), `tools/spike_canvas.py` (drives the tests through `hyprctl eval '<lua>'`), `tools/spike_search/shell.qml`, the two vendored protocol XMLs, and [`docs/canvas-spike.md`](canvas-spike.md) with every measurement. Neither no-go fallback (region capture, stage output) was triggered.

| # | Question | Result |
|---|---|---|
| S1 | Capture by address, DMA-BUF import, under load on the hybrid GPU. | **Pass**, with the load redefined: the pre-M0 hot set (4 × 1080p at 60 Hz + 16 warm) reaches only 21–29 fps on the GT1; latency (commit + output frame per request), not throughput, caps a single-request window near 30 fps. Two requests in flight fix it. Zero import failures in every run. |
| S1b | Tier profile 60 / 24 × 4 / 10 / idle, 50 windows, stereo render on. | **Pass**: 59.5–59.9 / 23.9–24.0 / 10.0 fps, Hyprland CPU 7–13 %, worst render p99 8.9 ms with phase staggering (42 ms without). Overview ceiling ≈ 400 captures/s of 720p (≈ 370 Mpix/s). |
| S1c | Small canvases and the rate ladder. | **Pass**: exported Mpix/s is the only budget that matters (≤ 330 always passes, ≥ 435 always fails). Pulled parked clients draw at the pull rate up to ≈ 30 Hz. Ladder in §4.4. |
| S2 | Pile vs park, promotion. | Parked windows are **throttled to ≈ 20 Hz, not frozen**; `ignore_damage=1` returns 60 captures/s, and pulled clients draw new content at the pull rate up to ≈ 30 Hz (S1c); promotion delivers fresh frames < 0.5 s, full rate within 1 s. |
| S3 | Lua names, staging, focus, mouse sweep. | **Pass**: stage + focus + warp 6.5 ms, typed input arrived; real-mouse sweep 0 focus changes with `no_follow_mouse` (23 without). Names recorded in §3.3/§3.4; `no_border`, `window.move`, `window.resize` do not exist. |
| S4 | Pull cost. | Pulling 8–16 parked windows costs less Hyprland CPU than live slivers (7 % vs 10–12 %). |
| S5 | Output teardown race. | **Pass**: 40/40 create/remove cycles under two live captures, no failure, no crash (nested Hyprland). |
| S6 | ext-image-copy-capture. | Skipped (informational). |
| S7 | Quickshell search prompt. | **Pass**, D4 confirmed: text arrived intact through 10 s of cursor motion; focus returned to the staged window on close; 222 ms cold start, so keep the prompt loaded. |
| S8 | Restart under a held lease. | Skipped (informational; live switch is single-process). |
| S9 | Workspace guard. | **Pass**: the guard runs within the dispatch; captures continue; empty workspace garbage-collected. SUPER+SHIFT+n and scratchpad not scripted. |
| S10 | Fullscreen. | Client-requested fullscreen blocked by `suppress_event`; **compositor-dispatched fullscreen is not** → SUPER+F rebind required, `window.fullscreen` guard as backstop. |
| S11 | Output scale legibility. | All of 1.0 / 1.25 / 1.5 comfortable; **1.0 is the default** (D3). M2: terminal text on the cylinder is legible in the 2D window at 1920×1080; the glasses recheck is **pending** (M2 PR checklist). |
| S12 | Sliver frame callbacks / composite cost. | Subsumed: slivers work (S2, 60 fps) but are no longer the main tier. |

Not measured: eDP frame-time p99 and package watts (optional, `sudo turbostat` during a `tiers zoomed-in` run); Hyprland CPU is recorded instead.

---

## 4. Architecture

### 4.1 New components / files

| File | Contents |
|---|---|
| `protocols/hyprland-toplevel-export-v1.xml`, `protocols/wlr-foreign-toplevel-management-unstable-v1.xml` | Vendored (already present from M0); `Makefile` scanner rules mirroring `wlr-screencopy` (added to `GEN_HEADERS`/`APP_OBJS`); listed in `package-release.py`. The second is link-only. |
| `src/frame_source.hpp` | `class FrameSource { update(CapturedFrame&); service(); setDemand(visible,w,h); setFrameRate(fps, inFlight, phase); setIgnoreDamage(bool); transport(); requests(); requestToReadyMs(); error(); alive(); }`. `DesktopCapture` implements it unchanged in behaviour. |
| `src/window_capture.hpp/.cpp` | `WindowCaptureHub`: one `wl_display`, one `gbm_device`/EGL display shared by all windows (`GpuCapture` gains a constructor taking a device instead of opening its own render node), binds toplevel-export + dmabuf + shm, `pump()`. `WindowCapture : FrameSource`: handle = low 32 bits of the address; frame machine adapted from `DesktopCapture::Impl` but **always keeps ≥ 1 request outstanding** (2 staggered for the focused window) and fires at the window's assigned phase; `ignore_damage=1` for parked windows; releases full-size slots when idle; keeps only the downsampled texture. `RegionSource : FrameSource` (M3, shipped as `RegionCapture` in `src/capture.{hpp,cpp}`): `capture_output_region` of the staged window's rectangle on the canvas output (staged window only). |
| `src/surface.hpp` | `SurfaceView{layout*, texture, width, height, status*, halo, visible, alpha, label}`, the per-surface view yielded by `View::forEachSurface` (§4.2) in both modes. |
| `src/canvas_model.hpp` | `canvas::Rect`, `Camera{focusX, focusY, zoom∈[zMin,1], targets, anchor}`, `project(rect, camera, period)`, `zoomAt(anchor, factor)`, `fitBounds(bounds, fovH, fovV, pxPerDeg)`, `workZoom(rect, fov…)`, `fillSize(fov, pxPerDeg, scale)`, `wrap(period)`, `toLayout(rect) → PanelLayout`; the ring constants (§4.3). Pure, header-only. |
| `src/capture_cadence.hpp` | `Cadence`: per-window request times on the hub clock — lanes staggered by one period, phase, shared epoch, no backlog after a stall (M2). Pure. |
| `src/canvas_placement.hpp` | `placeNew()` (periodic ring search, parent-aware), `arrange()`, `neighbour(dir)`, `nudge()`, `snap()`, `overlaps()`/`freeAt()`/`inBand()`. Pure. (`rowFor(y)` lives in `canvas_model.hpp`; `category()` arrives with search in M4.) |
| `src/canvas_search.hpp` | `fold()`, `tokens()`, `scoreToken()`, `rank(query, records, mru, origin)`, `Mru`. Pure. |
| `src/canvas_memory.hpp` | `Memory::load/save/claim/note` for `<state>/canvas-memory.tsv` (AsyncFile, debounced 1 s, ≤ 256 entries). |
| `src/window_list.hpp` | `.windows` parser → `std::vector<WindowRecord>`; `.cursor` parser. |
| `src/canvas_scene.hpp` | `canvas::Scene`: `windows`, hub, `Camera`, states, search, tiers, virtual cursor; `adopt(records)` (diff by address, like `replacePanels`), `geometry()` (projected `PanelLayout`s, `output=address`), `visibleCandidates(heading)` (angular cull), `draw(ctx)`, `drawOverlay(ctx)`, `occluders(heading)`, `tick(now)`, verbs `overview/land/search/fill/cycle/neighbour/nudge/summon/pin/arrange/undo/recenter/zoomBy/pan/follow`. Every function < 80 lines (lint), delegating to the pure headers. |
| `src/canvas_labels.hpp` | M2 part of the overlay: per-window label atlas (Pango/Cairo raster keyed by `address+title`, LRU-trimmed to 512), drawn above each window. |
| `src/hex_token.hpp`, `src/gl_texture.hpp` | Shared helpers (M2): the mailbox hex token codec used by `window_list.hpp` and `LiveControls`; texture parameters and the BGRA raster upload used by the View, the scene, labels and the notification HUD. |
| `src/canvas_overlay.hpp` | Pango/Cairo rasters (reusing `notification_content.hpp` helpers): search palette, MRU switcher, radar strip, F1 help, per-window labels (atlas keyed by `address+title`); body-locked placement through `notifications::space` primitives; screen-space path for 2D. |
| `src/capture_governor.hpp` | Pure rate decision (§4.4): inputs per window (`adaptive::Plan`, in-view flag, projected angular size, staged/pinned/MRU rank, pixel size), global inputs (zoomed in/out, pixel budget in Mpix/s, measured request→ready p50, GPU p80 from `GpuTimers`, VRAM estimate); outputs `{rate step, inFlight, phase, ignoreDamage, place: stage|sliver|park}` with hysteresis. |
| `config/xr-controls.lua` | `publishWindows`, `publishCursor`, `stageWindow`, `selectCanvasWindow`, `focusByAddress`, workspace/fullscreen guards, rule enforcement, canvas key set with `binding:set_enabled` takeovers, `.mode` reader; version 6. |
| `studio/canvas.py` | `CanvasSession`: journaled create/adopt/remove of the canvas output, workspace rules, migration/restore (`canvas-session.json`), `canvas.json` validation → `canvas.tsv`, exclusions, `.mode` mailbox with the takeover flag. |
| `studio/backend.py` | `renderMode` in `presentation.json`; `set_render_mode`; `viewer_command` branch; `start` without monitors; `present`/`action_present_direct` branch; `status()` fields; `redistribute_laptop_windows` → per-window adoption; `terminal()`; `reconcile_outputs` reinstalls rules; `relocate_workspaces(exclude=canvas)`; actions `canvas_overview/search/fill/arrange/focus`. |
| `studio/MonitorStudio.qml` | Mode selector; Canvas settings tab replacing the Monitors tab content in canvas mode; canvas verbs on the Controls tab; `activeCount`/footer gates; performance labels. |
| `notifications/…` (Studio plugin) | `SearchPrompt.qml`: layer-shell keyboard sink on the canvas output, publishes `.search`. |
| `docs/window-canvas.md`, `docs/canvas-spike.md`, `THIRD_PARTY_NOTICES.md` | User guide, spike results, phantomat BSD-3 notice. |
| Tests | `tests/canvas_model.cpp`, `canvas_placement.cpp`, `canvas_search.cpp`, `canvas_memory.cpp`, `window_list.cpp`, `capture_governor.cpp`, `canvas_focus.cpp` (main.cpp include trick), `canvas_preview.cpp`; `tests/test_canvas.py`; `tests/controls.lua` additions; `tests/qml/tst_mode_selector.qml`, `tst_search_prompt.qml`; `tests/live_canvas.py`. All new unit binaries are added to `UNIT_BINS`/`run-units` (`Makefile:16`, `:120`, `:129`) and to `check-san`. |

### 4.2 The scene-mode seam in `src/main.cpp`

`View` gains `SceneMode mode`, `std::unique_ptr<canvas::Scene> canvas`, three accessors and one draw helper:

- `const std::vector<PanelLayout>& sceneGeometry()` — `geometry` in monitor mode; `canvas->geometry()` (already projected) in canvas mode.
- `template<class F> void forEachSurface(F)` — iterates `panels` or `canvas->windows`, yielding `SurfaceView{layout*, texture, width, height, status*, halo, visible, alpha, label}` (`src/surface.hpp`).
- `template<class Walk> void drawSurfaces(Walk&& candidates)` — the `drawHalo`/`drawPanel` loop lifted out of `drawEye`; `candidates(visit)` yields `SurfaceView`s (no ctx argument: the GL state is the View's own); monitor mode passes `forEachSurface`, canvas mode passes a walk over `visibleCandidates(heading)`.
- `Cylinder sceneCylinder()` — `{cx, cy, span, distance, workspace}`; canvas: `{0, 0, 2π·R − gap, R, {degrees=360, follow=true, gap}}` (so `workspaceBend`'s `k = 2π/(span+gap)` yields radius exactly R **[verified: `curvature.hpp:34`]**).

**Every** `geometry`/`panels` consumer, and how it changes (nothing is renamed; the three `#include "../src/main.cpp"` tests keep compiling):

| Consumer (main.cpp) | Monitor mode | Canvas mode |
|---|---|---|
| `drawEye` `:800-803` | unchanged | `forEachSurface(drawHalo, drawPanel)` over `canvas->visibleCandidates(heading)`; then `canvas->drawOverlay()`; then the HUD. |
| `skyHidden` `:806-816` | unchanged | test only the staged window. |
| `projectPanels` `:764-774` | unchanged | `adaptive::project` on candidates → `CaptureGovernor` → per-window `setDemand`/`setFrameRate`. |
| `sampleTarget` `:263-281` | unchanged | `targeting::query(ray, candidates, cyl)`; `selection.validate()` against the **unculled** window list; dwell radius in projected px (constant angle). |
| `placeNotification` `:1018-1030`, depth loop `:1025` | unchanged | `scene.tessellate(occluders(heading), cyl)`, depth = staged window centre or 0.85·R (§6.1). |
| `serviceCaptures`/`updateCaptures`/`reconnectCaptures`/`dropCapture` | unchanged | via `FrameSource*`; hub `pump()` once per tick; window-closed = `alive()==false`, no backoff. |
| `ensureLease` `:464-496`, `finish` `:1046-1063` | unchanged | also release/regenerate hub, per-window textures/slots, overlay textures; canvas textures are recreated after the lease returns. |
| `draw()` halo easing `:837` | unchanged | `forEachSurface`. |
| `sceneBounds()` `:947`, `boundsOf` `:661`, placeholder panels `main():1127`, `safe()` `:209`, `spatial::safeDistance` | unchanged | **never called** in canvas mode (`--canvas` skips placeholders and bounds; placement is non-overlapping by construction, `distance = R`). |
| `overviewDepth()`/`maxZoomDepth()` `:213-214`, `writeStats` | unchanged | not called; stats report canvas values (`mode`, `canvasWindows`, tiers, `zoomLevel` mapped `overview|window|pane`←`Overview|Work|Fill`). |
| `fit()` `:261` (sets `targetPanZ = distance − overviewDepth()`) | unchanged | → `canvas->overview()` (canvas zoom, `panZ` untouched). |
| `fitSelection`/`focusSelected` `:294`, `aimAt`, `recenterSelected` `:338`, `recenterOn`/`headingOnly`, `zoomedPose` `:363`, `panSelected` `:374`, `beginPan`, `easePan` `:727`, `zoomBy` `:405`, `followZoom`, `gazeFocus`/`lockZoomGaze`, `fitOutput` `:418`, `fitPane` `:441`, `flickIn`/`flickOut` `:430-438` | unchanged | all routed through one `View::navigate(Verb, args)` switch that calls `canvas->…`; the canvas uses `navigation::rectDistance`/`aimAt` math for the eye dolly (§4.3) with `panZ ∈ [0, R − 0.3]`. |
| `adoptLayout`/`reloadLayout` `:608-659` | unchanged | polls `canvas.tsv` (settings) instead of `viewer.tsv`; the window list comes from `controls->windows`. |
| `steer()` `:694-715` | unchanged (modes > 7 ignored) | the input table below. |
| `recordWork` smoke exit `:958` | unchanged | counts frames for the focused and near windows. |
| `main()` `:1083-1143` | unchanged | `--canvas FILE` (exclusive with `--layout`/`--capture`); `environment.tsv`/`gaze.tsv`/`tracking.tsv` still resolve from `dirname(FILE)` (`:973,1067`). |

**Input sources → canvas actions** (every path is gated; tests in `canvas_focus.cpp` assert `panZ` stays in `[0, R−0.3]` and neither `overviewDepth` nor `safe` is called):

| Source | Monitor action | Canvas action |
|---|---|---|
| `.controls` mode 1 / 2 (flick out / in) | `flickOut`/`flickIn` | Fill→Work→Overview / Overview→Work (land on gazed, else search selection, else MRU) and Work→Fill |
| mode 3 (double tap) | recenter | Work: aim at staged window; Overview: aim at bounds centre |
| mode 4 / 5, controls `total`, wheel, pinch | `zoomBy` | Overview: canvas `zoomAt(latched anchor)`; Work: eye dolly (`zoomDepth` clamped to `[0, R−0.3]`) |
| mode 6 / 7 | notification flick | unchanged |
| modes 8–17 (canvas key set, §5.5) | ignored | `overview, search, fill, mru_next, mru_prev, arrange, neighbour(token), nudge(token), pin, help` |
| `.pan` (4 fingers) | `panSelected` | `focusX/Y += delta/zoom` |
| `.focus` (Hyprland active window / workspace) | `fitOutput` (needs `OMXR-`) | `canvas->follow(address)` with the 90 % visible rule; `OMXR-` gate applies only in monitor mode (`live_controls.hpp:95`) |
| `.pane` | `fitPane` | not published in canvas mode |
| pose socket `fit`, `fit_target`, `fit_center`, `zoom_in/out`, `recenter` | monitor verbs | `overview` toggle, land on gazed, land at heading, zoom as above, recenter; new verbs `search`, `fill`, `arrange`, `focus:<addr>`, `mode:<monitors|canvas>` (M6), `pause_captures`/`resume_captures` |
| SDL `F`, `R`, wheel, right-drag, middle-drag, `/`, `Tab`, `Esc`, text | fit/recenter/zoom/look/pan | Fill toggle / recenter / zoom / look (unchanged) / focus pan / search / MRU / close palette then quit / search text |
| SDL click | – | ray → restage + warp (§3.4) |

### 4.3 Data model

**Ring geometry.** The canvas is a 2D pixel plane wrapped on the shared cylinder of radius `R` (world units; 900 px = 1 unit as everywhere). Canvas pixels per degree `pxPerDeg = 900·R·π/180` (37.7 at R = 2.4, ≈ the glasses' native 1080 px / 28°, so 1:1 canvas pixels are 1:1 display pixels). Period `P = 360·pxPerDeg` (13 572 px). Angle `θ = x/pxPerDeg`, height `y` (up negative, like layouts). Rendered with `spatial::pose` in follow mode with `Workspace{degrees=360, follow=true}`, `span = 2πR − gap` — `tests/canvas_model.cpp` asserts ring centre = eye.

**FOV-derived constants** (all computed from `fov` and `aspect()` at runtime, never hard-coded; defaults shown for fov 28°, 16:9, R 2.4):

| Quantity | Formula | Default |
|---|---|---|
| view width/height in canvas px | `fovH·pxPerDeg`, `fovV·pxPerDeg` | 1810 × 1055 |
| row height | `0.8·fovV·pxPerDeg`, snapped to 50 | 850 px |
| rows | row 0 at eye height, at most ±1 extra row → vertical extent within ±25° pitch | 3 rows max |
| Work zoom for a window | `min(1, 0.9·viewW/w, 0.9·viewH/h)` | 0.85 for a 1920×1080 window |
| Fill size (buffer px) | `0.9·viewW × 0.9·viewH` | 1630 × 950 (= logical at scale 1.0; logical 1300 × 760 at scale 1.25) |
| Overview zoom | `fitBounds`: used bounds → 1.0× view width, 0.9× view height; if that makes the median window < 3° wide, cap the arc at ±45° yaw (a "wide overview" the head pans across) | – |
| dwell radius | existing `radiusPx` interpreted in projected px = constant angle (120 px ≈ 3.2°) | – |

**Canvas camera** (`canvas::Camera`): `focusX, focusY` (canvas px), `zoom ∈ [0.08, 1]`, eased targets (τ 0.15 s), a **latched zoom anchor** (set when a zoom gesture starts, eased, so 3DoF jitter does not wobble the content). Projection:

```
x' = focusX + wrap(x − focusX)·zoom     y' = focusY + (y − focusY)·zoom
w' = w·zoom                              h' = h·zoom
```

Canvas zoom is used **only for ≤ 1** (Work fit and Overview). Getting closer (Work zoom gesture, Fill) reuses the existing eye dolly: `panZ` toward the staged window via `navigation::rectDistance`/`aimAt` (as `fitPane` does), clamped to `[0, R − 0.3]`. Moving *toward* the front window never exits the ring, and windows never overlap on the cylinder surface.

**Window record** (`canvas::CanvasWindow`): `address, cls, title, pid, xwayland`; `pixelW, pixelH` (buffer px); `Rect rect` (canvas px; `w,h == pixelW,pixelH` except during Fill); `PanelLayout layout` (projected, per tick); `unique_ptr<FrameSource> source` (+ optional `RegionSource` when staged); `CapturedFrame frame; GLuint texture; width, height`; `captureStatus, retryAt, retryMs`; `halo, alpha, visible, quality`; `int rateHz; Place place; double rateSince, lastFrame, focusStamp; unsigned focusHistoryID`; `optional<Rect> beforeFill; optional<Rect> pinnedAt`; `bool matched, staged, pinned; optional<string> parent`.

**Persistence.**
- `<state>/canvas-memory.tsv` (renderer, phantomat format + `camera\tx\ty\tzoom`).
- `<state>/canvas.json` (Studio) → `<state>/canvas.tsv` (renderer, hot-reloaded every 250 ms): `# canvas v1 fps radius gapPx dimUnmatched labelDeg outputScale captureBudgetMpix adoptPolicy takeoverKeys refreshHz` plus `exclude <pid|class>` rows. `outputScale` is the canvas output's scale (1.0/1.25), which the renderer needs for the Fill buffer→logical conversion; `takeoverKeys` (field 9) arrived in M4; `refreshHz` (field 10, 30–240, default 60, from M5) is the canvas output refresh the backend applies through `hl.monitor`, carried only for the governor's request→ready calibration threshold.
- `<state>/canvas-session.json` (Studio): per-window origin state for restore.
- `<state>/presentation.json`: `"renderMode": "monitors"|"canvas"`.
- Runtime mailboxes: `.windows`, `.cursor` (Lua→renderer), `.hover` v4 and `.tiers` (renderer→Lua: `v1 pid seq stamp <address> sliver …`, the complete sliver set sorted by address, written on change at most every 500 ms and refreshed by the 1 s heartbeat with the same seq; unlisted canvas windows are parked, a missing or stale file means no slivers; readers also accept `park` pairs), `.mode` (backend→Lua: `v1 canvas|monitors <takeover 0/1> stamp`; the flag is the Studio switch for the optional chords, §6.5), `.search` (prompt→renderer: `v1 owner seq <hex text> <open 0/1> stamp`).

### 4.4 Capture scheduling: tier caps and the pixel-budget ladder

Decided and verified in M0 **[measured S1b/S1c]**. The one resource that matters is **exported pixels per second**, Σ(w × h × rate) over all captured windows: every run at ≤ ~330 Mpix/s met its targets and every run at ≥ 435 Mpix/s failed, regardless of window count (Hyprland exports full-size buffers, so thumbnails do not help). The ceiling on the dev GT1 is ≈ 350–370 Mpix/s.

`CaptureGovernor::plan()` runs per tick on the culled candidate set plus a cheap pass over the rest:

| Tier | Who | Rate cap | In flight | Where in Hyprland |
|---|---|---|---|---|
| **Focused** | the staged window | always **60 Hz**, counted first against the budget (≈ 124 Mpix/s at 1080p) | 2, staggered | stage band |
| **Near** (zoomed in) | the ≤ 4 other visible windows with the largest angular size, then most recent use | ladder step ≤ 40 Hz (M2 fixed profile: 24 Hz) | 1 (2 when the rate is > 30 Hz) | park + pull; sliver if > 30 Hz |
| **Far** (zoomed in) | remaining visible windows | ≤ 10 Hz | 1 | park + pull |
| **Overview** (zoomed out) | every visible window | ≤ 10 Hz (`min(10, budget / Σ pixels)`; 50 × 720p run at 6–7 Hz) | 1 | park + pull |
| **Idle** | off-screen | 0 (session kept alive, no `copy`; last texture kept ≤ 256 px, full-size slots released) | – | park |

**Ladder rule.** Steps are `60 › 40 › 30 › 24 › 20 › 15 › 10 › 6`. Windows are assigned in order of visual importance (angular size in view, then MRU); each gets the highest step, within its tier cap, that keeps Σ(w × h × rate) ≤ **budget**. The budget defaults to **300 Mpix/s** (headroom under the measured 330–370) and is a Studio setting. It **self-calibrates**: shrink when measured request→ready p50 exceeds 2 output frames, grow slowly while it stays under 1 (**M5 tuned**: a healthy export completes on the second output frame, 33.2–33.3 ms p50 at 60 Hz in every M5 run, so the shipped thresholds are > 2.5 frames for two evaluations in a row to shrink and < 2.25 frames for 5 s to grow; see the M5 notes). **Hysteresis**: lower a rate as soon as the sum is over budget; raise only after 2 s under 90 % of the budget. With 1080p windows on the dev machine that yields: 2 windows → 60/40 (the Near cap; the spike's S1c table says 60/60, which the cap rules out); 4 → 60 + 24–30; 6 → 60 + 15–20; 11 → 60 + 4 × ~15 + 10; overview of 30 × 720p → 10 each; 40 × 720p → 6 (300 / 36.9 = 8.1 → step 6; S1b measured 40 × 10 Hz clean at 369 Mpix/s, so the budget setting has room above the default).

**Phase staggering is mandatory.** Every window gets its own phase within its period (windows at the same rate spread evenly). Without it, synchronised 10 Hz requests cause 42 ms render spikes; with it the worst p99 is 7–9 ms and request→ready falls 2.5× **[measured S1b]**.

Other rules: park ↔ sliver moves go to Lua via the `.tiers` mailbox at most every 500 ms with ≥ 2 s hysteresis, while staging on an explicit action is immediate (§3.4); the staged window's `RegionSource` capture (its stage rect × 60 Hz) replaces its toplevel export while region frames keep coming (decided and measured in M3, see the M3 notes), so it counts once against the pixel budget; GPU p80 (from `GpuTimers`, the `SpectatorGovernor` recipe) above 60 % lowers the budget by 25 % per 350 ms step, above 85 % all non-focused windows drop to 6 Hz; VRAM estimate capped at 512 MB (focused: 2 slots + tiled texture; near/far: downsampled; idle: thumbnail). **M2** ships the fixed S1b profile (60 / 24 × 4 / 10 / idle, 10 zoomed out, phases, 2 in flight for the focused window). **M5** adds the ladder, self-calibration, GPU feedback and the VRAM cap.

### 4.5 Rendering: stereo and 2D spectator paths

`renderScene` → `drawEye` serves direct stereo (`draw()`), the SDL window and `presentSpectator` **[verified]**; canvas draws through the same `drawHalo`/`drawPanel` helpers with:

1. `alpha` on `drawPanel` (search non-matches at `dimUnmatched` 0.35; Overview dims non-gazed to 0.8) and a thin accent rim for matches (`HaloShader` at low intensity).
2. **Labels**: in Overview always, at a fixed angular size (`labelDeg` 0.8° x-height: app icon/class + shortened title) above each thumbnail; in Work only when the window is ≥ 6° tall.
3. **Body-locked overlays** (palette, switcher, radar, help): berthed at 0.9·R with the notification lazy-follow (`retargetAt` after 0.3 s), drawn after the cylinder with depth test off; the spectator gets them through `drawEye`; in 2D they are screen-space quads.
4. **XR cursor**: a small quad on the staged window's quad at the virtual cursor position.

**Culling**: each window keeps an angular interval `[θ0, θ1]` and row after projection; `visibleCandidates(heading)` filters by `heading ± (fovH/2 + margin)` before any tessellation, for drawing, `targeting::query` (new optional candidate list parameter, asserted equal to brute force in `tests/targeting.cpp`), `skyHidden` and notification occluders. Per-frame cost is proportional to visible windows.

---

## 5. UX spec

### 5.1 Canvas layout and auto-placement

- Rows of `rowHeight` (850 px) around the ring, `gapPx` 60; row 0 at eye height, then one above, then one below (never more: ±25° pitch). Windows taller than a row are allowed (they span rows in placement collision terms) but placement prefers the ring before adding rows.
- A window wider than `P/2` is clamped.
- **New window**: (1) session position, (2) `Memory::claim()` by class/title, (3) parent rule (dialogs: centred over the parent, staged), (4) `placeNew()`: start at the camera focus, ring search 1..16 in 8 directions with step `(size + gap)`, periodic collision test, snap 20 px. Then a 300 ms halo pulse and, if outside the FOV, an edge cue (reuse `notification_draw.hpp` cue; both deferred to M6). The camera flies to it only if Hyprland focused it (§5.7).
- **Arrange** (`CTRL+A` in the prompt or the SDL window, Studio button, pose verb; from Work or Fill it ends in Overview): category grouping (categories by their most recent window, classes inside) + shelf-pack along the ring from the canvas point at the view heading, rows filled before adding one; undo/redo.
- **Move by hand**: `SUPER+SHIFT+arrows` nudge one grid step; `Shift+Enter` in search = summon to the heading; in Overview a mouse drag (2D or real mouse) moves the gazed window along the ring (deferred to M6). Head-drag is deferred (no hold-able gesture in the current control set).
- **Pin**: keeps a window body-locked at 0.85·R (lazy-follow) until unpinned; pinned windows always count as near.
- **Closed windows** fade out over 200 ms; memory keeps their slot.

### 5.2 Camera states

| State | Camera | Input |
|---|---|---|
| **Work** | `zoom = workZoom(staged window)` (≤ 1), focus = staged window centre, eye dolly `panZ` from the zoom gesture; head look free | real mouse/keyboard into the staged window; gaze only halos; explicit actions move focus (§5.6) |
| **Overview** | `zoom = fitBounds(...)`, focus = used-bounds centre (or of matches during search), `panZ = 0`; labels on; non-gazed dimmed; radar strip shown | gaze halos; flick-in / `fit_target` / click / Enter lands; wheel/pinch zooms about the latched anchor; 4-finger pan moves focus; **typing starts search** in an Overview entered from Work or Fill (the prompt opens with it; otherwise `SUPER+CTRL+G`, see M4 notes) |
| **Search** | Overview + palette; camera follows the best match (`followSelection`, target moved up so the palette never covers it) | text; `↑/↓/Tab` cycle; `Ctrl+1..8` pick; `Enter` land; `Shift+Enter` summon; `Esc` clears, second `Esc` reverts camera and focus (`returnFocus` snapshot) |
| **Fill** | staged window resized to `fillSize`, `zoom = 1`, eye dolly so it covers ≈ 90 % of the view; a soft leash (head turns > 15° pan within an oversized window, top-left kept visible) | as Work; `SUPER+F`/flick-out restores (three-case rule) |

Transitions are eased (τ 0.15 s zoom, 0.1 s rotation) and never jump; dwell is suppressed while `cameraMoving()`. Flick out: Fill→Work→Overview. Flick in: Overview→Work (land), Work→Fill.

### 5.3 Zoom and pan

- Continuous zoom (controls `total`, wheel, pinch): Overview → `zoomAt(latchedAnchor, exp(±0.12))` clamped `[0.08, 1]`; Work → eye dolly. Crossing from Work below `0.9·workZoom` enters Overview semantics; crossing back exits.
- 4-finger pan → `focusX/Y += delta/zoom`. Recenter as in the table.

### 5.4 Search

- **Indexed**: title, class/initialClass, category, workspace name (0.4), MRU (from `focus_history_id` + landing stamps). Also non-canvas windows (`canvas=0`) ranked ×0.7 with a "bring to canvas" affordance (settles D9). Re-ranked per keystroke; ≤ 512 windows in < 1 ms.
- **UI**: body-locked palette (720×≤480 raster): query line, ≤ 8 rows with highlights, category chip, "N of M"; the ring dims non-matches, matches get a rim, the best match a full halo.
- **Text input** **[D4 resolved, measured S7]**: a Quickshell layer-shell keyboard sink (`SearchPrompt.qml`, `omarchy-xr-search`, `WlrLayershell.keyboardFocus: Exclusive`) on the canvas output, opened by the search chord or on entering Overview, publishing `.search` on every edit; Hyprland returns focus to the staged window by itself on close. It stays loaded inside the Studio plugin (hidden, not relaunched: a cold start costs 222 ms). While it is open the renderer suppresses pointer warps. In windowed 2D mode SDL text input is used instead. The renderer draws the palette in all cases.

### 5.5 Keybindings, gestures, Lua controls

The five configurable Studio actions keep their file format (`controls-settings.tsv`: `fingers` + 5 lines; no change to `settingKeys`/`descriptions`/`controlDraft`) and get canvas meanings: `fit_all` → Overview toggle, `fit_target` → land on gazed window, `recenter`, `zoom_in/out`. Gestures (3/5-finger flick, 4-finger pan, double tap) map as in §4.2.

The **canvas key set** is fixed (not user-configured) and active only while the viewer runs in canvas mode. Chords that Omarchy already binds are taken over with `binding:set_enabled` (the pattern of `xr-controls.lua:181-239`) and released on exit — their meaning stays what the user expects **[verified collisions: `SUPER+F` fullscreen, `ALT+TAB` cycle, `SUPER+arrows` focus, `SUPER+TAB` next workspace, `SUPER+SLASH`, `SUPER+CTRL+A` audio]**. **D5 resolved**: the `SUPER+F` takeover is **always on** in canvas mode (Omarchy's dispatch would fullscreen the staged window to the whole output; `suppress_event` cannot stop it **[measured S10]**); the other takeovers default on behind a Studio switch "Take over Omarchy window keys in canvas mode". Lua publishes them with an explicit action→mode map (never `ipairs` index):

| Mode | Action | Chord | Token |
|---|---|---|---|
| 8 | `overview` | `SUPER+TAB` (takeover, optional) | – |
| 9 | `search` | `SUPER+CTRL+G` (free in Omarchy **[verified]**) | – |
| 10 | `fill` | `SUPER+F` (takeover, **required**; `window.fullscreen` guard as backstop) | – |
| 11 / 12 | `mru_next` / `mru_prev` (hold-to-show switcher; release lands) | `ALT+TAB` / `ALT+SHIFT+TAB` (takeover) | – |
| 13 | `arrange` | `Ctrl+A` as a prompt key (the prompt holds the keyboard in Overview; no submap; mode reserved) | – |
| 14 | `neighbour` | `SUPER+arrows` (takeover) | hex `left|right|up|down` |
| 15 | `nudge` | `SUPER+SHIFT+arrows` (takeover, optional: Omarchy's *Swap window*) | hex direction |
| 16 | `pin` | `SUPER+ALT+P` (free **[verified M4]**; `SUPER+O` is Omarchy's *Pop window out*) | – |
| 17 | `help` | `F1` as a prompt key (no submap; mode reserved) | – |

`live_controls.hpp`: modes ≥ 8 accepted only in canvas mode, require the stamp, and require a target only for 14/15 (the `mode>7` check at `:194` and the "≥ 6 needs target" rule at `:202` change accordingly). `input_settings.py` adds `slash`/`period` to `KEYS` and exempts takeover chords from the clash validator in canvas mode.

### 5.6 Gaze, hover, dwell

- `Hit.output` = address; dwell radius constant in angle.
- **Work: dwell only halos.** Focus moves on explicit actions: `fit_target`/flick-in on the gazed window, `SUPER+arrows`, Alt-Tab, search landing, a click, or the virtual cursor crossing into another window. (Dense small targets make dwell-focus steal keyboard focus while typing.)
- Overview/Search: dwell = selection + halo; landing uses the gazed window if a dwell settled within 2 s, else the search selection, else MRU.
- `Hud::interacting()` and an overlay under gaze (the view-centre ray meets a drawn overlay quad; a shown palette alone does not) suppress dwell; 400 ms `hoverFocusSettling` after keyboard/flick jumps via `interactionUntil`.

### 5.7 Focus follow (Hyprland → renderer)

On `window.active` Lua publishes `.focus` with the address; the renderer moves the camera only if that window is < 90 % visible (phantomat `canvasWindowOnScreen`), eased. Explicit user actions beat gaze (mirrors `tests/workspace_focus.cpp`). New windows are followed only when Hyprland focused them.

### 5.8 Glasses vs 2D

- Glasses: the ring is physical — head turns walk along windows at Work zoom; Overview compresses the used ring into the view (or ±45°); overlays are body-locked at 0.9·R; the radar strip along the lower view edge shows ring positions and heading.
- 2D and spectator: same scene; mouse look, wheel zoom, middle-drag pan, click to focus, `/` search, `F` Fill, `Tab` MRU, `Esc`; overlays are the same body-locked quads, which read as screen space in 2D (M4 notes); the spectator shows exactly what the wearer sees.

---

## 6. Integration and preservation (per feature)

### 6.1 Notifications
- `Hud` construction (`main.cpp:1068`) becomes `!posePath.empty()` so `--display` and windowed runs show cards (the uncommitted `drawEye` change already draws them in mono). **[decision D6]**
- `space::Scene::monitors()` → `tessellate()` (alias kept; named `tessellate` because `Scene::surfaces` is already the facet member); canvas feeds ≤ 24 angular-culled visible windows, so `findPlace()`'s 1250-berth search stays bounded; `depth` = staged window centre (or 0.85·R).
- **Inside-the-ring routing**: in canvas mode `route()`/`outerRadius()` are capped at `R − 0.3` and berths are searched in a view-relative lower band (independent of rows, so cards are inside the ±14° view), and `Hud::draw` runs with depth test off after the scene — cards can never fly behind the window wall or land far overhead. `tests/notification_space.cpp` adds: dense 360° wall, path never exceeds R, berth within the view band.
- Flicks 6/7, dismissal JSON, Quickshell service: unchanged; `notification_controls.cpp` also runs with `mode=Canvas`.

### 6.2 Live controls
- `LiveControls`: `.windows`/`.cursor`/`.search` parsers; `.hover` v4 (v3 in monitor mode) + mirror; `.focus` accepts `0x` addresses in canvas mode; modes ≤ 17 in canvas mode; `.tiers` writer; heartbeat/ownership/stamp rules unchanged.
- Lua v6: every `^OMXR%-` gate gets a `canvasMode` branch (`.mode` mailbox); `publishPane` is a no-op in canvas mode; `followWorkspace` publishes addresses; guards, rule enforcement, key takeovers; `tests/controls.lua` extended (v6 activation, window hover → stage + warp, address focus, modes 8–17 with tokens, guards, 5-key settings unchanged).
- Studio: `controls_hint()` expects 6; canvas verbs whitelisted in `camera_control`.

### 6.3 Environments, theme, sky culling
Unchanged (`environment.tsv` from `dirname(--canvas FILE)`); `skyHidden` tests only the staged window.

### 6.4 Tracking
`PoseSocket`, One-Euro, prediction, `waitForPose`, `vblank`: unchanged; new verbs parsed in `PoseSocket::update` before `camera.accept`.

### 6.5 Studio: mode toggle, settings, backend lifecycle
- **Toggle**: segmented "Virtual monitors | Window canvas" in the XR session card (`MonitorStudio.qml:894-928`), persisted as `renderMode` (pattern of `set_spectator`/`action_spectator`). **M3**: disabled while `viewing`. **M6**: live switch in one renderer process — the backend performs the Hyprland side (create canvas output + migrate windows, or restore + remove) and sends `mode:<m>` to `pose.sock`; the renderer swaps scenes without touching `DirectOutput`/`Dedicated`/SDK. During the switch `remove()` is called with `relocate_workspaces(exclude=canvas)` so nothing lands as a hidden workspace on the canvas output; a test asserts `layout.json` is byte-identical after monitors→canvas→monitors and no hidden workspace remains on the canvas output.
- **Settings**: Canvas tab (radius, gap, dim, label size, canvas output refresh 60/120 and scale 1.0/1.25, capture budget in Mpix/s, adopt policy, exclusions, key takeover switch for the optional chords) → `canvas.json` → `canvas.tsv`.
- **Lifecycle**: `apply()` in canvas mode = `CanvasSession.ensure()` (journaled, no `persist_applied`), rules, migration. `viewer_command` emits `--canvas <state>/canvas.tsv` and never requires `applied["monitors"]`. `status()` adds `renderMode`, `canvasWindows` (from `pose.sock.stats`); `active` = 1 so existing gates work; footer "Window canvas · N windows". `redistribute_laptop_windows()` adopts windows per address; `terminal()` execs `foot` (opens on the focused canvas workspace and is adopted by the `window.open` handler).
- **Recovery/cleanup**: canvas output journaled in `outputs.json`; `stop_viewer` → `CanvasSession.restore()` (per-window origin) → rules removed → `remove()`; `recover_journal` also restores from `canvas-session.json`; `reconcile_outputs` reinstalls rules after `hyprctl reload`. Renderer: `ensureLease`/`finish` cover hub, textures, overlays; window-closed is normal.

### 6.6 Stats, smoke, docs
`pose.sock.stats` keeps `pid/fps/time/zoomLevel`, adds `mode`, `canvasWindows`, `tiers`. `--smoke-test --canvas` uses `--canvas-windows-file` (synthetic list) and requires ≥ 1 frame for the focused window and each near window plus the stereo-distinct check. `docs/architecture.md` gains "Scene modes"; `docs/window-canvas.md` is the user guide.

---

## 7. Phased implementation plan

### M0 — Spike & decisions — **DONE** (2026-09-25, verdict GREEN)
**Delivered**: `tools/spike_window_capture.cpp`, `tools/spike_canvas.py`, `tools/spike_search/shell.qml`, `make spike-canvas`, both vendored protocol XMLs, [`docs/canvas-spike.md`](canvas-spike.md) with S1–S5, S7, S9–S11 measured (S6/S8 skipped as informational, S12 subsumed).
**Outcome**: toplevel export chosen; park + pull replaces the frozen park and the sliver strip; capture policy and ladder fixed (§4.4); scale 1.0 and 60 Hz defaults (D3); Quickshell prompt (D4); SUPER+F takeover required (D5); Lua names recorded (§3.3/§3.4). Still to wire into the main build: `Makefile` scanner rules for both XMLs in `GEN_HEADERS`/`APP_OBJS` and `package-release.py` (M2, with the hub).

### M1 — Scene seam refactor, no behaviour change (1 week)
**Changes**: `src/frame_source.hpp` (+ `DesktopCapture` adapter); `src/surface.hpp`; `View::sceneGeometry/forEachSurface/sceneCylinder/navigate/drawSurfaces`; `SceneMode` with only `Monitors` live; `GpuCapture` device injection (shared device optional); `notification_space.hpp` `tessellate()` entry point (`monitors()` alias kept); zero-panel guards; tolerant `readLiveSettings`; `writeStats mode`; new Makefile target **`check-preview`** with a committed baseline PNG (taken after committing the mono-HUD change) and `notification-preview` diff; `UNIT_BINS` wiring for future tests.
**Acceptance**: `check-preview` diff = 0; `make check`, `check-san`, `check-notifications`, `check-workspace-focus`, `smoke`, `check-lint` green.
**Shippable**: yes (neutral refactor).

### M2 — Window capture hub + canvas scene, renderer-only — **DONE** (2026-09-25; glasses checklist pending)
**Changes**: `src/window_capture.{hpp,cpp}` (hub, shared device, `WindowCapture` with a request always outstanding and per-window phase, `ignore_damage` pulls), both protocol XMLs into `Makefile`/`package-release.py`, `canvas_model.hpp`, `canvas_placement.hpp`, `canvas_memory.hpp`, `window_list.hpp`, `canvas_scene.hpp` (Work/Overview, follow, labels), **fixed-profile `capture_governor.hpp`** (focused 60 Hz with 2 in flight, ≤ 4 near at 24, far 10, idle off-screen, 10 zoomed out, phase staggering; no ladder), `main.cpp`: `--canvas`, `--canvas-windows-file`, `navigate` routing, candidate culling in `targeting::query`, `ensureLease`/`finish`, smoke; `THIRD_PARTY_NOTICES.md`.
**Acceptance**: the M2 harness (`tests/live_canvas.py`, reusing the `tools/spike_canvas.py` output/workspace setup) creates the headless output, stages the focused window by hand on the visible workspace and parks the rest (a parked window tops out at ≈ 30 Hz, so the focused-rate check needs a staged window); `--canvas` with a hand-written list shows live windows on the ring in stereo/mono/spectator (windows parked by hand on a hidden workspace capture fine); auto-placement and memory work; flick out/in toggles Overview/Work; 50 windows listed, focused ≥ 58 fps, near ≥ 23, far 10 at 60 fps stereo (`pose.sock.stats`), matching S1b; overview bounds ⊂ frustum; text legibility at scale 1.0 rechecked in the glasses on the cylinder (S11 caveat); lease loss recovers with textures regenerated.
**Tests**: `canvas_model.cpp` (projection, wrap, ring centre = eye, `zoomAt` invariants, `fitBounds` ⊂ frustum for N=60, FOV-derived constants), `canvas_placement.cpp` (no overlap on the periodic canvas, determinism, parent rule, arrange N≤200 < 5 ms), `canvas_memory.cpp`, `window_list.cpp`, `capture_governor.cpp` (tier assignment, near ≤ 4 by angular size then MRU, phases distinct per rate, hysteresis), `targeting.cpp` (candidates = brute force), `canvas_focus.cpp` (every verb keeps `panZ ∈ [0,R−0.3]`, never calls `overviewDepth`/`safe`), `smoke-canvas`, `live_canvas.py` (opt-in cadence).
**Shippable**: behind the flag, developers only.
**Measured** (dev machine, TGL GT1, windowed 1280×720 renderer on a `SPIKE-canvas` 2560×1440@60 output, `python3 tests/live_canvas.py profile zoomed-in-near-parked --seconds 45`; 50 windows, the last two settled Work reports after a round trip through Overview):

| Tier | Windows | Rate | Mono fps (min / avg) | Stereo (SBS window) fps |
|---|---|---|---|---|
| Focused (staged 1920×1080) | 1 | 60 | 60.0 / 60.0 | 60.0 / 60.0 |
| Near (parked, pulled) | 4 | 24 | 23.9 / 24.0 | 23.9 / 24.0 |
| Far (parked, pulled) | 21 | 10 | 10.0 / 10.0 | 10.0 / 10.0 |
| Idle | 24 | 0 | 0 | 0 |
| Overview (all visible) | 49 mono / 23 stereo | 6 mono (budget rule) / 10 stereo | 6.0 / 6.0 | 10.0 / 10.0 |

Hyprland CPU 6.7 % mono, 6.3 % stereo (S1b: 7–13 %); renderer 59.3–60.0 present fps; no GL errors. `profile zoomed-out-50 --seconds 30`: 50 × 720p in Overview at 6.0 Hz clean (the budget rule), Hyprland CPU 6.7 %. `make smoke-canvas` exits 0. S11 in the glasses, stereo Overview bounds, real flick controls and direct-mode lease loss are PR checklist items.

#### M2 notes

Decisions taken while implementing M2 (M3+ build on them):
- **Window source**: `--canvas-windows-file FILE` in the exact `.windows` format of §3.1, polled by mtime every 100 ms; only `v1` and a non-decreasing `seq` are checked (owner/stamp gates arrive with `LiveControls::updateWindows()` in M3, which reuses `windows::parse` unchanged). A torn, malformed or older list keeps the previous one. The staged window is the `stage` row, else `focus_history_id` 0; a change of the `focus_history_id` 0 row stands in for the `.focus` mailbox (follow with the 90 % rule). Titles longer than 550 bytes reject the list, so the M3 Lua writer must truncate.
- **`--canvas FILE`** names `canvas.tsv` (§4.3 header, defaults when absent, re-read every 250 ms) and anchors `environment.tsv`, `tracking.tsv`, `gaze.tsv` and `canvas-memory.tsv` by its directory. `View`'s constructor and `preview()` are unchanged; the scene is attached after construction.
- **Identity and geometry**: `PanelLayout.output`/`Hit.output`/`Selection.output` carry `0x` + lowercase hex; canvas px have y down like layouts; the ring is `Cylinder{0,0, 2πR − gap, R, Workspace{360, follow, gap}}` so the ring centre is the eye. The FOV formula gives a 1802 px wide view (not 1806) and a 1622 × 950 fill size.
- **Capture**: one hub (one `wl_display`, a GBM device from the EGL render node) and one `GpuCapture` per lane; the staged window runs two staggered lanes and drops duplicate presentation stamps before import; parked windows pull with `ignore_damage=1`, the staged window uses 0 and never times out while waiting for damage (a still window may legitimately wait). When two lanes are ready together only the newest stamp is imported (the other finishes as a duplicate); a size change rebakes the shown image, except from an SHM buffer the lane's next copy may be writing. Idle windows stop copying, keep a ≤ 256 px thumbnail and release full-size slots, but keep one frame open without `copy` (§3.2/§4.4: the export session stays alive, so the recording indicator does not flicker when windows cross the view edge); a window that goes away while idle still fails that frame and counts towards `alive()`. A closed or failed source takes its frame texture with it (`Scene::dropSource`), so a window never draws a texture name its capture already deleted. Other failures retry on the 0.5–5 s ladder. In the windowed renderer, `WindowCaptureHub::settle()` reads events for ≤ 2 ms after new requests because the SDL swap blocks a whole frame (without it the staged window ran at 30 fps). Lease loss and a lost connection recreate the hub instead of migrating it.
- **`canvas.tsv` fields**: `fps` caps every tier's rate (a staged window at ≤ 30 Hz runs one lane); `radius`/`gapPx` rebuild the ring; `exclude` rows re-adopt the last list at once; `labelDeg`, `outputScale` and `captureBudgetMpix` apply; `dimUnmatched` and `adoptPolicy` are parsed for M4/M3. The settled camera is saved as the `canvas-memory.tsv` camera row and restored in Overview at start when no window is staged.
- **Governor**: the fixed S1b profile plus two spike-backed additions: Overview drops to 6 Hz when 10 Hz would exceed the 300 Mpix/s budget, and Near membership has 0.5 s entry / 1 s exit hysteresis (Near never exceeds four; a window that qualified waits for a free place). The staged window stays at 60 Hz in Overview (§4.4 Focused: always).
- **Placement**: dialogs (floating, same pid as a tiled window; the mailbox has no parent field) only choose the start point, collisions are still resolved; `placeNew` is extended by a search along each row's centre line. Arrange groups by class (categories come with search, M4); arrange/neighbour/nudge are pure and unit-tested but not bound until M4. Memory entries are claimed by live windows; saves are debounced 1 s from the first unsaved change.
- **Camera**: aims reuse `navigation::frontFocus` on a one-pixel layout at the focus point with depth in [0.3, R]; the eye dolly is a translation of magnitude R − depth, asserted as `|pan| ≤ R − 0.3` (upper/lower rows raise the minimum depth to ≈ 0.52 m because the eye rises to the row). Canvas zoom is only used for ≤ 1, in Overview, about a latched anchor (the gazed point, else the focus); vertical pan is `focusY −= dy` because canvas y points down; zoom and pan aim at the current aim point, not the landed window's centre; Overview keeps a 4 % margin per side; the Work landing depth also fits the curved width; follow measures visibility from the actual eye depth. In Work a settled dwell only sets the selection and the landing candidate (no pointer warp).
- **Labels**: Overview (and Work windows ≥ 6° tall) only, a Pango atlas keyed by address + title, trimmed to 512, drawn after the surfaces without depth test. Body-locked overlays, search, radar, cursor and edge cues stay M3/M4.
- **Smoke**: the compositor-free `make smoke` stays monitor mode; `make smoke-canvas` is opt-in (`tests/live_canvas.py --smoke`); the in-renderer rule ignores dead sources.
- **Packaging**: `package-release.py` ships only the compiled renderer, so the vendored XMLs need no entry; their notices are in `packaging/licenses/WAYLAND-PROTOCOLS.txt`, and phantomat's BSD-3 text is `packaging/licenses/PHANTOMAT-BSD.txt`.
- **Hover** publishing stays monitor-only until `.hover` v4 (M3).
- **Harness**: `tests/live_canvas.py` maps the renderer with the spike client class so the spike rule floats it onto the `spikecanvas` workspace silently (no focus change, nothing tiles into the user's workspace); `pile` tiers of the spike profiles park in M2 (no slivers yet); `write` without `--stage` stages the most recently focused client on the spike canvas workspace.

Open items found by the acceptance run (not M2 blockers):
- **Ring capacity**: at R 2.4 the ring holds 3 × 13 572 px of row length; the S1b set (1 × 1920, 10 × 1280, 39 × 960 px wide, plus gaps) needs ≈ 55 000 px, so 25 windows find no free place and are stacked at the focus (logged `Canvas: no free place`), where their Work labels show over the staged window. Capture rates are unaffected. M4's arrange/overflow policy has to choose: extra rows beyond ±1, a larger radius, or smaller idle windows.
- **Overview labels** sit 8 px above their window and, at small zoom, overlap the bottom of the window in the row above (constant angular height vs. a 130 px row gap).
- **Work landing after Overview** in `zoomed-out-50` cost one report at 52 fps (≈ 25 windows going idle at once); watch it when M5 adds the ladder.
- The windowed stereo SBS view has a narrower per-eye aspect than the glasses, so its Overview shows fewer windows (23 of 49); the stereo frustum check belongs in the glasses.

### M3 — Control plane: Lua, backend, Studio toggle, input — **DONE** (2026-09-25; glasses checklist pending)
**Changes**: `xr-controls.lua` v6 (`publishWindows` dirty/heartbeat, `publishCursor` from `hl.get_cursor_pos()`, `stageWindow` (stage ↔ park; slivers arrive with `.tiers` in M5), guards incl. the `window.fullscreen` revert, `SUPER+F` takeover (Fill itself lands in M4; until then the chord only keeps Omarchy's fullscreen off), rule enforcement with `border_size=0`, address focus, `.mode` reader incl. the takeover flag); `studio/canvas.py` (output at scale 1.0 / 60 Hz by default, park workspace as the migration target); `backend.py` (renderMode, `set_render_mode`, `viewer_command`, `start`, `present`/`present_direct` branch, `status`, `redistribute_laptop_windows`, `terminal`, `reconcile_outputs` rules, `relocate_workspaces(exclude)`, `FakeHypr` extensions); `MonitorStudio.qml` selector (disabled while viewing) + gates + Canvas tab skeleton; `live_controls.hpp` (`.windows`, `.cursor`, hover v4 + mirror, address focus, modes ≤ 17); renderer virtual cursor + XR cursor quad; `RegionSource` for the staged window; SDL click path; `canvas_focus.cpp` explicit-beats-gaze; version gating (`controls_hint`, renderer check).
**Acceptance**: from Studio (stopped) choose Window canvas → Start stereo: canvas output created, windows migrated to the park workspace, rendered at the S1b rates; click/type into any window via `fit_target`, neighbour keys, or mouse crossing (stage + warp ≤ 50 ms; the Lua part measured 6.5 ms in S3); menus and cursor visible on the staged window; a 60 s real-mouse sweep with the laptop screen off never moves focus; SUPER+3 / SUPER+SHIFT+3 / scratchpad do not freeze the canvas; SUPER+F and browser F11 never fullscreen; stop returns every window to its origin workspace, tiled state restored; crash recovery cleans output and rules; monitor mode and `layout.json` untouched.
**Tests**: `test_canvas.py` (mode persistence; `viewer_command` per mode × {direct, present, windowed}; exactly one output at scale 1.0/60 Hz by default; `layout.json` unchanged; per-window restore; laptop-off adoption; no foreign hidden workspace on the canvas output; rule reinstall; exclusions; version gate), `controls.lua` additions (v6, hover v4, guards incl. fullscreen revert, takeovers with `SUPER+F` always on, settings still 5 keys), `tst_mode_selector.qml`, `canvas_focus.cpp` (Hyprland focus → smooth follow; explicit beats gaze; dwell never restages in Work), `window_list.cpp` cursor parser.
**Shippable**: yes — first user-facing Window canvas (Overview + gaze + keys, no search yet).
**Delivered** in five work packages: WP1 renderer control plane (`live_controls.hpp` v6 mailboxes, `.mode` heartbeat, hover v4, XR/virtual cursor, SDL click, version gate); WP2 `config/xr-controls.lua` v6 with `tests/controls.lua`; WP3 `studio/canvas.py` `CanvasSession` and backend `renderMode` with `tests/test_canvas.py`; WP4 `studio/ModeSelector.qml`, the Studio toggle and Canvas tab with `tests/qml/tst_mode_selector.qml`; WP5 the region source, stats, [`docs/window-canvas.md`](window-canvas.md) and this checklist.
**Measured** (dev machine, TGL GT1, windowed 1280×720 renderer on `SPIKE-canvas` 2560×1440@60, `python3 tests/live_canvas.py profile --seconds 45`, 50 windows; staged 1920×1080 window):

| Staged window source | Focused fps (Work, Overview) | request→ready (5 s samples) | Near / far / overview | Hyprland CPU |
|---|---|---|---|---|
| Region (`RegionCapture`, two lanes taking turns) | 60.0 / 60.0 | 33.2–33.6 ms | 24.0 / 10.0 / 6.0 | 7.2 % (7.4 % in a second run) |
| Export (`OMARCHY_XR_NO_REGION=1`, the M2 path) | 60.0 / 60.0 | 33.3–33.7 ms | 24.0 / 10.0 / 6.0 | 6.9 % |

The two are equal within noise; the region adds ≈ 0.3–0.5 points of Hyprland CPU for menus, tooltips and the native cursor, so it stays the default while healthy. `make smoke-canvas` exits 0 with 9 of the staged window's 10 smoke frames from the region source.

#### M3 notes

Decisions taken while implementing M3 (M4+ build on them):
- **`.mode` is written by the renderer**, not the backend: the `LiveControls` heartbeat writes `v1 <pid> canvas|monitors <takeover> <stamp>` once a second with the same owner gate as `.active`, so canvas mode is tied to the live session and developer runs without Studio work. The takeover flag is fixed to 1 in M3; the optional chord switch flows through `canvas.tsv` in M4.
- **SUPER+F takeover = unbind + rebind**: Hyprland 0.56.2 has no `hl.get_binds` and Omarchy's `o.bind` discards its handle, so canvas entry runs `hl.unbind("SUPER + F")` plus our own bind, and exit rebinds Omarchy's default `hl.dsp.window.fullscreen({mode="fullscreen"})` ("Full screen"). A user-customised SUPER+F comes back with the next `hyprctl reload` (known limitation). The `window.fullscreen` guard reverts with `fullscreen_state{internal=0,client=0}` and publishes mode 10 (Fill, a logged no-op until M4).
- **Workspace-matched rules**: the backend installs the workspace and window rules in one place, matched by `workspace="name:omxr-canvas"`/`"name:omxr-park"`; rule handles have `set_enabled` but no remove, so stop and recovery disable them. Lua enforces `float` and the six `set_prop` values for windows that arrive later; `suppress_event` exists only in the rule, and the fullscreen guard is the backstop.
- **Region replaces export**: `RegionCapture` screencopies exactly the staged window's rectangle on the canvas output (so texture size, `sizeFromBuffer` and the pixel budget are unchanged) with the cursor included. While a region frame arrived within 0.5 s (`regionShown`), the export gets `setDemand(false)` (idle, session kept alive) and its idle thumbnail is kept out of the window's frame; after 0.5 s without one, or on an error, the export takes over again, and a failed region source retries on the 0.5–5 s ladder (logged once per rectangle). A moved or resized staged window reopens it at the new rectangle; unstaging closes it. The XR cursor quad is hidden while `regionShown`. `OMARCHY_XR_NO_REGION=1` keeps the export (troubleshooting, measurements).
- **Why two lanes**: Hyprland answers an output screencopy ≈ 19 ms after the request, a little over one output frame, which the render loop sees two ticks later, so one request in flight tops out at 30 Hz (measured 12 fps with the plain `DesktopCapture` gate, 20 fps with the settle, 30 fps with the GPU release gate relaxed). Two `DesktopCapture` lanes on their own connections take turns with one request per period, never both in one output frame, giving 60 Hz; a region lane requests again right after `ready` in GPU mode (the slots rotate), always uses a full `copy` (so static content keeps delivering and `regionShown` is a true health signal) and times out after 3 s like an unsubmitted request. The windowed renderer also settles the region lanes for ≤ 2 ms (the export hub already does). Monitor mode is unaffected (no region, same gates).
- **`.windows` header extension**: `v1 owner seq stamp outX outY outName` (4 or 7 fields accepted) so the renderer computes output-local region coordinates and the harness can name `SPIKE-canvas`; rows keep global `at`.
- **`.cursor` mailbox**: `v1 owner seq x y ox oy stamp` with a cumulative overflow beyond the staged rectangle (coalescing never loses motion); the overflow resets on restage and when the cursor leaves the canvas output. Lua rewrites it at least every 0.4 s because the renderer hides the XR cursor after 0.5 s without a line.
- **Pointer release**: the three-finger double tap (mode 3) in canvas mode also warps the cursor to the centre of the first non-OMXR monitor; SUPER+CTRL+G stays reserved for search (M4).
- **Version gate**: the renderer refuses `--canvas` only when no `--canvas-windows-file` is given and `<dirname(--pose-socket)>/controls.version` is missing or < 6 (the harness and tests keep working); Studio disables the selector segment with a hint, and `set_render_mode('canvas')` raises the same hint.
- **Migration order**: the backend journals every origin to `canvas-session.json` first, then moves each window to `omxr-park`, floating tiled windows at their journaled size (invisible on the hidden workspace); Lua handles later arrivals (`window.open`, `move_to_workspace`). Restore runs per window before the output is removed; un-journaled leftovers go to the laptop's active workspace; a journal from another Hyprland instance is dropped.
- **Region only on the stage**: `chooseStaged` falls back to the focus_history_id 0 window when no row is `stage` (session start, the staged window closed), but that window is parked, so only a `stage` row opens a region, maps the XR cursor, or replaces the export. The region follows the governor's rate for the staged window (capped at 60 Hz), and when both lanes answer in one tick the newer request's frame wins (`RegionTurns`, unit-tested with fake lanes).
- **Focus follow**: the focus_history_id 0 stand-in for `.focus` runs only for `--canvas-windows-file`; the mailbox path follows `.focus` alone, so XR's own staging never moves the camera.
- **Leaving the canvas restores the origin**: Lua drops the six `set_prop` overrides with `value="unset"` (verified live on Hyprland 0.56.2) on SUPER+SHIFT+n and when canvas mode ends, and the backend does the same for every window it returns on Stop. SUPER+SHIFT+n reads the window's origin from the backend journal (`canvas-session.json`), falling back to Lua's own record, and re-tiles it or restores its floating size and position.
- **Exclusions**: `canvas.json` `exclude` classes and pids are never adopted by the backend; Lua reads the `exclude` rows of `canvas.tsv` and sends an excluded window that lands on the canvas output to the laptop's active workspace.
- **Guards**: a special workspace shown on the canvas output is closed there and toggled open on the laptop's workspace. The monitor id `canvas` (and ids ending in `-canvas`) is reserved, since output names are the prefix plus the id.
- **Stage band** is the whole output in M3 (no sliver strip until M5): the staged window sits at the output origin, is shrunk to fit it, and cursor confinement clamps to its rectangle. `adoptPolicy` `all` (default) migrates every regular window at start; `empty` is the Studio setting carried in `canvas.tsv` field 9.

Open items found by M3 (not blockers):
- Windows Lua adopts later (not in the backend journal) return to the laptop's active workspace on Stop, still floating.
- The harness renderer window overlaps the staged 1920×1080 window on `SPIKE-canvas`, so the region frames include part of the renderer (a harness artefact; rates are unaffected).
- The Studio Canvas tab and the `DecimalField` were only linted against the stubs, not run in the real Quickshell Studio.

**M3 glasses PR checklist**
- [ ] Studio (stopped) → Window canvas → Start stereo: canvas output created, windows migrate to `omxr-park`, `pose.sock.stats` shows focused ≥ 58 / near ≥ 23 / far 10.
- [ ] Click/type into windows via `fit_target`, mouse crossing and the SDL click (stage + warp ≤ 50 ms).
- [ ] Menus, tooltips and the native cursor visible on the staged window (`pose.sock.stats` `stage.shown` true).
- [ ] 60 s real-mouse sweep with the laptop screen off never moves focus.
- [ ] SUPER+3, SUPER+SHIFT+3 and the scratchpad never freeze the canvas.
- [ ] SUPER+F and browser F11 never fullscreen.
- [ ] `hyprctl reload` mid-session: rules reinstalled, canvas keeps working, SUPER+F still taken over.
- [ ] Stop returns every window to its origin workspace with tiling restored; SUPER+F is Omarchy's again.
- [ ] `kill -9` the backend: recovery restores windows, removes the output and rules.
- [ ] `layout.json`/`viewer.tsv` byte-identical after a canvas session and monitor mode still starts.
- [ ] S11 text legibility in the glasses at scale 1.0 (carried from M2).
- [ ] Spectator and windowed preview show the XR cursor.
- [ ] Studio's Canvas tab (settings save, decimal fields, window count in the footer) in the real Quickshell Studio.
- [ ] Direct mode: the region source keeps the staged window at ≥ 58 fps (no windowed settle there; the capture service runs during the presentation wait).

### M4 — Navigation UX: search, Fill, switcher, arrange, pin, radar, help — **DONE** (2026-09-25; glasses checklist pending)
**Changes**: `canvas_search.hpp`, `canvas_overlay.hpp` (body-locked placement, screen-space 2D), `SearchPrompt.qml` + `.search` (kept loaded in the plugin), `canvas_scene` states Search/Fill, summon/nudge/neighbour/pin/undo, hold-to-show switcher, radar strip, F1 help; `PoseSocket` verbs; Studio View-controls canvas buttons; Lua key set 8–17 with the optional takeovers; Fill dispatch behind the `SUPER+F` takeover from M3 (the `window.fullscreen` guard now triggers Fill); SDL mapping.
**Acceptance**: open search with one chord in stereo (or by typing in Overview), camera follows the best match, Enter lands; Fill fills ≈ 90 % of the view with native text and restores; Alt-Tab shows the list while held; arrange + undo; pin stays body-locked; palette never covers the selection; all visible in spectator/2D.
**Tests**: `canvas_search.cpp` (phantomat ranking cases: origin penalty, AND tokens, accent folding, non-canvas ×0.7), `canvas_preview.cpp` (PNG smoke of overlays), `canvas_focus.cpp` (search landing beats gaze; Esc reverts; Fill three-case restore), `controls.lua` (modes 8–17, tokens, takeovers and their restore, `.fill`), `tst_search_prompt.qml`, `test_canvas.py` verbs and `canvas.tsv` field 9.
**Shippable**: yes — feature-complete canvas.
**Delivered** in five work packages: WP1 pure logic (`canvas_search.hpp` phantomat port, arrange grouped by category, `Undo` snapshots, the `.search`/`.prompt`/`.fill` codecs, `canvas.tsv` `takeoverKeys`); WP2 renderer states and verbs (Search/Fill, three-case Fill, hold-to-show switcher, arrange/undo/redo, neighbour/nudge/summon/pin, bring to canvas, modes 8–17, pose-socket verbs, SDL keys) with `canvas_focus.cpp`; WP3 `canvas_overlay.hpp` (palette, switcher, radar, F1 help, pinned quads) with `make check-canvas-preview`; WP4 the Lua v6 canvas key set with takeovers, release binds and the `.fill` consumer in `tests/controls.lua`; WP5 the Quickshell prompt (`SearchPrompt.qml`, `SearchPromptWindow.qml`, `tst_search_prompt.qml`), `canvas.tsv` field 9, Studio/backend canvas verbs, [`docs/window-canvas.md`](window-canvas.md) and this checklist.

#### M4 notes

Decisions taken while implementing M4:
- **Mailboxes** beside the pose socket (all `v1`, one line, replaced atomically):
  - `.prompt` (renderer → prompt): `v1 <pid> <seq> <open 0/1> <output|-> <stamp>`; `seq` counts every open and close, the line is rewritten with the heartbeat while open (boot-clock stamp), so a prompt older than 3 s belongs to a dead renderer and closes. The prompt converts the stamp with `/proc/uptime`.
  - `.search` (prompt → renderer): `v1 <owner> <promptSeq> <editSeq> <hex text|-> <open 0/1> <keys> <stamp>` with keys from `enter shift-enter up down tab shift-tab esc ctrl-1..ctrl-8 ctrl-a ctrl-z ctrl-shift-z f1`. The prompt owns the keyboard, so it forwards keys; `editSeq` restarts with every `promptSeq`; the text is UTF-8 hex cut at 550 bytes on a character boundary; the stamp is Unix time. `keys` is `-` for a text edit, else the key log: the keys since the last text edit, comma-separated, oldest first, at most 8, the newest being this line's. The mailbox keeps one line, so two keys between two renderer polls (Down then Enter, key repeat) would otherwise lose the first; the renderer replays the keys whose `editSeq` (counting back from the line's) it has not read. The renderer applies the text, then the unseen keys in order (a key that ends the session drops the rest). The prompt's first Esc is a plain edit to the empty text; the second sends `open 0` with key `esc`, which the renderer routes through its Esc rule (help closes first, otherwise close and revert); `open 0` with any other key closes keeping the landing.
  - `.fill` (renderer → Lua): `v1 <pid> <seq> <address> <w> <h> <stamp>` in logical px, applied only to the staged window. The renderer owns the fill size (0.9 of the view, so no soft leash) and the three-case restore; Lua only resizes.
- **Chords**: pin is `SUPER+ALT+P` (`SUPER+O` is Omarchy's *Pop window out*); `SUPER+CTRL+G` search and `SUPER+F` Fill are always bound in canvas mode; the optional takeover set is `SUPER+TAB`, `ALT+TAB`/`ALT+SHIFT+TAB` (with `{release=true}` binds on `ALT_L`/`ALT_R` publishing mode 11 token `release`, plus a 1.5 s landing fallback), `SUPER+arrows` and `SUPER+SHIFT+arrows` (nudge collides with Omarchy's *Swap window*). Exit rebinds Omarchy's default dispatchers from `default/hypr/bindings/tiling.lua`.
- **Takeover switch**: Studio → `canvas.tsv` field 9 (`takeoverKeys` 0/1 after `adoptPolicy`) → renderer settings → the `.mode` heartbeat flag → Lua (one live source).
- **No submap**: the layer-shell prompt opens (holding exclusive keyboard focus) when Overview is entered from Work or Fill and with every explicit search (`SUPER+CTRL+G`, `/`, Studio, pose verb), so arrange, undo/redo and help arrive as prompt keys; modes 13 and 17 stay reserved, and Studio buttons, pose-socket verbs and SDL `Ctrl+A`/`F1` cover them in any state (arrange from Work or Fill ends in Overview). An Overview reached otherwise (at start, after Esc reverted a search to an Overview snapshot) has no prompt, deliberately: reopening it would grab the keyboard at start and make the dismissing Esc useless; `SUPER+CTRL+G` brings it back with the prompt keys.
- **Palette clearance**: in Search the selection is centred with its bottom edge 1.5° above the full-height palette's top edge (berth pitch −5°, 480 raster px at 45 px/°) and its top 0.5° below the view top; 0.22 view heights above the aim when that fits. A selection taller than that band lowers the Search zoom until it fits (Esc restores the snapshot zoom).
- **Dwell under overlays**: dwell is suppressed only while the view-centre ray meets a drawn overlay quad (`notifications::space::gazeHit` over the last `overlayQuads`), so in Search gaze still selects windows above the palette and a flick-in lands on a dwell settled within 2 s.
- **Fill restore point**: filling again (the buffer differs from the fill size by 2 px or more: a user resize, a client minimum, or Lua's clamp to the output) keeps the rect from before the first Fill, so the window always restores to its pre-Fill size.
- **Pinned windows** ask for their native buffer size (they are never projected on the ring) and hold a Near place (at most four, pinned first) zoomed in and zoomed out.
- **Reserved chords**: Studio's hotkey validator rejects the canvas chords (`SUPER+F`, `SUPER+CTRL+G`, `SUPER+ALT+P` and the takeover set) in every mode, since canvas binds carry `XR:` descriptions the clash check skips; a profile saved before M4 still loads. The F1 help hides the takeover rows while the takeover switch is off.
- **Deferred to M5**: the new-window cues of §5.1 (300 ms halo pulse, off-FOV edge cue) and the Overview mouse drag that moves the gazed window along the ring. Nudge, summon and arrange cover moving windows in M4.
- **Prompt hosting**: `SearchPromptWindow.qml` (`WlrLayershell`, namespace `omarchy-xr-search`, overlay layer, exclusive keyboard only while visible) is instantiated once in `MonitorStudio.qml`, so it stays loaded with the plugin (`keepLoaded`). The renderer uses SDL text input only when its own window has keyboard focus; otherwise it asks the prompt. Fallback if a Studio version loads the panel lazily: move the host to `BarWidget.qml`.
- **Overlays, one code path**: palette, switcher, radar, help and pinned windows are body-locked lazy-follow quads at 0.9 (pinned 0.85) of the eye-to-ring distance, drawn depth-test-off after the surfaces in stereo, the 2D window and the spectator alike; no separate screen-space path.
- **Search index**: title ×1, class ×0.85, category ×0.55, MRU bonus `max(0, 36 − 4·rank)`, origin −24, non-canvas ×0.7 with *bring to canvas* (reusing the hover v4 staging path). Workspace names are not indexed (the `.windows` mailbox has none).
- **Ring overflow**: arrange packs by category, then class, along the ring; windows that do not fit keep their place (M2 behaviour). Extra rows and shrinking idle windows are deferred. Nudge is 100 px and may overlap; arrange and summon avoid overlap while `placeNew` finds room.
- **Studio**: `camera_control` also sends `overview search fill arrange undo redo pin help` in canvas mode (`ValueError` in monitor mode); the View controls relabel *Fit workspace*/*Fit monitor* to *Overview*/*Land on window* and add Search, Fill, Arrange and Undo; `status()` reports `canvasState`.

**M4 glasses PR checklist**
- [ ] SUPER+CTRL+G in stereo opens the search; typing in Overview (entered with SUPER+TAB or a flick out) searches too; the camera follows the best match and the palette never covers it; Enter lands, Shift+Enter summons, Esc clears then reverts.
- [ ] The Quickshell prompt holds the keyboard only while open; focus returns to the staged window on close; no pointer warp while it is open.
- [ ] A window off the canvas found by search is brought over and landed on.
- [ ] SUPER+F fills ≈ 90 % of the view with native text; the three-case restore (untouched, moved, resized); browser F11 fills instead of going fullscreen.
- [ ] ALT+TAB: a tap flips to the previous window, holding shows the list, releasing Alt lands (release binds fire); the 1.5 s fallback lands if they do not. Confirm one `hl.unbind("ALT + TAB")` drops both Omarchy binds.
- [ ] SUPER+TAB, SUPER+arrows, SUPER+SHIFT+arrows drive the canvas; with the takeover switch off they keep Omarchy's meaning while SUPER+F, SUPER+CTRL+G and SUPER+ALT+P stay canvas keys.
- [ ] Ctrl+A arranges by kind without overlap; Ctrl+Z / Ctrl+Shift+Z undo and redo.
- [ ] SUPER+ALT+P pins the window body-locked and it stays readable while turning; unpin puts it back.
- [ ] Radar strip and F1 help readable in the glasses; overlays lazily follow the head (no jitter within 12°).
- [ ] Spectator and windowed preview show the same overlays; windowed keys `/ F O P Tab Alt+arrows F1 Esc`.
- [ ] Stop: every taken chord has Omarchy's default binding again (`hyprctl binds -j`).
- [ ] Studio View controls (Overview, Land on window, Search, Fill, Arrange, Undo) in the real Quickshell Studio; the prompt is loaded after Studio was opened once and hidden.
- [ ] `make check-preview` pixel-identical (monitor mode unchanged).

### M5 — Capture scheduling: the pixel-budget ladder — **DONE** (2026-09-26; glasses checklist pending)
**Changes**: `capture_governor.hpp` `governor::Ladder` replaces the fixed S1b profile (§4.4 ladder `60 › 40 › 30 › 24 › 20 › 15 › 10 › 6` under a 300 Mpix/s budget filled tier by tier, importance order by angular size then MRU, tier caps, 2 s raise hysteresis, `Budget` with request→ready self-calibration and GPU-p80 feedback, VRAM cap, place hold, 2 in flight above 30 Hz); `canvas_scene.hpp` feeds it (presented size, `noteReady` from pulled non-staged exports, `noteGpu`) and derives the sliver set; `.tiers` writer (`window_list.hpp` codec, `live_controls.hpp` rate limit and heartbeat) and consumer in `xr-controls.lua` (sliver ↔ park moves, `no_follow_mouse`, the stage band clamp); stats `budget` block, per-window `place`/`inFlight`, `Budget:` and `Canvas: ladder` log lines; `canvas.tsv` field 10 (`refreshHz`), Studio's live budget readout (`status().canvasBudget`); `tests/canvas_ladder.py` (the settled ladder in Python) and `tests/live_canvas.py` extended (model verdict, `.tiers` stand-in, `--churn`, screencast counter, GPU-memory sample, `make check-canvas-live`); docs.
**Acceptance**: §8.3 budget met with 50 windows (1 focused, 4 near, rest far/idle) and with 2/4/6 × 1080p windows at the S1c rates under the Near cap (60/40, 60 + 24–30, 60 + 15–20); a 30 fps video in a near window plays without dropped captures; opening and closing windows never oscillates rates; no VRAM growth over 1 h; recording indicator steady.
**Tests**: `capture_governor.cpp` (the S1c table, overview 30/40/50 × 720p, budget never exceeded over random sets, raise and place hysteresis, calibration down/up and the one-slow-second rule, GPU steps and panic, VRAM cap order, fps cap, pinned, 512 windows in 0.02 ms), `canvas_focus.cpp` (`.tiers` rate limit and place hold, budget stats, calibration from exports only, GPU feedback, closed windows leave the ladder), `window_list.cpp` (`.tiers` codec), `canvas_model.cpp` (field 10), `controls.lua` (`testTiersSliver`, `testTiersPark`, `testStageBand`, `testTiersLeave`), `test_canvas.py` (field 10, `canvasBudget`), `test_canvas_ladder.py` (the model against the C++ table, panic), `test_live_canvas.py` (the harness verdict, `.tiers` reading, screencast and memory rules).
**Shippable**: yes.
**Delivered** in five work packages: WP1 the pure ladder with exhaustive unit tests; WP2 renderer integration (samples, `.tiers` writer, budget stats and logs); WP3 the Lua `.tiers` consumer and sliver strip; WP4 Studio/backend/`canvas.tsv` field 10 and the Python ladder model; WP5 the live harness, the acceptance runs below, the calibration tuning and the docs.
**Measured** (dev machine, TGL GT1, windowed 1280×720 renderer on `SPIKE-canvas` 2560×1440@60, `python3 tests/live_canvas.py profile NAME --seconds 45`; every run passed the model verdict: each window at the settled ladder's tier, rate, lanes and place for the renderer's effective budget, focused ≥ 58 fps, the others within 1 fps of their rate, used ≤ effective; the last two settled Work reports after a round trip through Overview. "In view" counts the windows the windowed renderer shows besides the staged one; `--fov 60` widens it):

| Profile | In view (Work) | Work: focused / near / far | Places | Overview | Used / effective Mpix/s | request→ready p50 | Hyprland CPU |
|---|---|---|---|---|---|---|---|
| `ladder-2` (2 × 1080p) | 1 | 60 / 40 | sliver | 60 / 10 | 207.4 / 300 | 33.2 ms | 4.9 % |
| `ladder-4` | 2 (+1 idle) | 60 / 40 × 2 | 2 slivers | 60 / 10 × 3 | 290.3 / 300 | 33.3 ms | 5.3 % |
| `ladder-4 --fov 60` | 3 | 60 / 24 × 3 | parked | 60 / 10 × 3 | 273.7 / 300 | 33.3 ms | 5.2 % |
| `ladder-6` | 2 (+3 idle) | 60 / 40 × 2 | 2 slivers | 60 / 10 × 5 | 290.3 / 300 | 33.3 ms | 5.4 % |
| `ladder-6 --fov 60` | 4 (+1 idle) | 60 / 20 × 4 | parked | 60 / 10 × 5 | 290.3 / 300 | 33.3 ms | 5.3 % |
| `ladder-2 --fov 100 --size 1600x1200 --all-in-view` | 1 (all) | 60 / 40 | sliver | 60 / 10 | 207.4 / 300 | 33.1 ms | 4.8 % |
| `ladder-4 --fov 100 --size 1600x1200 --all-in-view` | 3 (all) | 60 / 24 × 3 | parked | 60 / 10 × 3 | 273.7 / 300 | 33.2 ms | 5.3 % |
| `ladder-6 --fov 100 --size 1600x1200 --all-in-view` | 5 (all) | 60 / 15 × 4 / 10 | parked | 60 / 10 × 5 | 269.6 / 300 | 33.2 ms | 5.5 % |
| `ladder-11` | 7 (+3 idle) | 60 / 15 × 4 / 6 × 3 | parked | 60 / 6 × 10 | 286.2 / 300 | 33.2 ms | 5.5 % |
| `ladder-video` (1080p at 30 fps + 1080p) | 2 | 60 / 40 × 2 (video 40.1 fps) | 2 slivers | 60 / 10 × 2 | 290.3 / 300 | 33.3 ms | 5.1 % |
| `zoomed-in-near-parked` (50) | 25 (all 960×600) | 60 / 40 × 4 / 6 × 21, 24 idle | 4 slivers | 60 / 6 × 44, 5 idle | 289.2 / 300 | 33.2 ms | 7.3 % |
| `zoomed-in-stress` (50, all at vsync) | 25 | 60 / 40 × 4 / 6 × 21, 24 idle | 4 slivers | 60 / 6 × 44, 5 idle | 289.2 / 300 | 33.2 ms | 10.6 % |
| `zoomed-in-near-parked --stereo` | 25 | 60 / 40 × 4 / 6 × 21, 24 idle | 4 slivers | 60 / 10 × 23 (26 out of view) | 289.2 / 300 | 33.2 ms | 6.9 % |
| `zoomed-out-50` (50 × 720p, none staged) | 25 | – / 40 × 4 / 6 × 21, 25 idle | 4 slivers | 6 × 50 | 263.6 / 300 (276.5 in Overview) | 33.3 ms | 6.7 % |
| `ladder-6 --churn` | 2 (+3 idle) | 60 / 40 × 2 | 2 slivers | 60 / 10 × 5 | 290.3 / 300 | 33.3 ms | 5.4 % |

Every fps was within 0.2 of its rate (focused 60.0), the renderer presented 59.2–60.0 fps (one 53.4 fps report while `zoomed-out-50` landed in Work with ≈ 25 windows going idle, as in M2), calibration stayed at 1.00 with no GPU steps in every settled report, and steady-state `screencast` events were 0 in every run (1 in `--churn`, the new window's session). The `--churn` window (1080p, opened at 10 s and closed at 16 s) left the rates at the model with no ladder tier raised twice within 2 s. The 1 h run (`ladder-6 --seconds 3600`) held 60 / 40 × 2 slivers through 716 Work reports (719 in all) with calibration 1 and request→ready p50 33.16–33.33 ms; the renderer's DRM memory (fdinfo `drm-total`) was 329 MB at the first settled report and 329 MB at the end, the governor's VRAM estimate 144 MB throughout; 5 `Canvas: ladder` changes (start and the Overview round trip only), Hyprland CPU 5.6 %, 0 steady-state screencast events. The canonical all-in-view rows (60/40; 60 + 24 × 3; 60 + 15 × 4 + 10; 60 + 10 × 4 + 6 × 6; the S1b set at 60 / 30 × 4 / 10 × 6 = 290 Mpix/s) are pinned by `capture_governor.cpp` and `test_canvas_ladder.py`; the live runs reproduce the 2/4/6 rows with every window in view (`--fov 100 --size 1600x1200 --all-in-view`: 60 / 40 sliver, 60 + 24 × 3, 60 + 15 × 4 + 10), and `make check-canvas-live` runs exactly those three plus `ladder-6` at the default view (slivers). `make smoke-canvas` exits 0 with region frames.

#### M5 notes

Decisions taken while implementing M5:
- **Tier-wise water fill**: §4.4's literal "each window in importance order gets the highest step that fits" gives 60/40/40/6/6/6 for six 1080p windows, against the plan's own "6 → 60 + 15–20". The ladder fills tier by tier (Focused → Near → Far/Overview), each tier at one step, the highest that leaves 6 Hz for the tiers below; per-window rank still decides Near membership and the trim order. Near stays capped at four, so six 1080p windows in view are 60 + 4 × 15 + 1 × 10.
- **Idle beyond the budget**: when even 6 Hz does not fit, the lowest-ranked visible windows go idle (rate 0, session kept, last texture kept) instead of running over; over-budget runs collapse the focused window (S1b). With a staged 1080p window and 49 × 720p in a mono Overview, 31 run at 6 Hz and 18 freeze. The focused window keeps 60 Hz in Overview.
- **Raise and place hysteresis**: rates drop at once; a raise waits 2 s with nothing lowered and use under 90 %; a window without a rate in force (new, back in view) gets its target at once. A window rated above 30 Hz becomes a sliver without a further delay (the raise already waited); a place changes at most once per 2 s. Pinned windows never move place, so they are capped at 30 Hz; the staged window is always Stage.
- **Self-calibration** multiplies the Studio setting by a factor in [0.5, 1.0], evaluated every second with ≥ 8 samples from pulled exports: not the staged window (its region lanes measure ≈ 33 ms by construction, M3) and not a damage-driven sliver (its request→ready is the client's commit pace, not compositor load). **Tuned in WP5**: a healthy export completes on the second output frame (request→ready p50 33.1–33.3 ms at 60 Hz in every run above, 207–297 Mpix/s), so the plan's "shrink above 2 frames" sat on the healthy value and ratcheted the budget down to 243 Mpix/s within a minute (never growing back, since "under 1 frame" is never reached), and one slow second when 42 windows started at an Overview entry cost a 10 % shrink whose recovery re-admitted frozen windows one per second. Shipped: shrink 10 % per second once p50 exceeds 2.5 output frames (42 ms at 60 Hz) for two evaluations in a row; grow 2 % per second after 5 s under 2.25 frames (37.5 ms); overload in S1b/S1c measured 45–106 ms. It never exceeds the setting (raise the setting for more).
- **GPU feedback** mirrors `SpectatorGovernor`: p80 of the last 30 frame GPU times over the render period; > 60 % → −25 % per 350 ms step (at most five), > 85 % → every non-focused window capped at 6 Hz, recovery one step per 2 s under 45 %.
- **VRAM estimate** per window = capture lanes × 2 `GpuCapture` slots × 4 B × native pixels + the presented texture (the staged window also counts its two region lanes); idle = 256 × 256 × 4. Above 512 MB the lowest-ranked windows go idle; the staged window never does.
- **`.tiers` mailbox** (renderer → Lua): `v1 <pid> <seq> <stamp> [<address> sliver]...`, the complete sliver set sorted by address; written on change at most every 500 ms (seq + 1) and rewritten by the 1 s heartbeat with the same seq and a fresh boot-clock stamp; readers apply a newer seq only, parse `park` pairs but ignore them, and treat a missing or stale (> 2 s) file as "no slivers"; anything unlisted on the canvas workspace is parked.
- **Damage-driven only once confirmed**: a sliver's export switches to damage-driven capture only when Lua's `.windows` row reports it as `sliver`; until the move lands (the 500 ms `.tiers` limit plus Lua's own 0.5 s limit), or if Lua never moves it (excluded, not a member), it stays pulled, since a copy waiting for damage on a parked window would stall. A stale `.tiers` file resets Lua's seq, so the heartbeat after a renderer hitch restores the set.
- **Stage band clamp**: staging and Fill are clamped to output width − 8 px, so slivers at x = outX + outW − 8 are never covered; slivers stack 24 px apart, stopping 64 px (`SLIVER_FLOOR`) above the output's bottom edge; `no_follow_mouse` is set on entry and unset on stage, park, release and leave; Lua keeps the set in the reload-safe `omarchy_xr_canvas.slivers`.
- **`canvas.tsv` field 10** is the canvas output refresh (30–240, default 60), used only for the calibration thresholds; Lua's positional field-8 regex is unaffected.
- **Screencast events**: rate changes and park ↔ sliver moves post none (sessions stay alive; the lowest step, 6 Hz, is well under Hyprland's 500 ms stop timer). A window leaving the view does post `screencast>>0,window` ≈ 0.5 s later despite its open frame, and `1,window` when it returns, so a view change posts about one event per window entering or leaving the view (up to ≈ 50 at an Overview round trip with 50 windows). omarchy-shell's recording indicator checks for `gpu-screen-recorder` and ignores these events. The harness counts only events outside the 2.5 s after a view change it made. A cheap keep-alive for idle windows is a possible follow-up if a bar that reads these events flickers.
- **Live harness**: the verdict is model-based (`tests/canvas_ladder.py`, pinned to the C++ table): each report's windows become ladder inputs (native size; the rank proxy is the native width, ties broken by the renderer's own Near choice), the model runs at the report's effective budget and panic state, and the settled reports must match window by window. A report counts as settled when the last three reports carry the same rates (after a view change the budget may still admit windows). The windowed renderer shows only the windows in its view, so the 2/4/6/11 rows depend on the view size (`--fov`, `--size`); the S1b harness expectation "near ≥ 23" was a floor, the ladder yields 30 for the S1b set when all ten are in view.
- **Out of scope**: the M4 items deferred to M5 (new-window halo pulse, off-FOV edge cue, Overview mouse drag) are not in this entry and move to M6 (listed in its Changes and Acceptance); `no_follow_mouse` is not added to the backend's `DECOR_PROPS`, since Lua unsets it on every path.

**M5 glasses PR checklist**
- [ ] Recording indicator steady in omarchy-shell during a session with rate and place changes (zoom in/out, open and close windows); note any bar that reads Hyprland `screencast` events.
- [ ] A live sliver is never focused during a 60 s real-mouse sweep across the right edge of the canvas output (`no_follow_mouse`), and the staged window never covers the strip.
- [ ] A 30 fps video in a near window (two or three windows in view) plays smoothly in the glasses; `pose.sock.stats` shows it at its rate with no dropped captures.
- [ ] Rates in `pose.sock.stats` from a glasses run (stereo, real head motion): focused 60, the others at the ladder rate, `usedMpix ≤ effectiveMpix`, `calibration` at 1 in steady state.
- [ ] Studio's budget readout ("Using X of Y Mpix/s", self-limited, capture latency, live slivers) in the real Quickshell Studio while the canvas runs.
- [ ] `make check-canvas-live` passes on the dev machine; `make check-preview` pixel-identical (monitor mode unchanged).

### M6 — Live mode switch, preservation hardening, docs, release (1–2 weeks)
**Changes**: `mode:` datagram + single-process scene swap; the §5.1 items deferred from M4/M5 (new-window 300 ms halo pulse, off-FOV edge cue reusing the `notification_draw.hpp` cue, Overview mouse drag of the gazed window along the ring); backend live switch with `relocate_workspaces(exclude)`; notification inside-ring routing + tests; HUD in non-stereo (D6); `ensureLease`/`finish` audit; stats consumers; docs, README, `package-release.py`; version bump.
**Acceptance**: switching modes while in stereo keeps SBS and the lease (`test_live_mode_switch_never_restarts_viewer_or_sdk`); unit tests for the new-window pulse/edge cue (a window placed outside the FOV gets the cue, one inside only the pulse) and the Overview drag (the gazed window moves along the ring, snapped, persisted to memory); full matrix green: `make check`, `check-san`, `check-notifications`, `check-workspace-focus`, `check-environment`, `check-preview`, `smoke`, `smoke-canvas`, `check-ui`, `check-lint`; manual checklist in both modes × {stereo, glasses-flat, windowed, spectator}.
**Shippable**: release 0.4.0.

---

## 8. Risks, open questions, performance budget

### 8.1 Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Exported pixel rate saturating the compositor's render thread (every capture is a render pass; over ~350 Mpix/s on the GT1 rates collapse and request→ready climbs to 70–100 ms) | measured | 300 Mpix/s budget with self-calibration and the ladder (§4.4); focused window kept at 2 in flight; phase staggering (42 → 8 ms p99). Hyprland CPU stayed at 7–13 % **[measured S1b]**. eDP p99 and watts remain unmeasured (optional `turbostat` run). |
| Parked clients still draw at ≈ 20 Hz (throttled, not suspended; suspension is not available from Lua) → battery | medium | Idle windows are not pulled (≈ 20 Hz callbacks only); client CPU was 2–6 % in total for 40–50 mostly idle windows **[measured S1]**; power measurement still open. |
| Rate oscillation as windows open/close | low after design | lower at once, raise only after 2 s under 90 % of the budget; place moves ≥ 2 s hysteresis, never for the staged/pinned window. |
| Pointer reaching invisible windows | low, verified | parked windows are unreachable; slivers `no_follow_mouse` (0 focus changes in the S3 sweep); Lua warp-back at the band edge (a click on a sliver still focuses it); virtual cursor, XR cursor. |
| Workspace/fullscreen state changes freezing or resizing captures | low, verified | Lua guards inside the dispatch **[S9]**, workspace rules, `suppress_event` for client requests, **required** `SUPER+F` takeover + `window.fullscreen` revert for compositor dispatch **[S10]**. |
| Toplevel export delivers full-size buffers only → VRAM and capture cost | high without governor | idle thumbnails ≤ 256 px, slot release, 512 MB cap; budget in pixels, not window count. |
| #16317-class crash during output churn | low, verified | 40/40 create/remove cycles under capture with no failure **[S5]**; `pause_captures`/`resume_captures` kept as insurance. |
| Popups clipped | certain for non-staged windows | `RegionSource` for the staged window (M3); non-staged windows are not interacted with. |
| Single-request capture loop halves the rate | certain if `DesktopCapture`'s cycle is copied | `WindowCapture` always keeps a request outstanding **[S1]**; `capture_governor.cpp` and `live_canvas.py` assert the focused rate. |
| Text softer on the cylinder than on a direct display at scale 1.0 | low | S11 caveat; recheck in M2; 1.25 stays a setting. |
| Restore on stop losing tiling/workspaces | low after design | per-window `canvas-session.json`; restore before output removal; recovery path. |
| `View` refactor breaking include-trick tests | medium | M1 adds only; `check-preview` gate; those suites in CI for M1. |
| Lua/dispatcher name drift | medium | S3 records names; no `hyprctl dispatch` fallback needed. |
| Studio ↔ adapter version skew | medium | v6 gate on both sides. |
| Hybrid GPU import on the wrong node | medium | S1 checks render node; `GpuCapture` device injection. |

### 8.2 Decisions for the user
- **D1** At canvas start, migrate all regular windows onto the canvas (default) or start empty and bring windows explicitly (search "bring to canvas", SUPER+SHIFT+move)?
- **D2** *Moot*: per-toplevel capture passed M0 (S1/S2/S5); the region-capture fallback is not built.
- **D3** *Resolved (S11)*: canvas output 2560×1440@60, scale **1.0** (all of 1.0/1.25/1.5 legible; 1.0 shows the most and captures the cheapest). Scale 1.25 and 120 Hz remain settings.
- **D4** *Resolved (S7)*: Quickshell layer-shell search prompt, kept loaded in the Studio plugin.
- **D5** *Resolved (S10)*: `SUPER+F` takeover is mandatory in canvas mode with a `window.fullscreen` revert guard; `ALT+TAB`, `SUPER+arrows`, `SUPER+TAB` takeovers default on behind a switch; search on `SUPER+CTRL+G`.
- **D6** Enable the notification HUD in non-stereo runs whenever a pose socket exists.
- **D7** *Resolved*: no Hyprland fullscreen; Fill only (S10 shows why the rebind, not just the rule, is needed).
- **D8** Ring defaults: R 2.4 (≈ native pixel density), row height 0.8·vFOV (850 px), max 3 rows, Overview fits the view (else ±45°).
- **D9** *Resolved*: non-canvas windows appear in search with "bring to canvas".
- **D10** Live mode switch in M6 (single process) vs "stop first" only.

### 8.3 Performance budget (60 fps stereo, ≈ 16.6 ms/frame, dev TGL GT1 iGPU; the 120 Hz canvas output is an option measured in S1b, not promised)
- Capture rates (`pose.sock.stats`): focused ≥ 58 fps; near at the ladder rate: ≥ 23 fps for the S1b profile (4 × 720p near), 15–20 fps with six 1080p windows; far 10 fps; overview 10 fps up to ≈ 30 × 720p, then 6; matches S1b/S1c.
- Exported pixel rate: Σ(w × h × rate) ≤ **300 Mpix/s** (setting, self-calibrated; ceiling ≈ 350–370 measured), the focused window counted first.
- Request→ready p50: ≤ 2.5 canvas output frames (42 ms at 60 Hz, 21 ms at 120 Hz; measured 25 / 15 ms in S1b, 33.1–33.3 ms at 60 Hz in the M5 runs); the governor shrinks the budget when this is exceeded for two seconds in a row.
- Scene GPU (both eyes + spectator): ≤ 6 ms with ≤ 30 visible quads; overlays ≤ 0.5 ms; worst render p99 ≤ 10 ms with staggered phases (S1b: 7–9 ms).
- Capture import + downsample: ≤ 4 ms per tick (ready→imported 0.5–2 ms p50 measured on a quiet GPU).
- Compositor: Hyprland CPU ≤ 15 % of a core (7–13 % measured); eDP p99 growth ≤ 1 ms (unmeasured, optional).
- CPU per tick: list diff ≤ 0.2 ms for 200 windows; ray tests ≤ 0.2 ms on candidates; governor ≤ 0.1 ms; search ≤ 1 ms per keystroke.
- Memory: canvas VRAM ≤ 512 MB; host RAM unchanged.
- Latency: explicit focus → stage + warp ≤ 50 ms (33 ms Lua timer + 1 frame; Lua part 6.5 ms measured); focused capture ready → display ≤ 1 frame; promotion park → stage fresh frame < 0.5 s, full rate within 1 s (S2).
- Clients: total client CPU ≤ 15 % of a core for 50 mostly idle windows (2–6 % measured); parked idle windows draw at ≈ 20 Hz, not 0.

---

## Appendix A — Review resolutions

Items marked † were later superseded by the M0 measurements (parked windows are throttled, not suspended; the sliver strip is an exception, not a tier; the budget is exported pixels, not bytes). The body of the plan above is authoritative.

**Feasibility review**
1. Workspace switch/move freezes captures — **accepted**: Lua guards + workspace rules (§3.3), S9.
2. Hyprland fullscreen blanks pile — **accepted**: D7 resolved to Fill only; `suppress_event`; S10.
3. † Cold tier via hidden workspace = suspended/frozen, `render_unfocused` does not help — **accepted then, overturned by S2**: parked clients keep ≈ 20 Hz of frame callbacks and `ignore_damage=1` captures them, so park became the uniform tier.
4. Cursor into the pile with `follow_mouse=1` — **accepted**: `no_follow_mouse` slivers at the far edge (now only for > 30 Hz windows), stage band capped; verified by the S3 sweep.
5. Default chords taken — **accepted**: takeovers + `SUPER+CTRL+G`; D5 reframed.
6. eDP impact and pile client cost unmeasured — **accepted**: S1 measured Hyprland/client CPU and the render node; eDP p99 and watts remain optional (§3.6).
7. Restore returns one floating workspace — **accepted**: per-window `canvas-session.json`, dispatch-based migration.
8. Text-input toplevel fights focus — **accepted by replacement**: Quickshell layer-shell prompt with exclusive keyboard focus (S7); the 1×1 toplevel is dropped.
9. Lua names — **accepted**: corrected throughout; `hyprctl` fallbacks removed.
10. `.windows` I/O per title event — **accepted**: dirty flag, ≤ 10 Hz.
11. S6 low value / ext as fallback — **accepted**: informational only; ext removed from fallbacks.
12. Screencast event flood — **accepted**: sessions kept alive, tier-change rate limit, indicator check deferred to M5 acceptance ("recording indicator steady"). **Measured M5**: 0 events in steady state; view changes post about one per window entering or leaving the view, which omarchy-shell's indicator does not read (see the M5 notes).
13. Vendor XML into packaging in M0 — **accepted**.

**Regressions review**
- More `geometry` consumers — **accepted**: full table in §4.2 and one `View::navigate` switch.
- Ungated input paths — **accepted**: source→action table, `canvas_focus.cpp` invariants.
- Settings slot = mode number — **accepted by design change**: canvas keys are a fixed key set outside `controls-settings.tsv` (file stays 6 lines); explicit action→mode map; hex tokens; target rule relaxed for ≥ 8.
- S8 tests the wrong call — **accepted**: S8 rewritten around `terminate_viewer()`; primary path is single-process `mode:` switch (M6); toggle disabled while viewing in M3.
- `place_monitors` damages monitor mode — **accepted**: `CanvasSession` own journaled path, no `persist_applied`; window-level migration; `relocate_workspaces(exclude)`; tests.
- Pointer reaches parked windows / XR cursor in v1 — **accepted**: §3.4, M3.
- Workspace keys hide the canvas; adopt-all vs capture — **accepted**: guards; membership = canvas/park workspace; D9 settled.
- Static rules do not float later windows — **accepted**: Lua enforcement on events + backend reinstall in `reconcile_outputs`.
- Cards fly behind the ring — **accepted**: inside-ring routing, depth test off, view-band berths, tests.
- Fill via zoom > 1 overlaps — **accepted**: zoom ≤ 1, eye dolly for Work/Fill, `panZ ∈ [0, R−0.3]`.
- Milestone order / shared `gbm_device` — **accepted**: minimal governor and device injection in M2.
- Hit pixels projected / selection validation — **accepted**: `.hover` v4 scaling, validate against the unculled list.
- Cylinder math off by gap — **accepted**: `span = 2πR − gap`, test.
- M1 gate missing — **accepted**: `check-preview` + baseline, `UNIT_BINS` wiring.
- Mirror/version skew — **accepted**: v4 mirror, v6 gates on both sides.
- Smoke/lease paths — **accepted**: §4.2 rows for placeholders, `sceneBounds`, `ensureLease` regeneration, per-window smoke counts.

**UX/performance review**
1. Geometry vs FOV — **accepted**: all constants FOV-derived (§4.3 table); Fill resizes the real window; never magnify above 1; frustum test.
2. Shortcut collisions — **accepted** (as above); arrange/help in an Overview submap (M4: prompt keys instead, see M4 notes).
3. Dwell steals focus — **accepted**: dwell only halos in Work; the "1.5 s after keypress" variant **rejected** as unnecessary once dwell no longer focuses.
4. Invisible mouse escape / 2D pointer path — **accepted**: virtual cursor, warp-back, XR cursor, SDL click path, return chord.
5. Warm at 5–10 Hz stutters video — **accepted**: near windows get 24–40 Hz by angular size within the pixel budget (§4.4).
6. † Pile renders at full rate; use `render_unfocused` at 5 fps — **accepted in intent**: non-staged windows go to the park workspace; S2 showed they are throttled to ≈ 20 Hz there (not suspended as assumed) and draw at the pull rate when captured, which is the throttle we wanted; output at 1440p; power still to measure.
7. Capture budget unmeasured — **accepted**: S1/S1b/S1c measured it; the cap is 300 Mpix/s exported, with a 120 Hz row.
8. Head-locked palette — **accepted**: body-locked lazy-follow at 0.9·R; screen space in 2D.
9. Dialogs/child windows; stage screencopy timing — **accepted**: parent rule; `RegionSource` in M3.
10. Dropped phantomat features — **accepted**: nudge, summon, mouse drag in Overview (deferred to M6), pin, radar strip, hold-to-show Alt-Tab, F1 help; head-drag **deferred** (no hold-able gesture available).
11. Overview text — **accepted**: labels always in Overview at fixed angular size; typing starts search.
12. Notifications out of view — **accepted**: row-independent view band.
13. Zoom anchor wobble — **accepted**: latched, eased anchor; cursor point in 2D.
14. Output scale — **accepted**: D3 includes scale, text-size setting.
