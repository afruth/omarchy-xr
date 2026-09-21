# Stereo startup / missing DisplayPort, 2026-09-20

Observed on this Dell/VITURE Pro 2 installation after reboot, before a suspend:

- USB device and SDK pose stream remain present while `/sys/class/drm/card2-DP-1/status` reports `disconnected`.
- The SDK log acknowledges standard SBS command 0x32. No new viewer or display-helper log is created during the failed attempt, consistent with failure while waiting for stereo EDID, before leasing/rendering.
- A persisted `display-mode.json` with mode 52 (0x34, 1920x1080 at 120 Hz) causes the old SDK connect path to send 0x31 then 0x34 with a fixed four-second delay before attempting stereo. SDK shutdown also replayed restoration without observing the host.
- Startup cleanup could raise its own restoration exception and replace the original startup error.
- Kernel reports UCSI_GET_PDOS (-5), duplicate partner DisplayPort alternate mode and a VDO mismatch at boot and on USB-C controller rebind. These are evidence of controller/firmware problems, not proof that they caused this particular mode-switch failure.
- Cable reconnect, a targeted DRM `detect`, and the existing UCSI rebind did not restore video in the affected boot.

Fixes applied:

1. SDK connect checks whether the saved mode is already restored; if so, clears the recovery journal without setting a mode. Otherwise it preserves pending restoration without blindly changing video timing. A failed display query does not disable tracking.
2. SDK close no longer changes display modes independently of the manager. Unfinished recovery remains journaled.
3. Startup preserves the original stage/error even when rollback also fails. Append-only `display-events.jsonl` records stages, timestamps and compositor output snapshots.

Unit coverage verifies no mode writes during healthy reconnect, pending recovery or SDK close, tracking availability after a failed display query, and retention of both startup and recovery failures. Hardware validation is still pending a healthy video link; these fixes must not be described as a verified resolution of the initial link loss.

Next controlled test: after the user reboots, inspect the video link before starting stereo, connect SDK and confirm no unsolicited mode changes, start stereo once, then inspect `display-events.jsonl` and USB/DRM events. If startup succeeds, verify a stop/start cycle before calling this resolved. Original logs were copied to the user's state-directory diagnostics folder before reconnecting the SDK.
