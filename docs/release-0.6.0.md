# Omarchy XR 0.6.0 — Mono 120 Hz and windows that follow you into XR

A spatial desktop for Omarchy 4 and Hyprland, currently supporting VITURE XR glasses only.

## New

- **Mono 120 Hz glasses mode.** A new **3D stereo · 60 Hz / Mono · 120 Hz** selector sits above **Start in glasses**. Mono gives up depth for 1920x1080 at 120 Hz, which halves the smear of text while you turn your head. Head tracking, virtual monitors and their resolutions stay the same. Mono uses the glasses' standard 120 Hz display timing and doesn't change the SDK display mode, so starting and stopping it are quick and safe. Stop works even if the glasses were unplugged during the session.
- **Windows come with you into XR.** Starting in the glasses used to leave the glasses' own windows stranded on hidden laptop workspaces. Now they move onto the virtual monitors, or into Canvas, before the display switches over. **Bring existing windows into XR** also works in virtual monitors mode and is on by default, so laptop windows move onto the virtual monitors when XR starts. Studio, the recording window and the search prompt always stay where they are.
- **Bring a window here from XR search.** In virtual monitors mode, pressing Enter in XR search moves a window that isn't in XR yet onto the monitor you are looking at (or last looked at).

## Improved

- **Gaze focus no longer moves the pointer around.** When you look around a window that already has focus, the pointer stays where it is. Explicit focus (confirm tap, XR+Up, search) still moves the pointer, and so does looking at a different window.
- **Gaze respects fullscreen windows.** A fullscreen or maximized window takes the gaze for everything it covers. Windows behind it, and windows on hidden workspaces, can no longer be focused by gaze and pop up fullscreen. Pinned windows on top still work.
- **Laptop and Canvas windows stay separate.** Windows opened on the laptop stay there, even browser windows that share a process with a Canvas window. Moving a desktop window into Canvas needs an explicit **Bring here**. A delayed pointer command can no longer undo **Return to desktop**.
- **Canvas focus hints** show the configured shortcut (zoom in, Ctrl+Alt+Up by default) instead of the old Ctrl+Alt+Down.
- **More reliable start and restore.** Hyprland sometimes misses the hotplug event when the glasses change video mode, and kept an outdated mode list. Start and restore now handle this, where they used to fail with "did not return to normal video" and block later starts.

## Under the hood

- XR controls are now version 10. Choose **Set up XR keys** under **Utilities → Setup & integrations** (or run `omarchy-xr-setup --controls`) so XR search can bring windows into XR.
- Camera fits and window-level focus decisions are logged in more detail.
- A plan for supporting RayNeo and XREAL glasses is in `docs/multi-vendor-glasses-plan.md`. Only VITURE glasses are supported so far.

The bundled VITURE SDK runtime is unchanged. Application terms reserve rights; this is not an open-source release.

## Install

Download `omarchy-xr-bin-0.6.0-1-x86_64.pkg.tar.zst` and verify it against `SHA256SUMS`, then run:

```sh
sudo pacman -U ./omarchy-xr-bin-0.6.0-1-x86_64.pkg.tar.zst
omarchy-xr-setup --controls --notifications
```

Marketplace users update the plugin and choose **Update XR runtime** inside Monitor Studio, then **Set up XR keys**. Restart the Omarchy shell after updating an already-running copy so its QML is recreated.

## Validation

Renderer unit, UX, Canvas, workspace-focus and mode-switch regressions pass, along with all 94 QML tests. The renderer builds successfully. The Python suite (219 tests) has one known failure that depends on the environment: a USB recovery test needs `/sys/bus/platform/drivers/ucsi_acpi`, which this machine lacks. A `scene_seam` label assertion in the notification checks fails the same way on 0.5.2, so it predates this release. Mono 120 Hz comfort still needs testing by someone wearing the glasses.
