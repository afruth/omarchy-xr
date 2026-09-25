# Window canvas

Window canvas is the second way Omarchy XR shows your desktop. Instead of virtual monitors, each
application window sits on its own panel on a ring around you. You look at a window to select it,
and the window you work in is live at 60 Hz with its menus, tooltips and mouse pointer. The design
is in [infinite-canvas-plan.md](infinite-canvas-plan.md).

It arrives with milestone M3. Search, Fill, the window switcher and arranging arrive in M4.

## Requirements

- **XR controls v6.** Window canvas relies on the controls adapter (`xr-controls.lua`) to list
  windows, stage them and keep them out of fullscreen. After updating, reinstall the controls once:
  **Utilities → Setup & integrations → Set up shortcuts & gestures**, or `make install-controls` in
  a source checkout. Until then the **Window canvas** option is disabled with a hint, and the renderer
  refuses `--canvas`.
- Hyprland with `hyprland_toplevel_export_manager_v1` v2 (the Lua generation of Omarchy has it).

## Turning it on

1. Stop stereo or the preview. The mode can only be changed while nothing is being viewed.
2. On **Controls**, choose **Window canvas** above the Start buttons.
3. Optionally open the **Canvas** tab (the second tab in canvas mode) to change the output
   refresh and scale, ring radius, window gap, label size, capture budget, exclusions and whether
   your windows move to the canvas at start. **Apply canvas settings** saves them to
   `canvas.json` in the state directory (`~/.local/state/omarchy-xr`).
4. Press **Start stereo** (or **Open windowed preview** in **Utilities**).

Choosing **Virtual monitors** again brings back monitor mode unchanged: your monitor setups and
`layout.json` are not touched by a canvas session.

## What happens to your windows

**At start** Studio creates one headless output named `OMXR-…-canvas` (2560×1440, scale 1.0,
60 Hz by default) with two workspaces: `omxr-canvas`, where the window you work in sits, and
`omxr-park`, a hidden workspace that holds all the others. Studio first records where every window
came from, including its workspace and whether it was tiled, floating, and at what size and
position. It saves this to `canvas-session.json`. Then it moves every regular window to
`omxr-park`. Tiled windows become floating at their tiled size. Windows on special workspaces,
such as the scratchpad, stay where they are. Excluded windows (a class or a process id) are never
moved: they stay on the laptop and off the ring, and one that opens on the canvas output is sent
to the laptop.

With **Move my windows to the canvas at start** turned off, the canvas starts empty. Windows you
open or move to the canvas later are still adopted.

**While it runs**, new windows join the canvas. A small window of an application that is already on
the canvas, such as a dialog, is staged in front of its parent. Canvas windows have no borders,
rounding, shadows, blur, dimming or animations, so the captured image is just the window.

**At stop** (**Stop stereo**, **Close preview** or **Stop & close canvas**) each window goes back to
its original workspace. A tiled window is tiled again; a floating one gets back its size and
position. A window you moved off the canvas yourself stays where you put it; if it was tiled
before, it is tiled again. Windows that had no recorded origin go to the laptop's active workspace.
Then the canvas output and its rules are removed. SUPER+F is Omarchy's again.

## Keys and the pointer

- **SUPER+F** is taken over while the canvas runs. In M3 it does nothing visible (it will be *Fill*
  in M4), and it never makes a window fullscreen. A window that asks for fullscreen itself, such as
  a browser after F11, is put back at once, because a fullscreen window would cover the canvas
  output.
- SUPER+number, SUPER+SHIFT+number and the scratchpad work as usual. A foreign workspace or the
  scratchpad that lands on the canvas output is sent to the laptop so the canvas never freezes.
  SUPER+SHIFT+number takes a window off the canvas and gives it back its borders and its tiled
  state, or its floating size and position; Stop does the same for every window.
- **The pointer** is held inside the window you are working in. Its movement past the edge
  continues over the canvas, and crossing into a neighbouring window brings that window to the
  front and puts the pointer on it. In the glasses the pointer you see is the real one, drawn into
  the window image with its menus. When that image is unavailable, the renderer draws an arrow
  instead. The spectator and the windowed preview show the same.
- **Releasing the pointer**: a three-finger double tap moves the pointer back to the centre of your
  laptop screen (the first display that is not an XR output), for example to use Studio.
- A click in the windowed preview or the spectator selects the window under it and puts the
  pointer there. Looking at a window and using **Fit** does the same in the glasses.

## Troubleshooting

- **"Window canvas needs XR controls v6"**: reinstall the controls (see Requirements). A controls
  file edited by hand, or an older one restored by a sync tool, shows the same hint.
- **SUPER+F makes windows fullscreen during a canvas session**: the controls are not active for this
  session. Check that the session was started from Studio and that `pose.sock.controls.mode` in the
  runtime directory (`$XDG_RUNTIME_DIR/omarchy-xr/`, next to `pose.sock.stats`) says `canvas`. If
  you customised SUPER+F in your Hyprland config, your binding comes back after the next
  `hyprctl reload`; until then SUPER+F has Omarchy's default fullscreen binding.
- **A window shows "capture unavailable" or stays grey**: its capture failed or the window closed.
  The canvas retries every 0.5–5 s. `pose.sock.stats` in the runtime directory lists every
  window's tier, rate and status.
- **Menus or the pointer are missing on the working window**: the renderer falls back to the
  window export when the region capture of the canvas output fails; its log says
  `Region capture of 0x…: …`. `OMARCHY_XR_NO_REGION=1` forces that fallback for comparisons.
- **After a crash** (Studio or the renderer killed), the next Studio start finds
  `canvas-session.json`, puts the windows back, removes the canvas output and turns the canvas rules
  off. A journal from another Hyprland session is dropped, because its window addresses no longer
  exist. If windows still sit on `omxr-park`, run **Stop & close canvas** once, or move them with
  SUPER+SHIFT+number.
- **Studio still shows the old mode or no Canvas tab** after an update: reinstall Studio and
  rescan plugins (see the README), then reopen Studio.
