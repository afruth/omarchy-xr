# Architecture

## Components

- `studio/MonitorStudio.qml`: native Omarchy shell panel, `qs.Ui` controls and
  theme tokens, interactive 2D canvas. Plugin ID: `afruth.omarchy-xr`.
- `studio/BarWidget.qml`: top-bar icon that summons the kept-loaded panel.
- `studio/backend.py`: JSON-lines worker. Owns virtual monitor lifecycle, saved
  layouts, recovery journal, and renderer child process. Uses argument arrays
  for subprocess calls and validates all geometry and identities before Lua calls.
- `src/main.cpp`: independent C++ viewer. One capture source and texture per panel,
  common 2D plane in a 3D scene, camera look/pan/zoom and fit-all.
- `src/capture.cpp`: asynchronous wlr-screencopy, GPU DMA-BUF import with reusable shared-memory fallback, separate
  Wayland connections; nonblocking frame polling and per-source failures.
- `src/pixels.hpp`: stride/format/inversion conversion with deterministic tests.
- `src/layout.hpp`: renderer layout format and validation.

## Lifecycle

Studio edits a draft. Save persists the draft; Apply validates non-overlapping
rectangles, creates or updates owned monitors, verifies actual dimensions,
removes obsolete outputs, then writes the renderer layout. Layout editing leaves physical outputs alone. Dedicated presentation temporarily
changes the VITURE mode and classification, then restores them on exit. An existing viewer stops before apply to release its captures.
Monitor identifiers remain stable during an editor session.

The editor is a kept-loaded `panel` plus a `bar-widget`. Hiding it (Hide, Escape,
or closing the window) dismisses the floating editor and leaves the top-bar icon
so it can be summoned again. Hide does not stop the helper, remove OMXR outputs,
or tear down stereo. Close viewer, viewer exit/crash, and explicit Stop restore the laptop display,
move XR workspaces (without closing their windows) to a remaining display, and
remove owned outputs. Internal viewer handoffs preserve the applied outputs.
Layout shrink moves workspaces to a surviving XR output. If no reachable display
exists or migration fails, outputs remain journaled for a retry; normal helper
termination and crash recovery use the same cleanup path.
A lock prevents concurrent helpers, and an output journal enables crash recovery.
Monitor creation intent is recorded before the create request. A failed Apply
removes newly created outputs but may leave changes to existing outputs; the UI
reports the error and permits a retry or cleanup. This is not a transactional
compositor API.

The original layout position is used in the viewer. For Hyprland, positions are
normalized and shifted to the right of all unowned outputs. A saved built-in
laptop panel keeps that origin while the panel is off. If no unowned display
remains, Apply keeps the current owned virtual-desktop origin instead of
refusing the layout switch. Apply still fails when the compositor has no
existing display at all. All virtual outputs
use scale 1; output refresh is max(60, requested capture fps), with capture capped at 120 fps.
Resolution and count are subject to compositor/GPU/resource limits. No physical
room-scale position is inferred from the glasses.

## Verification

`make check` runs pixel tests and backend tests with an injected Hyprland runner.
`tests/live_studio.py` tests real mixed-resolution output creation, capture,
resize, removal and cleanup under Hyprland. The renderer smoke test requires
frames from every selected source. Native panel loading, theme integration,
Apply/Stop, and five-panel capture are also checked in the running Omarchy shell.
CI can run unit tests and synthetic rendering; it does not provide Omarchy.

## Next milestones

1. VITURE SDK pose source is integrated: isolated lifecycle, local versioned Euler
   stream, SDK-demo camera conventions, yaw-only recentering, and stale-sample handling.
   The wearer verified all axes. Remaining tracking work includes latency and drift
   characterization, and optional prediction.
2. Panel interaction: ray/plane hit testing, pointer coordinates and keyboard
   focus using supported compositor APIs.
3. Efficient capture: reuse buffers, GPU imports where supported, profile latency
   and hybrid-GPU transfers; prioritize visible panels.
4. Presentation: DRM-leased stereo scanout is hardware-tested. Remaining work
   includes individual optical calibration and comfortable panel sizing.

## UI rationale

Omarchy's accepted shell-extension mechanism is a manifest-based QML plugin in
its existing Quickshell process. Shared `qs.Ui` and `qs.Commons` supply native
controls, styling and live theme updates. The Python worker and C++ renderer
stay out of the shell's UI thread. No GTK interface or second shell is launched.

References: [Omarchy plugin guide](https://plugins.omarchy.org/develop.html),
[official shell reference](https://github.com/basecamp/omarchy/blob/quattro/shell/README.md),
[VITURE SDK](https://www.viture.com/en-SG/developer/glasses-sdk/glasses),
[Hyprland output controls](https://wiki.hypr.land/configuring/core/advanced-configuration/using-hyprctl/).

## Curvature geometry

`src/curvature.hpp` contains pure, renderer-independent geometry. For centered
horizontal arc distance x and inverse radius k, panel centers map to
`(sin(k*x)/k, y, -distance + (1-cos(k*x))/k)` and yaw is `-k*x`.
Zero and near-zero cases use stable limits. Workspace bending transforms only
panel poses; individual surface bending tessellates in local coordinates with
its own inverse radius, then applies the pose. The common reference is the
neutral camera origin. Looking rotates the view; it never rotates the anchor.

The shared surface function draws textures, borders and placeholder artwork,
with approximately one degree per segment (8–180 segments for curved patches).
Tests cover flat limits, rigid tangent planes, curvature independence, translated
camera origins, continuity, sweep bounds, legacy layout parsing, validation,
persistence and presentation-only applies that do not mutate compositor outputs.

## Gutter enforcement

A positive integer workspace spacing is enforced in two domains. The backend
normalizes drafts by resolving expanded-rectangle collisions, validates the
result, and publishes the same adjusted layout to Hyprland and the viewer. Legacy
profiles acquire a 24-pixel gutter; output sizes are unchanged.

`src/spacing.hpp` analytically bounds each transformed cylindrical patch using
endpoints and trigonometric extrema, including all draw-depth layers. Pairwise
AABB distance is a conservative lower bound on surface separation. Fit and zoom
increase camera distance until every pair satisfies spacing/900 scene units (with
0.009-pixel floating-point tolerance, well below the 1-pixel minimum). This changes
camera-relative curvature radii while preserving the requested percentages. The
check is repeated when distance changes; panning/looking leave geometry fixed.
Tests sample the analytic bounds densely and exercise mixed widths/heights and
independent curvatures at spacing 1, 24 and 200 pixels, plus profile migration,
validation and normalization idempotence.

## Dedicated presentation

`src/direct_output.cpp` discovers and requests a Wayland DRM lease, creates an
EGL/GBM compatibility context, and page-flips directly on the leased connector.
There is no SDL video window in this mode; the desktop cursor comes from capture. `studio/dedicated.py`
supervises a temporary polkit helper, which marks only the VITURE EDID as an AR
display and restores it on pipe close. `sdk_worker.py` journals the pre-XR mode
and manages the SBS/restore commands independently of its pose publishing thread.
The lease is released before requesting the old device mode family, restoring
the connector, and reapplying the saved host mode. SDK mode readback is verified
after that host timing is restored. Tests cover
failed handoff cleanup, mode journaling, headset identification checks, and a live
lease/stereo/restore cycle. See README for dependencies and the opt-in hardware test.

The display handoff executable is installed separately as root at
`/usr/local/libexec/omarchy-xr-display` (`make install-helper`). Its shebang uses
isolated Python (`-I`); it imports only the standard library. A dedicated polkit
action authorizes that exact executable for active local sessions and denies
inactive/remote sessions. It grants no access to arbitrary Python scripts. The
normal user plugin installer never updates privileged code. The system installer
checks ancestor ownership/permissions, atomically replaces files, and refuses
updates/removal while the display handoff lock is held. No per-user rule or GPU
index is required. `make uninstall-helper` removes both executable and policy.


### Head-directed monitor targeting

`src/targeting.hpp` is the reusable picking layer, independent of keyboard/UI
commands. A normalized world-space ray originates at the cyclopean camera (the
midpoint between stereo eyes). Its inverse view transform includes SDK head
rotation, manual yaw/pitch, and all three camera translation axes. This is head
direction targeting; the glasses do not provide eye tracking.

`query` intersects the full logical panel surface using the renderer's shared
`surfaceSegments` and `spatial::vertex` geometry. It supports workspace and surface
curvature, rejects behind-camera/near-plane hits, and returns the nearest surface,
including self-occlusion. A `Hit` contains stable output identity, world position,
normal, distance, top-left normalized UV and logical monitor pixel coordinates.
Panels have no permanent frame. `interaction::contentUV` maps logical panel UV
through content letterboxing before desktop hover; black bars never emit input.

`Tracker` publishes current hit and enter/move/switch/leave transitions. Selection
is cleared immediately on a miss, removed monitor, or stale SDK pose (>250 ms).
There is no closest-monitor fallback, automatic activation, or dwell delay;
future dwell actions can consume the same tracker without changing hit testing.
A fit action samples the current ray at execution, latches the output identity,
and fits that monitor's projected vertical bounds in the current viewing frame.
It does not recenter head tracking or switch targets while animating. Ctrl+Down
and Studio's Fit looked-at monitor use this same action (`fit_target`); the old
renderer socket command `fit_center` is retained as an alias for compatibility.


### Gaze selection and independent pointer

The ray query supplies one looked-at output for halos, Ctrl+Down, and workspace
selection. The renderer publishes this exact target through `.controls.hover`.
The Lua adapter dispatches `hl.dsp.focus({workspace=monitor.active_workspace.config_name})`
only on target entry/change. No dwell, readability threshold, mouse override timer,
or continuous cursor motion is involved. Named workspaces are supported; outputs
outside OMXR, stale sessions, and duplicate samples cannot trigger selection.
Workspace dispatch retains the user's normal Omarchy cursor-warp policy.

The native compositor cursor is included in capture (`overlay_cursor=1`). There is
no synthetic XR pointer mesh or cursor-coordinate polling mailbox. Capture cadence
therefore matters for cursor motion. Copy requests generated by buffer negotiation
are flushed immediately. The direct DRM flip wait services capture connections at
2 ms intervals, on the render thread with its GL context current. CPU work metrics
include this service time. Request deadlines retain their phase with a 1 ms tolerance
rather than slipping an entire frame on minor vblank timing variation.

For an opt-in hardware cadence test, run `make build/capture-timing` then
`python3 tests/live_capture_timing.py`. It temporarily moves the real cursor on an
existing XR output and restores its position/workspace afterward. It compares
frame-only protocol polling with polling during presentation waits. Results are
workload dependent, not a full end-to-end latency benchmark.

### Persistent selection and gaze-directed zoom

`targeting::Selection` retains the last hit output through gaps and stale tracking,
separately from transient `Tracker::current`. A new hit replaces it; removal from
the layout clears it. Halos, workspace selection, Ctrl+Down, and swipe zoom consume
the retained identity. The head-view anchor is captured on selection, so looking
at the keyboard before zoom does not anchor the monitor at the keyboard.

Focused navigation holds workspace geometry distance fixed and changes camera
translation and rotation. Wheel, swipe, and `zoom_in`/`zoom_out` zoom along the
head-directed hit captured at the start of a zoom gesture via `gazeFocus` and
`panFocus`, so that look point stays on the heading axis. The captured UV is held
across zero-delta frames until pan, fit, or recenter; a later zoom on a different
looked-at monitor recaptures that hit. `panLimits` does not snap
an on-panel origin to the monitor center or pull it into the pan envelope when
pan begins. With no hit, zoom uses the selected monitor center, then workspace
depth. Fit-target still faces the selected monitor's center; `frontFocus` aligns
that center normal with the anchored camera. Intrinsic monitor curvature is
preserved; front-facing height fit includes its edge depth.
Rotation and translation ease together. Live head tracking remains relative to the
anchor. Zoom stays in front of curved edges; Ctrl+Up restores workspace overview.
Tests cover side-monitor normals, projected fit bounds, curved surfaces, retained
selection, and deletion. `python3 tests/live_focus.py` tests looking down followed
by another fit/zoom in an isolated preview.

### Tap, flick, and continuous zoom

Live Hyprland swipe `time_ms` drives classification. A mostly unidirectional swipe
released within 220 ms, with at least 35 units of net travel and average speed
>=0.35 units/ms, sends fit-selected (mode 2) upward or fit-workspace (mode 1) downward. Slow motion (after 70 ms, <0.30
units/ms) commits to zoom; sustained motion commits after 220 ms. Pending deltas
are retained, so no distance is lost when committing. Fast candidates are buffered
to avoid zooming before fit. Cancellation discards uncommitted motion.
Mode 3 recalibrates heading and centers the retained selected monitor face-on,
preserving current perpendicular viewing depth, including during zoom animation.
Camera depth is independent of the geometry radius used for gutter checks. Fit
and zoom use camera translation; zoom-out is capped at twice the overview depth.
Layout-driven geometry-distance changes compensate camera translation. Device-scoped touchpad bindings
map libinput's three-finger tap button to this action and expire with the viewer
heartbeat. The installer generates a local `hypr.xr-touchpads` list from detected
input devices; no universal mouse button binding is installed.

### Configurable input

`studio/input_settings.py` validates modifier chords, duplicate actions and
existing compositor bind conflicts. Preferences live separately from monitor
layout in `controls-settings.json`; a data-only TSV mailbox lets the Lua adapter
replace its own binding handles and vertical gesture registration live. It
unregisters the old finger count and clears an in-progress swipe before changing
settings. The renderer mode mailbox includes recenter (3) and zoom steps (4/5).
The UI keeps a separate controls draft so applying hotkeys never applies pending
monitor geometry edits. Two-finger scroll is not offered as a swipe gesture.

Focus navigation extracts only horizontal heading from its anchor. This prevents
head tilt at selection time from becoming a persistent workspace roll or pitch.
Tests verify identical focus rotation for varied anchor roll/pitch at fixed yaw,
and an unchanged world-up axis throughout the eased navigation transition.

Workspace wrapping stores optional `workspaceDegrees` (0–360, absent/-1 for legacy)
and `workspaceFollow` (boolean). The settings TSV appends these after spacing.
Explicit degrees fix monitor longitudes independently of the safety radius.
Follow mode maps positions and vertices onto a shared vertical cylinder, overriding rather
than deleting each panel's independent surface curvature. Rendering, picking,
visibility and bounds share the cylindrical mesh. Fit and recenter use each
selected panel's horizontal tangent orientation; vertical position and height stay unchanged.

Four-finger pan uses a separate cumulative `.controls.pan` mailbox with renderer
identity, gesture ID, serial, two-axis totals, active flag and timestamp. Lua
refreshes active gestures; the renderer rejects stale or foreign sessions.
Pan limits account for viewport footprint and surface arc length, plus 64 native
pixels of overscroll. Surface coordinates are eased before deriving the tangent
camera, retaining constant depth throughout independently curved and workspace-following pans.

In follow mode, monitor centers and vertices use the same surface arc-length
scale. Explicit wrapping no longer stretches center positions with geometry
distance. Gutter validation uses disjoint surface-coordinate rectangles rather
than overlapping world-axis bounding boxes. A 360-degree wrap reserves one
additional gutter at the closing seam; vertical layouts have no pole limit.

Three-finger recenter requires two touchpad taps 40–400 ms apart. A single tap
never emits a camera command. Zoom/pan gestures clear pending taps and suppress
trailing tap events for 250 ms. Timing uses monotonic `/proc/uptime`, and all
pending state is cleared on binding reconfiguration and viewer expiry.

Recenter rebases navigation against the newly calibrated head view so calibration
does not cause a one-frame jump. Rotation and position then use the existing
time-based camera easing (settling in about 0.5 seconds). Selection is retained
during that transition; new zoom, fit or pan input interrupts it. Repeated
recenter requests start from the current rendered transform.

### Head-pose prediction and its tuning file

In direct mode the renderer samples the pose late, just before the flip deadline, and
extrapolates it to the middle of the next scanout (`src/vblank.hpp`). `tracking::Camera::predict`
takes angular velocity from a least-squares fit over the newest unbroken run of samples, not a
two-point difference, which multiplies sensor jitter by horizon/dt. Prediction fades in with head
speed: a still head is shown exactly as measured, a fast turn gets the whole horizon.

The four values are read from an optional `tracking.tsv` beside the viewer layout
(`~/.local/state/omarchy-xr/`), checked every 250 ms, so they can be tuned while wearing the glasses:

    tracking-v1 <horizonMs 0..30> <restSpeed deg/s> <fullSpeed deg/s> <samples 2..8>

The default is `tracking-v1 20 2 20 5`. A lower horizon, higher speeds or more samples give a
steadier image; the opposite gives less lag. `0` for the horizon disables prediction. Removing the
file restores the defaults, and an invalid line is ignored with a message in `viewer.log`.

Each missed vblank adds 1 ms of latch margin (`MissPenalty`, at most 6 ms, draining at 1 ms per
20 s), so the pose stays on the late latch and the margin converges. The pose is sampled at the
start of the frame when there is no direct output, or when the lease has not reported a vblank
timestamp yet. The scene GPU timer that feeds the margin includes the spectator render, because it
queues ahead of the stereo scene. `pose.sock.stats` reports `predictionMs`, `predictionCapMs`,
`latchMarginMs` and `latchPenaltyMs`.

### GPU load on the integrated card

The compositor's DMA-BUF is linear, and sampling a linear image is slow on the Intel GPU: each
screen row touches a different cache line of the source. Every new capture frame is therefore
blitted once into a driver-tiled private texture, also at 1:1, and the scene samples that copy
(`OMARCHY_XR_DIRECT_SAMPLING` restores direct sampling for A/B measurements). The sky texture
carries mipmaps for the same reason; an 8192x4096 image is heavily minified on a 1920x1080 eye.

The spectator window renders the scene a third time and queues ahead of the stereo scene. It is
timed separately (`gpu spectator p95`), and `SpectatorGovernor` lowers it to 10 fps once a frame's
spectator + scene GPU time passes 60% of the refresh period and pauses it past 85%, recovering one
step at a time after two seconds below 45%. The latch margin uses the sum of both timers.

Halos and drop shadows are four feathered bands per panel drawn through `HaloShader`, a GLSL 1.20
program that fades alpha with the distance from the panel edge; the twelve-ring immediate-mode
version remains as the fallback when the program cannot be built. Panel copies carry one mipmap
level, since the quality buckets always leave them 1.25-1.9x denser than the screen. When one flat
opaque panel covers an entire eye (`occlusion::panelCoversEye`, corner quad in eye space), that eye
skips the full-screen sky draw; curved surfaces never qualify, because their edges bow inward on
screen. The governor decides on the 80th percentile of the last thirty frames, not single spikes.
