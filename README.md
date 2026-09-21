# Omarchy XR

A spatial desktop experiment for Omarchy / Hyprland and VITURE XR glasses.

**Working now:** any number of independently captured panels, a native Omarchy
Monitor Studio panel, per-monitor resolution, drag-and-drop 2D arrangement,
saved layouts, row/grid presets, and a live 3D viewer with pan/zoom/fit controls.
There is no fixed monitor-count limit; CPU, memory, GPU and compositor resources
limit practical configurations. VITURE head tracking, stereo eye rendering, and dedicated DRM-leased
presentation are supported. Forwarding clicks/typing through the 3D panels
is still pending.

## Monitor Studio — native Omarchy UI

Studio is a Quickshell/QML **panel plugin hosted by `omarchy-shell`**, using the
real `qs.Ui` buttons and number fields and `qs.Commons` theme tokens. It does not
start a second shell. A Python helper manages outputs outside the UI thread; the
C++ renderer runs separately. Requires the Lua/Quickshell generation of Omarchy.

```sh
make
make install-studio
make install-helper # one-time administrator setup for dedicated stereo
make install-controls # optional live touchpad and keyboard controls
omarchy-shell shell rescanPlugins
omarchy plugin enable afruth.omarchy-xr
omarchy bar put afruth.omarchy-xr
make studio
```

You can also launch **XR Monitor Studio** from the application launcher after
installation, or click its icon on the Omarchy top bar. `make install-studio`
copies only this project's plugin files and binary into your user configuration
and registers that bar icon; it never edits `/usr/share/omarchy`. Move the icon
with `omarchy bar move afruth.omarchy-xr --section right`.

Studio has three tabs (also available with **Ctrl+1 / Ctrl+2 / Ctrl+3**):
- **Controls**: start/close stereo, recenter, fit, and zoom, with live connection status.
- **Monitors**: arrange panels, edit resolution, curvature and spacing, then save/apply
  using the persistent action bar. Switching tabs preserves unapplied edits.
- **Utilities & Debug**: connection checks, SDK controls, USB-C recovery, previews,
  monitor cleanup, and selectable session activity (up to 40 recent events).

1. In **Monitors**, choose a monitor count, or use **+ Add / Remove selected**.
2. Select a rectangle and edit its width/height or X/Y position. Drag to reposition
   in 20-pixel increments. Scroll to zoom; drag empty space to pan. Row/Grid and
   Fit help arrange large layouts. Resolutions range from 320×200 to 8192×8192,
   subject to actual compositor/GPU support. X/Y are in desktop pixels.
3. **Apply layout** creates/resizes/removes app-owned virtual outputs. Overlapping
   layouts are moved apart on Apply to preserve the selected gutter. All virtual outputs use scale 1 and refresh at max(60, capture fps) Hz; capture fps is
   independently configurable from 1–120. Dedicated stereo applies changes live.
4. **Open terminal here** launches a terminal on the selected monitor. Run apps
   from those terminals, or place windows using your usual Hyprland controls.
5. **Start stereo** on **Controls** applies the draft, connects the SDK, switches the
   glasses to SBS, and takes a temporary DRM lease through the installed helper. The renderer has no desktop window and
   the headset disappears from the normal desktop layout until you stop it.
   **Fullscreen mono** retains the regular-window fallback.
   **Windowed preview** opens the applied layout in a regular window.
   **Close viewer** stops presentation while keeping virtual desktops running.
6. **Stop & remove monitors** stops the viewer and removes the virtual outputs.
   Existing applications are left running and Hyprland relocates their workspaces.

Hide, Escape, or closing the Studio window parks the editor as an icon on the
Omarchy top bar and leaves virtual monitors and any running viewer up. Hide is
not Stop. Click the top-bar icon, launch **XR Monitor Studio**, or run
`omarchy-shell shell summon afruth.omarchy-xr '{}'` to reopen the editor; Stop
still lives there. The helper cleans up on orderly shutdown; a journal permits stale
outputs to be removed on its next start after a crash. It only manages its uniquely
named `OMXR-…` outputs. It will not remove unrelated monitors.

Layout coordinates map to the panel plane and to the relative arrangement of
Hyprland outputs. The output group is offset to the right of existing displays so
it does not overlap your physical desktop. Studio stays on your physical desktop; dedicated XR reserves the VITURE output.
Only app-owned virtual monitors are captured, avoiding capture of the viewer itself.

Save writes a layout without applying it. Apply also saves. State, the renderer
layout and viewer log live under `$XDG_STATE_HOME/omarchy-xr` (default
`~/.local/state/omarchy-xr`). Runtime monitor rules are not written to Hyprland's
configuration. If applying a layout fails, newly created outputs are removed;
existing app-owned outputs may already have been resized. Correct the layout and
apply again, or use Stop to clean up.

For development, reinstall after changes. Some Quickshell versions cache plugin
files despite rescan; if old code remains loaded, use `omarchy restart shell`
when the session is unlocked, then reopen Studio. This briefly restarts the bar
and panels but leaves applications running.

## Spacing / gutter

**Spacing (pixels)** is a workspace setting, defaults to **24**, and accepts only
whole numbers from **1 to 8192**. Zero and negative values are rejected by both
the UI and backend. Row/Grid and adding monitors include the gutter. Applying or
saving a manually edited draft moves conflicting rectangles right or down by the
smallest available correction, preserving screen sizes and list order. The editor
shows the resulting positions. Existing larger gaps are retained.

Old saved layouts without spacing migrate to 24 pixels when loaded. The migration
is saved on the next Save/Apply. Hyprland receives separated flat output rectangles;
curvature exists only in the viewer, not in the compositor's monitor geometry.

In 3D, spacing is a minimum world-space separation expressed in layout pixels
(900 pixels per scene unit), not a constant number of pixels on the physical
screen. The renderer computes analytic bounds of each curved surface, including
its border/depth layers. If those bounds are too close, it increases viewing
distance until the gutter is safe; zoom-in is subject to the same check. Bounds
are conservative, so the actual visible gap may be larger. Curvature percentages
remain unchanged; their distance-based radii increase as the camera backs off.
A layout that cannot satisfy the guard within the supported viewing distance is
rejected with an explanatory error rather than rendered with intersecting panels.

Direct renderer usage accepts `--spacing 24`; use layouts with at least that
flat gutter. The borders are drawn inside monitor dimensions so they cannot fill
small gaps. Capture resolutions do not shrink to create spacing.

## Curvature

Studio provides two independent 0–100% controls, both defaulting to zero:

- **Workspace curvature** wraps panel centers horizontally around the neutral
  viewing origin and rotates their tangent planes along the arc. Monitors keep
  flat surfaces when their individual curvature is zero.
- **Surface curvature** in the selected monitor's inspector bends that monitor's
  actual mesh into a concave horizontal cylinder. Other monitors are unaffected.

The 2D editor remains an unwrapped layout map. These are presentation settings;
resolutions and the compositor's flat desktop coordinates do not change. Apply
curvature changes to update the presentation. If a viewer is running, it restarts
with the new shape. Save alone only persists the draft.

The neutral camera sits at the origin, with the workspace centered ahead of it.
Workspace radius at 100% is the viewing distance, increased if necessary to keep
its total sweep within 300°. Lower percentages increase the radius continuously;
0 is exactly flat. Each monitor's surface radius uses its center's distance to
that same origin; its sweep is capped at 160° to prevent folding. Bending preserves
horizontal arc length and the panel's center/tangent. Height remains unchanged.

Head/mouse rotation changes the view, not the workspace anchor. Middle-drag pans
the viewing camera relative to the neutral anchor. Wheel zoom changes the viewing
distance. F fits/recenters the workspace; R centers the selected monitor
while preserving viewing distance. Curvature-aware separation also limits how close you can zoom; panel surfaces
and borders stay apart.

For a synthetic preview:

```sh
./build/omarchy-xr --workspace-curvature 80 --surface-curvature 50
```

`--surface-curvature` applies uniformly to direct `--capture` arguments or the
synthetic preview. TSV layouts carry independent curvature as an optional sixth
column (`NAME X Y WIDTH HEIGHT CURVATURE`); existing five-column files stay flat.
`--workspace-curvature` applies to the whole scene with either input format.
Saved JSON profiles without curvature fields also load as zero.

## Build and development

Omarchy/Arch dependencies: `gcc make pkgconf sdl2-compat libglvnd wayland mesa libdrm`.
Studio additionally uses the already installed `quickshell`, `python`, `hyprctl`,
Omarchy shell UI components. Panorama import uses `imagemagick`.

```sh
omarchy pkg add gcc make pkgconf sdl2-compat libglvnd wayland mesa libdrm imagemagick
make                 # optimized build with debug symbols and warnings
make run             # synthetic preview without creating monitors
make check           # pixel conversion, lifecycle and CLI tests
make smoke           # ten rendered frames; requires a graphical session
python3 tests/live_studio.py  # opt-in Hyprland integration test with temporary outputs
python3 tests/live_tracking.py # opt-in hardware test; close other SDK sessions first
python3 tests/live_dedicated.py # opt-in stereo/DRM handoff/restoration test
```

Ubuntu renderer dependencies: `g++ make pkg-config libsdl2-dev libgl1-mesa-dev
libwayland-dev libwayland-bin libegl1-mesa-dev libgbm-dev libdrm-dev python3`. The Studio UI requires Omarchy itself.
VS Code build/run/check tasks are included. Build artifacts stay in `build/`.

## Viewer controls and direct capture

- Right-drag: look around; middle-drag: pan across the panel plane.
- Mouse wheel: zoom; **F**: fit every panel; **R**: recenter; **Esc**: exit.
- Clicking panels does not yet control their applications.

```sh
./build/omarchy-xr --list-outputs
./build/omarchy-xr --capture XR-1 --capture XR-2 --capture XR-3 --fps 20
./build/omarchy-xr --layout /path/to/layout.tsv --fps 20
```

Repeated `--capture` arguments arrange outputs in a row. A TSV layout specifies
one output per line as `NAME X Y WIDTH HEIGHT` (whitespace separated). Output
names must be unique. A missing output at startup is an error. If a source fails
while viewing, that panel turns red and other captures continue; automatic
reconnection is not yet implemented.

Add `--smoke-test` to require ten frames from **each** output within fifteen
seconds and check OpenGL errors. The integration test exercises landscape and
portrait outputs, different resolutions, resizing, removal, and cleanup.

## Performance and limitations

Capture imports reusable DMA-BUF buffers on supported EGL/GBM systems and downsizes
textures on the GPU to match projected monitor size, with sampling headroom and
hysteresis. Monitors outside a 10% expanded view stop requesting captures and retain
their last texture. Damage-aware requests avoid copying unchanged desktops.
The screencopy protocol still requires a native-size source buffer; logical desktop
resolution is unchanged. This removes CPU readback/upload on the GPU path, rather
than eliminating all GPU copies. A reusable shared-memory fallback converts only
the reduced target resolution (`OMARCHY_XR_SHM_CAPTURE=1` forces it for diagnostics).
Utilities & Debug reports transport, texture sizes, presentation rate and frame
cost. CPU work excludes presentation waits and is not a GPU execution measurement.
Capture supports up to 120 fps, but actual throughput depends on source refresh,
GPU load and the physical display mode; Pro 2 stereo is verified at 60 Hz.

Gaze highlights a monitor and selects its current workspace once per target change.
The compositor adapter polls gaze selections at 2 ms. The actual desktop cursor
is included in capture; XR draws no separate pointer. Workspace selection uses normal Omarchy focus/cursor behavior.
Desktop redraw effects still depend on capture latency.

The renderer uses OpenGL compatibility functionality. Dedicated XR draws separate
left/right eye views. Its projection is not an individual optical calibration;
default vertical FOV is 28° and IPD is 64 mm (`--fov` / `--ipd` overrides). Glasses require working USB-C video;
USB device detection alone is insufficient. The preview works on a normal screen.
Pro 2 tracking is rotational only (3DoF).

## SDK and licensing

Obtain the current Linux SDK from [VITURE](https://www.viture.com/developer).
No vendor SDK binaries are bundled; keep local SDK files in ignored `vendor/`
and review redistribution terms before packaging them.

Original code is MIT. The vendored [wlr-screencopy protocol](https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-screencopy-unstable-v1.xml)
retains its MIT notice; other dependencies retain their licenses. This project
is not affiliated with VITURE or Omarchy.

See [architecture and milestones](docs/architecture.md) and the
[Omarchy plugin development guide](https://plugins.omarchy.org/develop.html).

### Reconnecting glasses

Monitor Studio checks the glasses connection every three seconds while open.
The connection row reports USB detection separately from an active display
identified as VITURE by Hyprland. **Check connection** refreshes this status
without changing your displays and shows a timestamped result in a fixed banner.
Action results and errors remain visible while you scroll. An unidentified display is not assumed to be
the glasses.

If reconnecting the cable does not restore video, choose **Reinitialize USB-C…**
and confirm in the centered dialog. This requests administrator authorization through polkit and
unbinds/rebinds the supported UCSI ACPI controller. Other devices using USB-C
may briefly disconnect. Recovery is available only when exactly one supported
controller is found and `pkexec` is installed. It runs asynchronously and
does not delete the virtual monitor layout. Cancelling authorization leaves
the controller untouched.

A successful controller reset does not guarantee video: firmware, cables,
and DisplayPort negotiation can still prevent a connection. The UI keeps
checking actual USB and video status; try reconnecting the cable or a full
shutdown if video stays absent. This recovers the OS connection, not a VITURE
SDK session. Use the SDK connection controls below for that.


### SDK connection (Pro 2 / Gen1 / Gen2)

Obtain the current **VITURE XR Glasses SDK**, **Linux (x86_64)**, from
https://www.viture.com/developer. The download form emails a link after
submission. Extract the archive and install its library directory locally:

```sh
python3 scripts/install-sdk.py /path/to/linux/library-directory
```

That directory must contain `libglasses.so`; sibling shared libraries are copied
alongside it to `~/.local/share/omarchy-xr/sdk/`. Vendor binaries remain outside
the repository. For development, `VITURE_SDK_LIBRARY` can select another absolute
library path (set it in the backend's environment). The adapter follows the
[current C API](https://www.viture.com/en-SG/developer/glasses-sdk/glasses);
older `libviture_one_sdk.so` packages are not compatible.

Linux USB access may need the official udev rules in
`/etc/udev/rules.d/70-viture.rules`:

```udev
SUBSYSTEM=="usb", ACTION=="add", ATTRS{idVendor}=="35ca", MODE="0660", TAG+="uaccess"
SUBSYSTEM=="hidraw", KERNEL=="hidraw[0-9]*", ATTRS{idVendor}=="35ca", MODE="0660", TAG+="uaccess"
```

After installing those rules, reload them with
`sudo udevadm control --reload-rules` in a terminal, then reconnect the cable.
The SDK itself runs as your normal user.

**Connect glasses / Reconnect glasses** opens a fresh SDK session, queries brightness to verify communication, reads the display mode when supported,
and requests 120 Hz pose samples. A rejected display-mode query does not disable tracking.
Tracking status requires a valid sample within two seconds; it does not merely
report that streaming was requested. **Retry display mode** reapplies the mode
reported by the glasses and checks the readback. This may interrupt the glasses'
video briefly and does not guarantee restoration of DisplayPort negotiation.
**Disconnect SDK** closes the SDK session without resetting USB-C or removing
virtual monitors.

The SDK runs in a separate process with timeout/crash handling. Unplugging marks
it disconnected on the next status poll; reconnect with the button after plugging
it back in. Recovery never automatically escalates to a USB-C controller reset.
The panel shows USB, SDK communication, recent tracking samples, and video
separately. Logs are in `~/.local/state/omarchy-xr/sdk.log`.
The renderer receives versioned Euler samples through a local nonblocking Unix socket.
The SDK callback only snapshots samples; its worker sends the latest pose up to
120 times per second. The camera follows the bundled SDK Gen1/Gen2 demo: yaw is left-positive,
pitch is down-positive, and roll tilts about forward. **R** recenters heading only
and resets mouse look/pan; pitch and roll remain gravity-referenced; **F** additionally fits all panels. Stale samples older
than 250 ms are discarded and the last view is held; mouse controls remain usable.
An SDK reconnect resumes streaming without needing to reopen the viewer. This is
rotational tracking only; physical translation is not tracked.
Validated with the Linux x86_64 SDK and attached Pro 2: SDK communication and
pose samples work as a normal user with the udev rules installed. On this device,
the firmware rejects the display-mode query (-7), so display-mode recovery is
unavailable; the earlier missing-video issue was separate from tracking and recovered before
fullscreen presentation development.

### Head-tracked presentation

**Start dedicated stereo** is the XR path. It switches the Pro 2 to standard SBS
3840×1080 at 60 Hz, preserving its prior mode for restoration. The SDK acknowledges
the request before the computer drives the new timing, so verification happens
after scanout starts. Left and right views use parallel cameras separated by IPD;
the eye offset is applied in head coordinates. The native desktop cursor is included in capture.

Use Studio's **Recenter**, **Fit view**, **Zoom in/out**, and **Close viewer** while
wearing the glasses. Direct output has no desktop window, keyboard focus, or
mouse surface. Recenter resets heading and preserves gravity-based pitch/roll,
matching the bundled VITURE SDK Gen1/Gen2 demo. The wearer confirmed all axes.

Hyprland only leases non-desktop outputs. The Pro 2 normally identifies as a
desktop monitor, so a polkit-authorized helper temporarily appends a DisplayID AR
use-case block to its EDID through the kernel debugfs override. Existing timings
and identity are preserved. The helper accepts only a connected VITURE DisplayPort
output and refuses existing overrides. It holds a parent pipe and restores normal
detection on EOF/stop; it writes no firmware files or boot settings. The renderer
uses the compositor-granted DRM fd as the normal user (EGL/GBM/KMS).

Closing XR releases scanout, requests the original 2D mode family, restores
EDID/detection, reapplies the saved host resolution/rate/position/scale, and then
verifies the glasses mode. A mode journal supports recovery at the next SDK connection after an
interrupted session. Studio polls even when hidden to restore a stopped/crashed
viewer. USB-C hardware resets are not part of this flow. A polkit prompt is
required when reserving a normally desktop-classified headset.

**Fullscreen mono** and **Windowed preview** remain fallback/test paths. They
use SDL and hide the host cursor; **R**, **F**, wheel and **Esc** work there. Direct
use supports `--direct DP-1 --stereo`, `--list-leases`, and `--pose-socket PATH`.
The SDK publisher sends `euler-nwu-v1 TIMESTAMP ROLL PITCH YAW` packets in degrees
over a private local socket. Publishing runs independently of blocking USB
control commands. Samples older than 250 ms are rejected; stale tracking holds
the previous view. The same socket accepts recenter/fit/zoom controls from Studio.

Verified on the attached Pro 2: direct mono at 120 Hz, stereo at 60 Hz, three live
1080p captures, live tracking, no renderer window or desktop VITURE output while
leased, and restoration after close. Stereo smoke tests compare eye images;
tracking tests cover six directions, 60 combined poses, heading wrap and recentering.
Stereo comfort and optical alignment still require wearer feedback. Panel input
forwarding is implemented; further performance work requires workload-specific profiling.

### Current hardware validation status

The wearer confirmed the corrected tracking axes. Direct mono and stereo rendering
worked on this Pro 2, including removal from the desktop monitor layout. Repeated
mode-switch testing then left this laptop's DisplayPort link disconnected; USB
remained available, and cable reconnect plus UCSI recovery did not restore video.
The override was cleared. After a cold boot, the video connection returned at
1920×1080/120 Hz. Two dedicated-stereo start/close cycles then restored desktop
video successfully with the serialized EDID-family, refresh-rate, and host-mode
transitions. The first exposed an interrupted page-flip wait on shutdown; the
renderer now retries EINTR within the original timeout. The strengthened hardware
test verifies clean renderer exit, live capture/tracking, and desktop restoration.
The final run captured 52 frames per monitor and received 405 pose samples.
This validates those cycles; suspend/resume reliability remains unresolved.


### Live settings updates

While dedicated stereo is running, **Apply to XR** updates monitor count, size,
position, workspace/surface curvature, spacing, and capture rate in the existing
session. The renderer checks the atomically replaced layout every 100 ms and
retains captures/textures for unchanged outputs. Head orientation, recentering,
and the display lease survive edits. Only changed virtual outputs are configured;
presentation-only changes do not reconfigure Hyprland monitors.

`make install-helper` installs a root-owned executable at
`/usr/local/libexec/omarchy-xr-display` and its polkit action. Setup (and explicit
helper upgrades) needs administrator authentication once; starting/stopping stereo
and applying monitor settings then require no password in an active local desktop
session. Remote and inactive sessions are denied. The policy authorizes only this
fixed helper, never Python generally or files in the user plugin directory.
No username, home directory, GPU number, or connector number is baked into setup.
The helper discovers the connected VITURE connector and validates its EDID at runtime.
It requires polkit, Python 3, kernel debugfs EDID override support, and Hyprland DRM
leasing; hardware/driver support still determines whether dedicated stereo works.
The separately supplied VITURE SDK must match the machine's architecture.

After updating helper source, rerun `make install-helper` with XR closed. Ordinary
`make install-studio` updates do not change privileged code. To remove system
integration, close XR and run `make uninstall-helper`; windowed preview remains
available. USB-C controller reset remains a separate administrator-only action.

The current SDL preview backend on this machine crashes during live output
resizes. Windowed/fullscreen preview therefore rejects resolution changes while
open; close preview first, or use dedicated stereo for live resizing.

Hardware regression: `python3 tests/live_layout.py` exercises curvature, spacing,
capture rate, addition, removal and resize, asserting identical renderer, SDK,
and display-helper processes throughout.

### Touchpad and keyboard camera controls

Install with `make install-controls` on Lua-based Omarchy. While the viewer is
running (including dedicated stereo with Studio hidden):

- Three-finger swipe **up** zooms in; **down** zooms out continuously.
- **Ctrl+Up** fits the complete workspace, accounting for curved panel bounds.
- **Ctrl+Down** fits the height of the monitor you are looking at, with a 4%
  margin. Selection follows headset direction, not eye movements. Looking into a
  gap or losing tracking leaves the view unchanged.

Zoom uses exponential, frame-rate-independent easing, with no momentum after
release beyond the short smoothing tail. Existing spacing safety limits still
apply and can limit zoom. Target-height fit can crop the sides of a wide monitor.
The Controls tab also provides a **Fit looked-at monitor** button.

The Lua integration uses live gesture callbacks and an atomic cumulative motion
mailbox, without spawning a process per gesture event. Only vertical three-finger
gestures are reserved. The bindings deactivate within 250 ms of a clean viewer
exit (within about 3 seconds after a crash); normal application Ctrl+arrow handling
then resumes. Controls require the standard Studio state directory, honor
`XDG_STATE_HOME`, and use no administrator privileges. The installer refuses
conflicting compositor Ctrl+arrow bindings. To remove integration, remove the
`require("hypr.xr-controls")` line from `~/.config/hypr/bindings.lua`, remove
`~/.config/hypr/xr-controls.lua`, then reload Hyprland.


### Gaze selection and independent pointer

The brighter halo and workspace selection use the same looked-at monitor. When
that target changes, XR selects the monitor's existing workspace once, without a
dwell delay. Gaps, looking away, and stale tracking preserve the last selected monitor. Looking around within one
monitor never repeats selection or steers the mouse. Mouse motion never changes
the gaze target. Workspace selection uses normal Omarchy behavior, including any
configured one-time cursor warp. Ctrl+Down centers the selected monitor face-on and fits its height.
Three-finger zoom approaches the looked-at point on that monitor along its local
normal without changing the workspace bend. With no look hit, zoom uses the
selected monitor center, then the workspace. Ctrl+Up returns to the full workspace view.

There is no XR pointer reticle. The native desktop cursor is captured with the
desktop, so its visible update rate depends on capture delivery. Requires `make install-controls` and
dedicated stereo. Head direction supplies gaze; this is not eye tracking.

Capture defaults to 60 fps. Direct presentation services capture protocol events
during the display-flip wait and immediately flushes negotiated copy requests,
avoiding extra full-frame waits. This improves delivery cadence but does not
guarantee a 60 fps cursor under every compositor workload.

Three-finger double tap smoothly centers the selected monitor without changing zoom (about half a second). Camera zoom
is independent of curvature spacing, and zoom-out stops at twice the fitted
workspace distance. A quick upward flick fits the
selected monitor; a downward flick fits the whole workspace; slow or sustained vertical motion zooms continuously. Flicks
are recognized on release within 220 ms; slow motion starts zooming earlier, and
held motion switches to zoom after that window. Cancelled flicks do not fit.

The installer discovers local touchpads for a device-specific tap binding. With
standard libinput LRM tap mapping, three-finger tap is middle click: that touchpad
button recenters on a double tap within 400 ms only while XR is active (including a physical middle click on
the same touchpad). External mice retain their middle button. Tap-to-click must
be enabled; this machine already has it enabled. Rerun `make install-controls`
after adding a new touchpad. The binding follows the global LRM/LMR tap mapping.

The main tab's **Input controls** card configures fit workspace, fit selected
monitor, recenter, zoom-in and zoom-out hotkeys. Leave a field blank to disable
that shortcut; use modifier combinations such as `CTRL + ALT + R`. Swipe finger
count can be 3 or 5 (hardware support required); four fingers pan. Tap remains three fingers.
Apply controls saves preferences and updates the running Lua bindings without
restarting XR or reloading Hyprland. Duplicate and conflicting desktop shortcuts
are rejected. Reset to defaults changes the draft; Apply confirms it.

Focused zoom uses heading-only navigation: head roll/pitch are never saved as
workspace tilt. Live head tracking still preserves its gravity reference.

### Mono window for recording

Enable **Utilities & Debug → Mono window for OBS** to open a separate computer window alongside stereo on the glasses. The preference is saved; it can also be toggled while stereo runs. Select **Omarchy XR — Mono spectator** in your recording application's window picker. Closing that window leaves stereo running; Studio can reopen it.

The window renders the same head-controlled camera from its center, without stereo separation. It shares captured desktop textures, uses GPU DMA-BUF buffers with no CPU readback, and renders at up to 30 fps. Resizing preserves the glasses' aspect ratio with letterboxing. Hidden or busy windows skip rendering instead of waiting on the compositor. The extra scene rendering still adds GPU work. CLI: `--direct OUTPUT --stereo --spectator`.

### Saved monitor setups

The **Saved setups** picker appears in Monitors. Enter a name and choose **Save new** to keep the editor's complete layout: monitor identities, dimensions, positions and surface curvature, workspace curvature, spacing, and capture rate. **Update selected** replaces that named setup (and can rename it). Names are unique ignoring case.

Click a saved setup to switch. Running virtual monitors update through the normal live apply path; dedicated stereo keeps its renderer and lease. Before startup, selecting a setup just loads it for the next session. Unapplied edits prompt before replacement. Layouts are stored atomically in `~/.local/state/omarchy-xr/setups.json` (or under `XDG_STATE_HOME`). Hotkeys, mono-window preference, running applications, and transient head/camera position remain session/global settings.

Studio's background status checks do not disable editing or interrupt mouse grabs. Monitor dragging snaps to nearby sibling edges and the configured gutter, falling back to a 20-pixel grid. A thicker selection border indicates snapping; moves that would overlap another monitor or reduce its gutter are blocked. `make check-ui` exercises numeric editing, dragging during status polling, request correlation, and monitor snapping with Qt Quick Test.

**Workspace wrap (°)** accepts 0–360 degrees and stays independent of camera zoom. Enable **Monitors follow workspace curvature** to project all monitor surfaces onto the same vertical cylinder (horizontal curvature, straight vertical edges). Disable it to restore each monitor's independent **Surface bend (°)** setting. The checkbox and angle are saved with layouts and setups. Older percentage-based layouts retain their existing geometry until the workspace angle is edited. Surface bend still uses the monitor's geometry radius for degree conversion. Positive monitor spacing remains enforced in both modes.

### Computer graphics limits

`omarchy-xr --graphics-limits` queries OpenGL texture, renderbuffer, and viewport limits on each accessible DRM render node using a temporary GBM/EGL context. It also reads DRM card dimension ceilings without leasing a display or allocating large framebuffers. Studio uses the smallest detected ceiling and its existing 8192-pixel application cap. Validation runs before any monitor mutation, including when applying a saved setup from another computer. The Monitors tab shows the effective limit and estimated bytes for one full-resolution RGBA frame.

These are dimension guards, not a guarantee of successful buffer allocation or smooth performance. Compositor allocations, multiple buffers, applications, GPU/system memory pressure, and capture rate still matter. If detection fails, Studio labels the application cap as unverified; partial GPU results are also identified. Detection is cached for the Studio backend's lifetime; reopen/restart Studio after changing GPUs or drivers.

## Environment backgrounds

The Environment tab selects a local 360° panorama or a black background.
Brightness and rotation apply live to stereo, desktop preview, and the mono OBS
window, independently of monitor layout. The sky follows camera rotation, with
no positional parallax or change from monitor zoom. Settings persist across runs.

Import a 2:1 JPEG, PNG or BMP using Choose image. ImageMagick prepares a maximum
4096 × 2048 texture by default; optional 8K uses more GPU memory. Smaller images
are not upscaled. HDR/EXR and cubemap imports are not currently supported.
The original file stays unchanged. Decode runs off the renderer thread and uploads
are spread across frames; switching retains the previous sky until loading finishes.
A static panorama adds one cached mesh draw per eye; 120 Hz performance still needs
hardware measurement. Brightness zero disables the sky draw.

Imported textures and thumbnails live in `$XDG_DATA_HOME/omarchy-xr/environments`
(default `~/.local/share/omarchy-xr/environments`). Preferences and the live renderer
configuration live in `$XDG_STATE_HOME/omarchy-xr/environment.{json,tsv}`. Imported
assets are local and are not included in saved monitor arrangements or Git.

Eight generated concept panoramas are installed from `assets/environments`; their
provenance is documented there. Third-party packs must be imported locally unless
their license explicitly permits redistribution.

## Built-in workspace setups

The Monitors tab includes Full HD, 4K, three Full HD, two Full HD,
Full HD with two portrait Full HD sides, curved 3440 × 1440,
curved 5120 × 1440, and 4K with a 1440 × 2160 side monitor.
Presets use 30 px spacing, 60 fps capture, scale 1, and full monitor brightness.
Curved presets bend the monitor surface; workspace curvature starts at zero.
Mixed-height monitors are vertically centered.

Selecting a preset follows the saved-layout switching flow and checks the
computer's resolution limits. Unsaved edits require confirmation before switching.
Built-ins cannot be overwritten; use Save new to keep a customized copy.
Environment, SDK, and input preferences remain independent of workspace presets.

## Laptop display during stereo

Controls → Laptop display can turn off active internal eDP/LVDS/DSI outputs after
stereo starts and the renderer reports presented frames. The preference defaults
to off and persists independently of monitor layouts. It never disables an
external monitor or changes a Hyprland configuration file.

The Restore laptop display button turns it back on for the current session;
unchecking the toggle also disables automatic shutoff for future sessions.
Stopping stereo restores the panel before SDK/display-mode recovery, even if
headset recovery fails. A separate unprivileged watchdog restores it on renderer
exit, manager pipe closure, USB disconnection, or lost glasses video connection.
It records the original mode, position, scale and transform before disabling.
Recovery is retried and a journal is retained on failure; app startup retries
unfinished restoration in the same compositor session.

Before disabling the laptop output, its normal-workspace windows are distributed
across the visible XR workspaces. Special workspaces are left unchanged. External
laptop toggles are detected by the status poll (normally within three seconds);
windows from its previously observed workspaces are redistributed then. Restoring
the laptop does not pull those windows back out of XR.

The same poll restores XR resolutions, scales and positions after compositor
configuration reloads. Capture renegotiates buffers without restarting stereo.
For terminal recovery without the UI (default installation paths):

```sh
python3 ~/.config/omarchy/plugins/afruth.omarchy-xr/studio/laptop_display.py restore ~/.local/state/omarchy-xr
```

This needs no administrator password. The watchdog cannot recover while the
compositor or machine is unresponsive, or if the watchdog itself is forcibly
killed; the journal allows restoration when the app is reopened.

Four-finger swipes pan the selected monitor when it exceeds the viewport. Motion
follows the surface tangent at constant zoom, with a 64-pixel margin beyond its
edges. Axes that already fit remain centered. The gesture keeps its initial
monitor selected until release. Fit and recenter reset the pan offset. Zoom
swipes use three or five fingers; four is reserved for pan.
