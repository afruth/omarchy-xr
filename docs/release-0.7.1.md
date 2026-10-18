# Omarchy XR 0.7.1 — Start in the glasses with the lid closed

A spatial desktop for Omarchy 4 and Hyprland, currently supporting VITURE XR glasses only.

## Fixed

- **Starting with the lid closed works.** When the laptop panel was already off, **Turn off laptop display** reported "No active laptop display was found" and rolled back the whole start. With nothing to turn off, it now does nothing and the glasses keep running.
- **Virtual monitors get time to settle.** Turning off the laptop display could fail with "A virtual monitor is still being restored" right after the glasses were handed over, before Hyprland had applied the monitor rules. Studio now waits for up to 3 seconds, as Apply does, and logs which monitor did not settle if it still fails.
- **No suspend after stopping or a failed start with the lid closed.** 0.7.0 kept logind from suspending while the glasses were being handed over, but let go about a second before the desktop had turned the glasses back on. With the lid closed, logind saw no display in that gap and suspended. The display helper now waits until the glasses are back on the desktop, or have been unplugged, before letting logind act on the lid again.

## Install

Download `omarchy-xr-bin-0.7.1-1-x86_64.pkg.tar.zst` and verify it against `SHA256SUMS`, then run:

```sh
sudo pacman -U ./omarchy-xr-bin-0.7.1-1-x86_64.pkg.tar.zst
omarchy-xr-setup --controls --notifications
```

Marketplace users update the plugin and choose **Update XR runtime** inside Monitor Studio. Restart the Omarchy shell after updating an already-running copy so its QML is recreated.

The bundled VITURE SDK runtime is unchanged. Application terms reserve rights; this is not an open-source release.
