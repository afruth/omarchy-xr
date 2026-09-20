# Omarchy XR

A spatial desktop experiment for Omarchy / Hyprland and VITURE XR glasses.

**Working now:** any number of independently captured panels, a native Omarchy
Monitor Studio panel, per-monitor resolution, drag-and-drop 2D arrangement,
saved layouts, row/grid presets, and a live 3D viewer with pan/zoom/fit controls.
There is no fixed monitor-count limit; CPU, memory, GPU and compositor resources
limit practical configurations. VITURE tracking, stereo output, and forwarding
clicks/typing through the 3D panels are still pending.

## Monitor Studio — native Omarchy UI

Studio is a Quickshell/QML **panel plugin hosted by `omarchy-shell`**, using the
real `qs.Ui` buttons and number fields and `qs.Commons` theme tokens. It does not
start a second shell. A Python helper manages outputs outside the UI thread; the
C++ renderer runs separately. Requires the Lua/Quickshell generation of Omarchy.

```sh
make
make install-studio
omarchy-shell shell rescanPlugins
omarchy plugin enable afruth.omarchy-xr
make studio
```

You can also launch **XR Monitor Studio** from the application launcher after
installation. The installer copies only this project's plugin files and binary
into your user configuration; it never edits `/usr/share/omarchy`.

1. Choose a monitor count, or use **+ Add / Remove selected**.
2. Select a rectangle and edit its width/height or X/Y position. Drag to reposition
   in 20-pixel increments. Scroll to zoom; drag empty space to pan. Row/Grid and
   Fit help arrange large layouts. Resolutions range from 320×200 to 8192×8192,
   subject to actual compositor/GPU support. X/Y are in desktop pixels.
3. **Apply layout** creates/resizes/removes app-owned virtual outputs. Overlapping
   layouts are moved apart on Apply to preserve the selected gutter. All virtual outputs use scale 1 and 60 Hz; capture fps is
   independently configurable from 1–60. Layout changes stop an existing viewer.
4. **Open terminal here** launches a terminal on the selected monitor. Run apps
   from those terminals, or place windows using your usual Hyprland controls.
5. **Open live viewer** shows every monitor as a live panel on a common plane.
6. **Stop & remove monitors** stops the viewer and removes the virtual outputs.
   Existing applications are left running and Hyprland relocates their workspaces.

Closing the Studio window hides the editor and leaves monitors running. Reopen it
to stop them. The helper cleans up on orderly shutdown; a journal permits stale
outputs to be removed on its next start after a crash. It only manages its uniquely
named `OMXR-…` outputs. It will not remove unrelated monitors.

Layout coordinates map to the panel plane and to the relative arrangement of
Hyprland outputs. The output group is offset to the right of existing displays so
it does not overlap your physical desktop. Keep Studio and the viewer on your
physical display to avoid capturing the viewer itself.

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
distance and recalculates reference radii. F fits/recenters the workspace; R resets
look and pan. Curvature-aware separation also limits how close you can zoom; panel surfaces
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

Omarchy/Arch dependencies: `gcc make pkgconf sdl2-compat libglvnd wayland`.
Studio additionally uses the already installed `quickshell`, `python`, `hyprctl`,
Omarchy shell UI components, and `foot` for the terminal button.

```sh
omarchy pkg add gcc make pkgconf sdl2-compat libglvnd wayland
make                 # optimized build with debug symbols and warnings
make run             # synthetic preview without creating monitors
make check           # pixel conversion, lifecycle and CLI tests
make smoke           # ten rendered frames; requires a graphical session
python3 tests/live_studio.py  # opt-in Hyprland integration test with temporary outputs
```

Ubuntu renderer dependencies: `g++ make pkg-config libsdl2-dev libgl1-mesa-dev
libwayland-dev libwayland-bin python3`. The Studio UI requires Omarchy itself.
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

Capture currently uses wlr-screencopy shared-memory buffers and RGBA texture
uploads, with one capture connection per output. The Studio workload indicator
estimates bytes per second for just **one** full-frame copy; actual traffic and
memory usage are higher. Lower capture fps/resolution as monitor count grows.
This is not zero-copy; capture conversion runs on the rendering thread and can
limit responsiveness at high loads. No arbitrary monitor count is advertised as
smooth. GPU buffer import, buffer reuse, and smarter scheduling are future work.

The renderer uses OpenGL compatibility functionality. It is monoscopic and its
projection is not an optical calibration. Glasses require working USB-C video;
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
