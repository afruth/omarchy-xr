# Omarchy XR 0.3.1 — Marketplace setup update

A spatial desktop for Omarchy 4 and Hyprland, currently supporting VITURE XR
glasses only.

This patch makes first-time setup available directly in Monitor Studio. A
prominent card explains when the XR runtime or stereo helper is missing, and
**Utilities → Setup & integrations** provides buttons for the runtime, helper,
shortcuts and gestures, and XR notifications. User-facing status and error text
has also been simplified and brought up to date.

The x86_64 package includes the unchanged VITURE Gen1/Gen2 glasses runtime. No
separate developer SDK download is needed. Carina 6DoF and cameras are not
supported. Application terms reserve rights; this is not an open-source release.

## Install the local Arch package

Download `omarchy-xr-bin-0.3.1-1-x86_64.pkg.tar.zst` and verify its SHA-256
against `SHA256SUMS`, then run:

```sh
sudo pacman -U ./omarchy-xr-bin-0.3.1-1-x86_64.pkg.tar.zst
omarchy-xr-setup --controls --notifications
```

Run setup as your desktop user. Setup presents the application and SDK terms and
privacy notice before enabling the plugin. Controls and notifications are
optional. Reconnect the glasses after first installation so the udev rules take
effect.

Marketplace users can install the plugin first and choose **Install XR runtime**
inside Monitor Studio. Because the panel is kept loaded by Omarchy, restart the
Omarchy shell after updating an already-running copy so its QML is recreated.

## Validation

The renderer, Python, Lua, QML, package lifecycle, and plugin manifest suites
pass. The release archive and Arch package are built from the same tagged source,
and the marketplace installer verifies the package size and SHA-256 before
asking pacman to install it.
