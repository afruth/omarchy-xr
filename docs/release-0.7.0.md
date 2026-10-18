# Omarchy XR 0.7.0 — Stabilization and safer glasses sessions

A spatial desktop for Omarchy 4 and Hyprland, currently supporting VITURE XR glasses only.

0.6.0 was prepared but never published, so this is the first public release since 0.5.2. It includes everything from the [0.6.0 notes](release-0.6.0.md): Mono 120 Hz glasses mode, windows following you into XR, and the gaze and restore fixes.

## New

- **Stabilization from 1x to 4x.** A new slider in Studio steadies the view. 1x is the tuned head-tracking filter, unchanged. Each step above it adds a small hold zone: swaying, typing, breathing and tremor no longer move the picture, while a deliberate turn is still tracked exactly. Changes apply to a running session, and the level is remembered.
- **Studio comes with you into XR.** With the laptop display off, Studio used to be impossible to see or use. Its window now moves into the virtual monitors or Canvas like any other window, and returns on Stop.

## Fixed

- **Starting stereo with the lid closed no longer suspends the laptop.** Handing the glasses over briefly makes them look unplugged. With the lid closed and the glasses as the only display, logind then suspended the machine. The display helper now keeps logind from acting on the lid until it has finished handing over or restoring the glasses.
- **Glasses sessions stop when the glasses can't show them.** Mono 120 Hz now checks the glasses' reported display mode and rolls back, with the laptop display still on, instead of leaving both screens black. A session also stops by itself when the glasses leave USB or their video port disconnects. A one-second blip doesn't count. Studio says why XR closed.

## Install

Download `omarchy-xr-bin-0.7.0-1-x86_64.pkg.tar.zst` and verify it against `SHA256SUMS`, then run:

```sh
sudo pacman -U ./omarchy-xr-bin-0.7.0-1-x86_64.pkg.tar.zst
omarchy-xr-setup --controls --notifications
```

Marketplace users update the plugin and choose **Update XR runtime** inside Monitor Studio. Users coming from 0.5.x should also choose **Set up XR keys** (XR controls are version 10 since 0.6.0). Restart the Omarchy shell after updating an already-running copy so its QML is recreated.

The bundled VITURE SDK runtime is unchanged. Application terms reserve rights; this is not an open-source release.

## Validation

Renderer unit tests and all 94 QML tests pass, and the renderer builds with warnings treated as errors. The Python suite (229 tests) has the same environment-dependent failure as 0.6.0: a USB recovery test needs `/sys/bus/platform/drivers/ucsi_acpi`, which this machine lacks. The `scene_seam` label assertion in the notification checks also still fails, as it has since 0.5.2. The lid-switch fix was checked against the real logind inhibitor list. Stereo start and stop with the lid closed still need to be tried with the glasses connected.
