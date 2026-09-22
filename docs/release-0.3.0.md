# Omarchy XR 0.3.0 — Arch Linux preview

A spatial desktop for Omarchy 4 and Hyprland, with monitor layouts, head
tracking, stereo output and optional spatial notifications.

This x86_64 release includes the unchanged VITURE Gen1/Gen2 glasses runtime.
No separate developer SDK download is needed. Carina 6DoF and cameras are not
supported. Application terms reserve rights; this is not an open-source release.

## Install the local Arch package

Download `omarchy-xr-bin-0.3.0-1-x86_64.pkg.tar.zst` and verify its SHA-256
against `SHA256SUMS`, then run:

```sh
sudo pacman -U ./omarchy-xr-bin-0.3.0-1-x86_64.pkg.tar.zst
omarchy-xr-setup --controls --notifications
```

Run setup as your desktop user. Setup presents the application/SDK terms and
privacy notice before enabling the plugin. The controls and notifications flags
are optional. Reconnect the glasses after the first installation for udev access.
An existing source installation should remove its manually installed display
helper before package installation; see docs/distribution.md.

The application tarball is the complete runtime used by the AUR recipe.
The AUR submission archive contains the matching PKGBUILD, .SRCINFO and terms.
AUR and marketplace listings are prepared but not submitted yet.

## Validation

The Arch package builds with dependency and checksum checks. 115 Python tests,
including isolated setup/update/removal, pass. Renderer/notification tests and
54 QML tests pass. The plugin passes Omarchy's manifest validator. Ruff and mypy
pass; existing broader lint limitations are recorded in the distribution
validation document. No live desktop configuration was changed by package tests.
