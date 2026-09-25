# Window canvas

Window canvas is the second way Omarchy XR shows your desktop. Instead of virtual monitors, each
application window sits on its own panel on a ring around you. You look at a window to select it,
and the window you work in is live at 60 Hz with its menus, tooltips and mouse pointer. The design
is in [infinite-canvas-plan.md](infinite-canvas-plan.md).

It arrived with milestone M3; search, Fill, the window switcher, arranging, pinning, the radar strip
and the F1 key help arrived with M4.

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

## Keys

These are the canvas keys; **F1** shows the same table in the glasses (and in the preview).

| Keys | What they do |
|---|---|
| **SUPER+CTRL+G** | Search your windows by title, class or kind (browser, terminal, editor, …). After you zoom out to Overview (SUPER+TAB or a flick out) you can also just start typing: the search field opens with it. |
| type, **↑/↓**, **Tab/Shift+Tab** | Filter and move the selection; the camera follows the best match and the other windows dim. |
| **Enter** / **Shift+Enter** | Land on the selection / summon it next to you first. A window that is not on the canvas yet is marked *bring to canvas* and is brought over. |
| **Ctrl+1…8** | Land on that row of the results. |
| **Esc** | Clears the text; a second Esc closes the search and puts the camera and focus back where they were. An open help closes first. |
| **ALT+TAB** / **ALT+SHIFT+TAB** | Recent-window switcher: a quick tap flips to the previous window, holding shows the list (after 0.2 s), releasing Alt lands. |
| **SUPER+F** | Fill: the window you work in grows to about 90 % of your view with sharp native text. Press again to restore: untouched, it gets its old size and place back; moved, it keeps the new place with the old size; resized in between, it fills again (and a later restore still returns to the size from before Fill). A flick in fills too, a flick out restores. |
| **SUPER+TAB** | Overview on and off. |
| **SUPER+arrows** | Land on the neighbouring window in that direction. |
| **SUPER+SHIFT+arrows** | Nudge the window you work in by 100 px (it may overlap others). |
| **Ctrl+A**, **Ctrl+Z**, **Ctrl+Shift+Z** (in the search field) | Arrange: group windows by kind, then application, along the ring from where you look, without overlap, and show the Overview; undo and redo arrange, nudge and summon. Studio's **Arrange** and **Undo** work from any view. |
| **SUPER+ALT+P** | Pin the window you work in to your view (body-locked), or unpin it. |
| **F1** (in the search field) | This help. It lists the takeover keys only while the takeover switch is on. |

**Take over Omarchy window keys in canvas mode** (Studio, **Canvas** tab, on by default) governs
SUPER+TAB, ALT+TAB, ALT+SHIFT+TAB, SUPER+arrows and SUPER+SHIFT+arrows. Turned off, those keep
Omarchy's meaning (next workspace, window cycling, focus and swap) and the canvas uses only
SUPER+F, SUPER+CTRL+G and SUPER+ALT+P, which are always taken while the canvas runs. When the
canvas stops, every taken chord gets Omarchy's default binding back; a chord you customised in
your Hyprland config returns with the next `hyprctl reload`. Studio's XR hotkeys (Input tab) cannot
use these canvas chords, in either mode.

The **radar strip** under the view in Overview and search shows the whole ring around you: three
row lanes, the window you work in in the accent colour and the part of the ring you are looking at.

Studio's **View controls** show **Overview**, **Land on window**, **Search**, **Fill**, **Arrange**
and **Undo** in canvas mode.

**In the windowed preview** the renderer window takes these keys itself while it has keyboard focus:
`/` search (then type), `F` Fill, `O` Overview, `P` pin, `Tab`/`Shift+Tab` switcher (Return, or 1.5 s without a step, lands),
`Alt+arrows` neighbour, `Alt+Shift+arrows` nudge, `Ctrl+A`, `Ctrl+Z`, `Ctrl+Shift+Z`, `F1`, and
`Esc`, which closes overlays before it quits. Without keyboard focus (and in the glasses) search
typing goes to a small search field that Studio keeps loaded on the canvas output; it holds the
keyboard only while the search is open.

## The pointer and other keys

- **SUPER+F** is taken over while the canvas runs: it is *Fill* (see Keys below) and never makes a
  window fullscreen. A window that asks for fullscreen itself, such as a browser after F11, is put
  back at once and filled instead, because a fullscreen window would cover the canvas output.
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

- **Typing in Overview or SUPER+CTRL+G does nothing in the glasses**: the search field lives in the
  Studio plugin, so Studio must have been started once in this Quickshell session (it stays loaded
  when hidden). Check that `pose.sock.controls.prompt` in the runtime directory says `v1 <pid> <seq> 1 …`
  while the search is open; the field answers in `pose.sock.controls.search`. The field opens by itself
  only when you zoom out from a window; an Overview shown at start or after Esc closed a search has
  none (so it never grabs the keyboard unasked): press SUPER+CTRL+G.
- **ALT+TAB, SUPER+TAB or SUPER+arrows do Omarchy's thing during a canvas session**: the takeover
  switch is off (Canvas tab), or the controls predate M4: reinstall them.

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
