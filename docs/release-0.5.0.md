# Omarchy XR 0.5.0 — XR keys

A spatial desktop for Omarchy 4 and Hyprland, currently supporting VITURE XR
glasses only.

This release gives both scene modes one set of controls that works with the
keyboard alone, or with keyboard and mouse. Hold **XR** (CTRL+ALT by default)
and press a key: Space recenters, = and - zoom, Up and Down step through three
zoom levels (everything, the monitor or frame you look at, the window you look
at, focused), Return fills your view with a window, Left and Right step through
your windows at the current level, / searches them, N dismisses a notification,
Home sends the pointer back to the laptop screen, H shows every key in the
headset and ` shows a performance card. Holding **XR+G** (or the middle mouse
button with XR held) grabs the view: whatever you look at stays in front of you
as you turn, and stays there when you let go. In the Window canvas, holding
**XR+F** carries the window you look at with your head; the other windows make
room and the drop never overlaps another window.

Looking at a window, or just beside it, now marks it at once with a thin rim;
keep looking and the dwell selects it as before. Looking alone never moves focus
or the pointer.

Virtual monitors mode gains windows: the zoom levels frame the monitor and then
the window you look at, XR+Left/Right walk the windows of all XR monitors,
XR+Return maximizes one, and XR+/ searches every window and brings up its
workspace. The Window canvas keeps its arrangement keys on the same layer.

Nothing of Omarchy's is taken over any more: SUPER+F, SUPER+TAB, ALT+TAB,
SUPER+arrows and SUPER+wheel keep their Omarchy meaning while XR runs, and the
Canvas tab's key-takeover switch is gone. **Controls → XR keys** shows every key
on one screen; click one and press the new key, or change the modifier. Conflicts
with Omarchy's bindings are named on the row. Touchpad gestures remain on
machines with a multitouch touchpad. The windowed preview no longer takes keys
or clicks of its own; the XR keys drive it like the glasses.

Text is sharper: captures are copied at power-of-two shares of their native size
and sampled with a density-based bias, instead of a fractional blit followed by
blurry mipmaps. The viewer no longer draws frames that would not change the
image, and an opt-in **Battery saver** (Studio → Battery) caps captures at 30 Hz
while the computer runs on battery. Studio allows up to 30 virtual monitors and
adds a 30 Full HD load-test setup.

Setup is safer on updates: package setup never replaces a newer plugin or its
controls with an older copy, and Studio offers **Update XR runtime** when the
installed package is older than the one the plugin installs.

The XR keys need **XR controls version 8**: choose **Install XR controls** under
the mode selector or **Utilities → Setup & integrations → Set up everything**.
Earlier custom hotkeys (CTRL+Up/Down and the like) are replaced by the defaults.
Controls installed from a development build as version 7 lack the performance
card, zoom-level and XR+F keys; Studio asks to update them.
See the [XR keys section of the README](../README.md#xr-keys) and the
[design](xr-controls-plan.md).

The x86_64 package includes the unchanged VITURE Gen1/Gen2 glasses runtime. No
separate developer SDK download is needed. Carina 6DoF and cameras are not
supported. Application terms reserve rights; this is not an open-source release.

## Install the local Arch package

Download `omarchy-xr-bin-0.5.0-1-x86_64.pkg.tar.zst` and verify its SHA-256
against `SHA256SUMS`, then run:

```sh
sudo pacman -U ./omarchy-xr-bin-0.5.0-1-x86_64.pkg.tar.zst
omarchy-xr-setup --controls --notifications
```

Run setup as your desktop user. Setup presents the application and SDK terms and
privacy notice before enabling the plugin. Controls and notifications are
optional, but the XR keys and the Window canvas need the controls. Reconnect the
glasses after first installation so the udev rules take effect.

Marketplace users update the plugin and choose **Update XR runtime** (or
**Install XR runtime**) inside Monitor Studio. Because the panel is kept loaded
by Omarchy, restart the Omarchy shell after updating an already-running copy so
its QML is recreated.

## Validation

The renderer, Python, Lua, QML, package lifecycle, and plugin manifest suites
pass. The marketplace installer (**Install XR runtime** in Monitor Studio) pins
this release's package by URL, size (6,099,056 bytes) and SHA-256.
