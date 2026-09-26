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
   `canvas.json` in the state directory (`~/.local/state/omarchy-xr`). The capture budget
   (default 300 Mpix/s) is how many window pixels per second the canvas asks Hyprland to export,
   summed over all windows. While the canvas runs, a line under the field shows how much of it is in
   use, whether the canvas has lowered it by itself, the capture latency and how many live slivers
   there are (see **Capture rates**).
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

## Capture rates

Each window is captured at its own rate, taken from the ladder 60, 40, 30, 24, 20, 15, 10 and 6 Hz.
The window you work in always gets 60 Hz. The four largest other windows in view share what is left
of the budget at one rate, as high as still leaves 6 Hz for the rest, and the remaining windows in
view get up to 10 Hz. In Overview every window except the one you work in gets up to 10 Hz. When
even 6 Hz does not fit, the smallest windows in view keep their last picture instead of going over
the budget. Rates drop at once and rise again only after 2 seconds of headroom.

The budget you set is a ceiling. When Hyprland takes clearly longer than usual to deliver captures
(more than 2.5 output frames for two seconds; a healthy capture takes two), or the GPU runs out of
time, the canvas lowers its own budget and raises it back slowly once things are calm again; it never
goes above your setting, so raise the setting if you want more. The readout under the budget field
shows this as "self-limited".

**Live slivers.** A window rated above 30 Hz is moved to an 8-pixel strip at the right edge of the
canvas output (a "sliver"), because Hyprland redraws a hidden (parked) window at most about 30 times a
second, and only a window on the visible workspace can be captured faster. Slivers are stacked 24 px
apart, ignore the pointer, and the window you stage is kept 8 px narrower than the output so it never
covers them. When the rate drops to 30 Hz or below the window goes back to the hidden workspace; a
window changes place at most once every 2 seconds. On a small canvas (two to four windows in view)
the windows next to the one you work in are usually slivers.

**Far over budget.** When many windows are in view (a zoomed-out Overview of 50 windows), 6 Hz for
every one of them would exceed the budget. The canvas then keeps the last picture of the smallest
windows instead of going over, because going over slows every capture down, the window you work in
included. With the default budget a 60 Hz window plus about 30 other 720p windows run at 6 Hz; the
rest show their last frame until you zoom in or raise the budget.

What the ladder gives when every window is in view (1920×1080 windows, the default budget; the
window you work in, when there is one, is always 60 Hz):

| Windows | Others | Mpix/s used |
|---|---|---|
| 2 | 40 Hz (a live sliver) | 207 |
| 4 | 3 × 24 Hz | 274 |
| 6 | 4 × 15 Hz near, 1 × 10 Hz | 269 |
| 11 | 4 × 10 Hz near, 6 × 6 Hz | 282 |
| Overview, 30 × 1280×720, none of them yours | 10 Hz each | 276 |
| Overview, 40 or 50 × 1280×720, none of them yours | 6 Hz each | 221 / 276 |
| Overview, your 1080p window and 49 × 1280×720 | 31 × 6 Hz, 18 keep their last picture | 296 |

Measured on the development machine (Intel Tiger Lake GT1 iGPU, the canvas output at
2560×1440@60, test windows drawing at 60 fps, the default budget, the renderer in a 1280×720
window; "in view" counts the windows besides yours that this view shows, the rest are idle). Every
window ran within 0.2 fps of its rate, request→ready stayed at 33.1–33.3 ms, the canvas never
lowered its budget, Hyprland used 5–11 % of a CPU core, and over a one-hour run neither the rates
nor the renderer's GPU memory moved:

| Set | In view | Work: your window / others | Overview |
|---|---|---|---|
| 2 × 1080p | 1 | 60 / 40 (sliver) | 60 / 10 |
| 4 × 1080p | 2 | 60 / 2 × 40 (slivers) | 60 / 3 × 10 |
| 4 × 1080p, wider view | 3 | 60 / 3 × 24 | 60 / 3 × 10 |
| 6 × 1080p, wider view | 4 | 60 / 4 × 20 | 60 / 5 × 10 |
| 11 × 1080p | 7 | 60 / 4 × 15 + 3 × 6 | 60 / 10 × 6 |
| 1080p video at 30 fps + 1080p | 2 | 60 / 2 × 40 (slivers, the video without dropped captures) | 60 / 2 × 10 |
| 50 mixed windows | 25 | 60 / 4 × 40 (slivers) + 21 × 6 | 60 / 44 × 6, 5 frozen |
| 50 × 720p, none of them yours | 25 | 4 × 40 (slivers) + 21 × 6 | 50 × 6 |

The **Capture budget** field sets the ceiling; the line under it shows the use ("Using 289 of 300
Mpix/s"), "self-limited" when the canvas has lowered its budget, the capture latency and the number
of live slivers. The development machine handles about 330–370 Mpix/s before captures slow down.

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

- **A thin strip of windows at the right edge of the canvas output** (8 px wide, visible on the
  canvas output or in a screenshot of it): these are live slivers, windows the canvas captures above
  30 Hz. This is expected; they ignore the pointer and go back to the hidden workspace when their
  rate drops. `pose.sock.stats` lists them with `"place":"sliver"`.
- **Some thumbnails freeze in Overview or with many windows in view**: the canvas is far over its
  capture budget and keeps the last picture of the smallest windows rather than slowing everything
  down. The budget line in Studio's Canvas tab shows the use. Raise the capture budget (the
  development machine handled up to about 350 Mpix/s) or close or exclude windows you do not need;
  zooming in on a window also brings its neighbours back to life.
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
