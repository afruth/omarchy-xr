# Omarchy XR 0.4.0 — Window canvas

A spatial desktop for Omarchy 4 and Hyprland, currently supporting VITURE XR
glasses only.

This release adds a second scene mode next to virtual monitors: the Window
canvas. Every window of the desktop sits on a ring around you, and the window you
work in is captured live at 60 Hz while the others refresh on a pixel budget
that steps down gracefully as more windows are visible. Search, Fill, the
window switcher, arrange, pin, the radar and the in-headset help work on the
ring, and new windows pulse briefly or point you to where they appeared. In the
windowed preview, windows can be dragged in Overview.

Monitor Studio now switches live between virtual monitors and the canvas while
stereo runs, without restarting the viewer or the glasses; if the renderer does
not confirm the switch it falls back to a quick stop and restart in the same
presentation. Notifications appear inside the ring in canvas mode and now also
in flat and windowed runs. The canvas requires XR controls version 6: reinstall
them via **Utilities → Setup & integrations**. See the
[Window canvas guide](window-canvas.md).

Dedicated stereo now starts on AMD graphics (`amdgpu`). The driver kept
re-applying its cached EDID, so the compositor never saw the glasses as a
headset and could not lease them; the stereo helper now refreshes that cache and
signals the compositor. Intel keeps its previous, unchanged sequence. Setup is
simpler too: **Utilities → Setup & integrations → Set up everything** installs or
updates the stereo helper, controls and notifications in one terminal, and the
Window canvas option offers **Install XR controls** directly when they are
missing.

The x86_64 package includes the unchanged VITURE Gen1/Gen2 glasses runtime. No
separate developer SDK download is needed. Carina 6DoF and cameras are not
supported. Application terms reserve rights; this is not an open-source release.

## Install the local Arch package

Download `omarchy-xr-bin-0.4.0-1-x86_64.pkg.tar.zst` and verify its SHA-256
against `SHA256SUMS`, then run:

```sh
sudo pacman -U ./omarchy-xr-bin-0.4.0-1-x86_64.pkg.tar.zst
omarchy-xr-setup --controls --notifications
```

Run setup as your desktop user. Setup presents the application and SDK terms and
privacy notice before enabling the plugin. Controls and notifications are
optional, but the Window canvas needs the controls. Reconnect the glasses after
first installation so the udev rules take effect.

Marketplace users can install the plugin first and choose **Install XR runtime**
inside Monitor Studio. Because the panel is kept loaded by Omarchy, restart the
Omarchy shell after updating an already-running copy so its QML is recreated.

## Validation

The renderer, Python, Lua, QML, package lifecycle, and plugin manifest suites
pass. The monitor-mode preview stills (`make check-preview`) are pixel-identical
to the 0.3.1 baselines, and the live canvas checks (`make check-canvas-live`,
`make smoke-canvas`) pass on the development machine. The end-to-end manual
checklist with the glasses is in the [Window canvas guide](window-canvas.md).

The marketplace installer (**Install XR runtime** in Monitor Studio) pins this
release's package by URL, size (5,638,571 bytes) and SHA-256.
