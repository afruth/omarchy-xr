# Omarchy XR 0.5.2 — Workspace and comfort workflows

A spatial desktop for Omarchy 4 and Hyprland, currently supporting VITURE XR glasses only.

This release simplifies starting XR and managing your spatial workspace:

- **Start in glasses** and **Preview on desktop** prepare the selected workspace automatically. Mode descriptions and the Canvas adoption option explain what happens to existing windows.
- Window labels show where **keyboard input** goes, independently of gaze selection. Confirmation hints remain available, and Canvas settings can require confirmation before pointer focus transfers.
- Monitor edits show a pending change summary and offer **Revert**. Setup switching can save the current draft first. Global desktop text size lives under **Appearance**.
- The Canvas window map shows actual geometry and titles, with **Focus**, **Bring here**, **Pin**, and **Return to desktop** actions.
- A comfort guide walks through recentering, an XR reading sample, distance adjustment, and checking application windows. Saved views return across restarts for each mode. Three task presets offer starting layouts, and following newly opened Canvas windows is configurable.

This includes the Luma Ultra support introduced in 0.5.1. The bundled VITURE SDK runtime is unchanged. Application terms reserve rights; this is not an open-source release.

## Install

Download `omarchy-xr-bin-0.5.2-1-x86_64.pkg.tar.zst` and verify it against `SHA256SUMS`, then run:

```sh
sudo pacman -U ./omarchy-xr-bin-0.5.2-1-x86_64.pkg.tar.zst
omarchy-xr-setup --controls --notifications
```

Marketplace users update the plugin and choose **Update XR runtime** inside Monitor Studio. Restart the Omarchy shell after updating an already-running copy so its QML is recreated.

## Validation

The renderer UX acceptance tests, Canvas/focus/mode regressions, and all 93 QML tests pass. The renderer builds successfully. The full Python suite has an environment-dependent USB recovery test failure because this machine lacks `/sys/bus/platform/drivers/ucsi_acpi`; existing lint issues remain. Headset comfort still requires wearer validation.
