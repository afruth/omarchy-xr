# Architecture and development milestones

## Proposed data flow

Hyprland headless outputs -> capture backend -> GPU textures -> spatial renderer
-> glasses display. A VITURE pose source supplies camera orientation. An input
router maps mouse interaction to the selected output and application.

Keep tracking, capture, rendering, and input routing separate as these components
are introduced. Start with one process. Add a tracking service only if measured
latency or device ownership requirements justify it.

## Milestones

1. **Development preview (implemented):** build locally, render three synthetic
   panels, mouse-controlled camera, recenter, CLI and rendering smoke checks.
2. **Tracking proof:** verify Pro 2 with the current official Linux SDK; log
   timestamps and orientation, establish coordinate conventions, apply relative
   orientation with a recenter reference, handle device loss. Use quaternions for
   SDK poses rather than the preview's simplified Euler camera.
3. **One live desktop (baseline implemented):** explicitly select a Wayland output,
   capture via wlr-screencopy shared memory, convert to RGBA and upload to the center
   panel. Tested on an independently created Hyprland headless output. Creation
   and app placement remain manual; latency profiling and GPU import are pending.
4. **Three interactive desktops:** independent live outputs, panel hit testing,
   pointer coordinate conversion and keyboard focus. Explicitly avoid capturing
   the renderer's own output. Restore windows and remove owned outputs on exit.
5. **Glasses presentation:** enumerate real display modes, verify stereo support,
   implement per-eye projection if supported, profile frame pacing and drift.
6. **Daily use:** saved layout, hotkeys, reconnect handling, packaging, documented
   SDK acquisition and licensing.

## Early decisions to validate

- Virtual monitors provide simultaneous live content. Inactive workspaces alone
  are not assumed to provide capturable frames.
- Prefer GPU buffer import where supported; establish a correct baseline before
  optimizing. Multi-GPU copies may matter on hybrid Intel/NVIDIA laptops.
- Keep viewing orientation independent from pointer movement and desktop focus.
- Do not assume unrestricted global input injection on Wayland. Prototype focus
  and input routing with supported compositor interfaces.
- Rotational tracking does not establish a persistent room-space anchor. Provide
  recentering and evaluate yaw drift; do not promise 6DoF on Pro 2.
- The preview is monoscopic and its projection is not an optical calibration.

## References

- [VITURE SDK](https://www.viture.com/en-SG/developer/glasses-sdk/glasses)
- [Hyprland headless outputs](https://wiki.hypr.land/configuring/core/advanced-configuration/using-hyprctl/)
- [Breezy Desktop](https://github.com/wheaney/breezy-desktop)
- [XRLinuxDriver](https://github.com/wheaney/XRLinuxDriver)

Existing projects are research references; their code has not been copied here.
The wlr-screencopy protocol XML is vendored separately with its copyright notice.
