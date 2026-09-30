# Omarchy XR 0.5.1 — VITURE Luma Ultra

A spatial desktop for Omarchy 4 and Hyprland, currently supporting VITURE XR
glasses only.

This release adds head tracking for **VITURE Luma Ultra** glasses (the SDK's
Carina family), requested in [#21](https://github.com/afruth/omarchy-xr/issues/21).
The bundled VITURE SDK 2.4.0 runtime already drives the Luma Ultra; Omarchy XR
now runs it in rotational (3DoF) mode, reads its pose 120 times per second and
feeds the same head-tracked views, recenter and stereo output as the other
glasses. Its cameras, VIO and 6DoF tracking are not used, and no additional
vendor libraries are packaged.

Gen1 and Gen2 glasses (One, Lite, Pro, Luma, Luma Pro, Luma Cyber, Beast,
Pro 2) are unchanged. Luma Ultra support has not yet been validated on
hardware; please report results on the issue.

The x86_64 package includes the unchanged VITURE glasses runtime. No separate
developer SDK download is needed. Application terms reserve rights; this is not
an open-source release.

## Install the local Arch package

Download `omarchy-xr-bin-0.5.1-1-x86_64.pkg.tar.zst` and verify its SHA-256
against `SHA256SUMS`, then run:

```sh
sudo pacman -U ./omarchy-xr-bin-0.5.1-1-x86_64.pkg.tar.zst
omarchy-xr-setup --controls --notifications
```

Marketplace users update the plugin and choose **Update XR runtime** inside
Monitor Studio. Restart the Omarchy shell after updating an already-running
copy so its QML is recreated.

## Validation

The renderer, Python, Lua, QML, package lifecycle, and plugin manifest suites
pass. The marketplace installer (**Install XR runtime** in Monitor Studio) pins
this release's package by URL, size (6,099,711 bytes) and SHA-256.
