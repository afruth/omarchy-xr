# Omarchy XR 0.5.0 — XR keys (draft, unreleased)

A spatial desktop for Omarchy 4 and Hyprland, currently supporting VITURE XR
glasses only.

This release gives both scene modes one set of controls that works with the
keyboard alone, or with keyboard and mouse. Hold **XR** (CTRL+ALT by default)
and press a key: Space recenters, = and - zoom, Up zooms out to everything,
Down focuses the window you look at, Return fills your view with it, Left and
Right step through your windows, / searches them, N dismisses a notification,
Home sends the pointer back to the laptop screen and H shows every key in the
headset. Holding **XR+G** (or the middle mouse button with XR held) grabs the
view: whatever you look at stays in front of you as you turn, and stays there
when you let go, a quick way to bring any window to the front.

Virtual monitors mode gains windows: XR+Down frames the window you look at,
XR+Left/Right walk the windows of all XR monitors, XR+Return maximizes one, and
XR+/ searches every window and brings up its workspace. The Window canvas keeps
its arrangement keys on the same layer.

Nothing of Omarchy's is taken over any more: SUPER+F, SUPER+TAB, ALT+TAB,
SUPER+arrows and SUPER+wheel keep their Omarchy meaning while XR runs, and the
Canvas tab's key-takeover switch is gone. **Controls → XR keys** shows every key
on one screen; click one and press the new key, or change the modifier. Conflicts
with Omarchy's bindings are named on the row. Touchpad gestures remain on
machines with a multitouch touchpad. The windowed preview no longer takes keys
or clicks of its own; the XR keys drive it like the glasses.

The XR keys need **XR controls version 7**: choose **Install XR controls** under
the mode selector or **Utilities → Setup & integrations → Set up everything**.
Earlier custom hotkeys (CTRL+Up/Down and the like) are replaced by the defaults.
See the [XR keys section of the README](../README.md#xr-keys) and the
[design](xr-controls-plan.md).
