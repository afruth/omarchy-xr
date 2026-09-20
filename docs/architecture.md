# Architecture

## Components

- `studio/MonitorStudio.qml`: native Omarchy shell panel, `qs.Ui` controls and
  theme tokens, interactive 2D canvas. Plugin ID: `afruth.omarchy-xr`.
- `studio/backend.py`: JSON-lines worker. Owns virtual monitor lifecycle, saved
  layouts, recovery journal, and renderer child process. Uses argument arrays
  for subprocess calls and validates all geometry and identities before Lua calls.
- `src/main.cpp`: independent C++ viewer. One capture source and texture per panel,
  common 2D plane in a 3D scene, camera look/pan/zoom and fit-all.
- `src/capture.cpp`: asynchronous wlr-screencopy, shared-memory buffers, separate
  Wayland connections; nonblocking frame polling and per-source failures.
- `src/pixels.hpp`: stride/format/inversion conversion with deterministic tests.
- `src/layout.hpp`: renderer layout format and validation.

## Lifecycle

Studio edits a draft. Save persists the draft; Apply validates non-overlapping
rectangles, creates or updates owned monitors, verifies actual dimensions,
removes obsolete outputs, then writes the renderer layout. Physical outputs are
never reconfigured. An existing viewer stops before apply to release its captures.
Monitor identifiers remain stable during an editor session.

The editor is a kept-loaded plugin: hiding it does not stop the workspace.
Explicit Stop removes owned outputs; normal helper termination also cleans up.
A lock prevents concurrent helpers, and an output journal enables crash recovery.
Monitor creation intent is recorded before the create request. A failed Apply
removes newly created outputs but may leave changes to existing outputs; the UI
reports the error and permits a retry or cleanup. This is not a transactional
compositor API.

The original layout position is used in the viewer. For Hyprland, positions are
normalized and shifted to the right of all unowned outputs. All virtual outputs
use scale 1; 60 Hz output refresh and user-configurable capture fps are separate.
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

1. VITURE SDK pose source: device lifecycle, quaternions, coordinate conventions,
   recentering, drift and disconnect handling.
2. Panel interaction: ray/plane hit testing, pointer coordinates and keyboard
   focus using supported compositor APIs.
3. Efficient capture: reuse buffers, GPU imports where supported, profile latency
   and hybrid-GPU transfers; prioritize visible panels.
4. Presentation: stereo modes after hardware verification, per-eye projection,
   optical calibration and comfortable panel sizing.

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
