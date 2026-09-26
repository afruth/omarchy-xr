# Window canvas

Window canvas is the second way Omarchy XR shows your desktop. Instead of virtual monitors, each
application window sits on its own panel on a cylinder around you — beside, above and below you; the
view scrolls up and down. You look at a window to select it,
and the window you work in is live at 60 Hz with its menus, tooltips and mouse pointer. The design
is in [infinite-canvas-plan.md](infinite-canvas-plan.md).

It arrived with milestone M3; search, Fill, the window switcher, arranging, pinning, the radar strip
and the F1 key help arrived with M4; the capture ladder with M5; the live mode switch, notifications
inside the ring, new-window cues and the Overview mouse drag arrived with M6; focusing and moving
windows in the glasses with M7; the full cylinder (windows at any height, vertical scroll) with M8.

## Requirements

- **XR controls v6.** Window canvas relies on the controls adapter (`xr-controls.lua`) to list
  windows, stage them and keep them out of fullscreen. After updating, reinstall the controls once:
  **Utilities → Setup & integrations → Set up shortcuts & gestures**, or `make install-controls` in
  a source checkout. Until then the **Window canvas** option is disabled with a hint, and the renderer
  refuses `--canvas`.
- Hyprland with `hyprland_toplevel_export_manager_v1` v2 (the Lua generation of Omarchy has it).

## Turning it on

1. On **Controls**, choose **Window canvas** above the Start buttons.
2. Optionally open the **Canvas** tab (the second tab in canvas mode) to change the output
   refresh and scale, ring radius, window gap, label size, capture budget, exclusions and whether
   your windows move to the canvas at start. **Apply canvas settings** saves them to
   `canvas.json` in the state directory (`~/.local/state/omarchy-xr`). The capture budget
   (default 300 Mpix/s) is how many window pixels per second the canvas asks Hyprland to export,
   summed over all windows. While the canvas runs, a line under the field shows how much of it is in
   use, whether the canvas has lowered it by itself, the capture latency and how many live slivers
   there are (see **Capture rates**).
3. Press **Start stereo** (or **Open windowed preview** in **Utilities**).

**Switching while XR runs.** The mode selector stays available while stereo or a preview runs; Studio
shows "Switching the XR view…" and then "Switched to Window canvas." (or "…Virtual monitors."). The
renderer, the glasses' stereo presentation and its display lease stay as they are; only the scene
changes:

1. Studio first prepares the desktop side of the new mode: for Window canvas it creates the canvas
   output (to the right of the monitor outputs, which are still live) and moves your windows to
   `omxr-park`, exactly as at a canvas start; for Virtual monitors it returns the canvas windows to
   where they came from and applies your saved monitor layout (the monitor outputs are placed to the
   right of the canvas output, and `viewer.tsv` is written).
2. It then tells the running renderer: the `mode:canvas` or `mode:monitors` message on `pose.sock`.
   The renderer builds the new scene in place and reports the new mode in `pose.sock.stats` at once.
3. Only after that acknowledgement does Studio remove the other mode's outputs (the monitor outputs,
   or the canvas output and its rules; SUPER+F and the other taken keys become Omarchy's again).

If the renderer does not answer within 3 seconds, Studio stops XR and starts it again in the new mode,
in the same presentation (stereo or the preview). The new mode is saved before that, so even if the
restart fails the new mode stays selected. A switch is refused while the laptop display is off
("Turn the laptop display back on before switching the render mode."), because returning the canvas
windows needs a computer display; turn the laptop display on first. With XR stopped, choosing a mode
only selects it for the next start.

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

## New windows

A new window opens next to where you look, sideways and up or down, without overlap. A window that
opens while the canvas runs gets a short halo pulse (300 ms) where it lands, so you see
where it went. When it is placed outside your view, an accent-coloured chevron at the edge of the view
points towards it for up to 3 seconds, or until you turn and its centre is in view. The windows moved
to the canvas at start, and a canvas you switch to, show neither.

## Moving windows

In the glasses, **SUPER+left-drag** moves the window you work in, in any direction, snapped to 20 px:
it follows the pointer's travel (also past the edge of the window), may overlap other windows (like a
nudge), and the new place is remembered for the next start. Letting go of SUPER before the button also drops it, and a drag that
holds still for 3 s ends there. **Ctrl+Z** undoes it. Only the panel moves: the real window stays where
the canvas keeps it, so the pointer, menus and the native cursor stay right. When the window leaves your
view, the camera follows it after the drop.

**SUPER+right-drag** resizes the window you work in, from any corner; the panel follows. Half a second
after you let go, the window is put back at the stage origin and trimmed to the canvas output, so a
top-left corner drag cannot leave it misplaced. **SUPER+CTRL+arrows** resize it in 100 px steps.
**SUPER+SHIFT+arrows** nudges it by 100 px, **Shift+Enter** in the search summons a window next to
you, and **Ctrl+A** arranges the windows (see **Keys**).

In the windowed preview (and a flat `--display` run) you can also drag with the mouse, without SUPER:
in Overview or the search, a left-drag moves the window under the pointer over the cylinder, with the same
snapping, memory and undo. A press that moves less than 4 px is an ordinary click: it selects
the window and puts the pointer there. In Work a plain press never drags. The spectator has no mouse
drag.

## Notifications

Notifications work in both modes. In canvas mode the cards float inside the ring, never behind a
window, in the lower part of your view, and are drawn over the windows. Look at a card and flick up
with three fingers to dismiss it, or down to cycle through the stack, exactly as in monitor mode.

The cards also show in the flat glasses view and in the windowed preview whenever the XR controls are
set up (the renderer then has a pose socket), not only in stereo.

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
| **SUPER+F** | Fill: the window you work in grows to about 90 % of your view with sharp native text. Press again to restore: untouched, it gets its old size and place back; moved, it keeps the new place with the old size; resized in between, it fills again (and a later restore still returns to the size from before Fill). A flick in fills too, a flick out restores. With no window on the stage, SUPER+F focuses the window you look at first and then fills it. |
| **SUPER+TAB** | Overview on and off. Leaving the Overview lands on and focuses the window you look at. |
| **Ctrl+Down** (your `fit_target` key) or a **three-finger tap** | Focus the window you look at: it is staged, raised, gets the keyboard, and the pointer goes to the point you look at. |
| **SUPER+arrows** | Land on the neighbouring window in that direction; every landing brings its window to eye level. |
| **SUPER+wheel** | Scroll the cylinder up or down, a fifth of the view per notch. A takeover of Omarchy's workspace scroll. |
| **SUPER+CTRL+Page_Up / Page_Down** | Scroll the cylinder a page up or down. |
| **4-finger pan** | Sideways turns the view along the ring; up and down scrolls the cylinder. |
| **SUPER+SHIFT+arrows** | Nudge the window you work in by 100 px, also up and down (it may overlap others). |
| **SUPER+left-drag** | Move the window you work in, in any direction (20 px steps, **Ctrl+Z** undoes it, the place is remembered). |
| **SUPER+right-drag** | Resize it; the panel follows. When the drag ends the window is put back at the stage origin. |
| **SUPER+CTRL+arrows** | Resize it by 100 px: Left/Right narrower/wider, Up/Down shorter/taller. |
| **Ctrl+A**, **Ctrl+Z**, **Ctrl+Shift+Z** (in the search field) | Arrange: group windows by kind, then application, into a block around where you look, without overlap, and show the Overview; undo and redo arrange, nudge and summon. Studio's **Arrange** and **Undo** work from any view. |
| **SUPER+ALT+P** | Pin the window you work in to your view (body-locked), or unpin it. |
| **F1** (in the search field) | This help. It lists the takeover keys only while the takeover switch is on. |

**Take over Omarchy window keys in canvas mode** (Studio, **Canvas** tab, on by default) governs
SUPER+TAB, ALT+TAB, ALT+SHIFT+TAB, SUPER+arrows, SUPER+SHIFT+arrows and SUPER+wheel. Turned off, those keep
Omarchy's meaning (next workspace, window cycling, focus and swap, workspace scroll) and the canvas uses only
SUPER+F, SUPER+CTRL+G, SUPER+ALT+P, SUPER+left-drag, SUPER+CTRL+arrows and SUPER+CTRL+Page_Up/Page_Down,
which are always taken while
the canvas runs. When the
canvas stops, every taken chord gets Omarchy's default binding back; a chord you customised in
your Hyprland config returns with the next `hyprctl reload`. Studio's XR hotkeys (Input tab) cannot
use these canvas chords, in either mode.

Two of these shadow Omarchy chords while the canvas runs. **SUPER+CTRL+Left/Right** are Omarchy's
*Move grouped window focus* keys; they come back when the canvas stops. **SUPER+left-drag** is caught
everywhere, so windows on the laptop screen cannot be dragged with it during a canvas session (use
SUPER+right-drag or the keyboard there, or stop the canvas). The F1 help always says Ctrl+Down; the
first-dwell hint in the glasses shows the key you actually configured.

The **radar strip** under the view in Overview and search shows the whole ring around you and the
height above and below eye level: every window as a mark, the window you work in in the accent colour,
and the rectangle you are looking at.

Studio's **View controls** show **Overview**, **Land on window**, **Search**, **Fill**, **Arrange**
and **Undo** in canvas mode.

**In the windowed preview** the renderer window takes these keys itself while it has keyboard focus:
`/` search (then type), `F` Fill, `O` Overview, `P` pin, `Tab`/`Shift+Tab` switcher (Return, or 1.5 s without a step, lands),
`Alt+arrows` neighbour, `Alt+Shift+arrows` nudge, `PageUp`/`PageDown` scroll, `Ctrl+A`, `Ctrl+Z`, `Ctrl+Shift+Z`, `F1`, and
`Esc`, which closes overlays before it quits. Without keyboard focus (and in the glasses) search
typing goes to a small search field that Studio keeps loaded on the canvas output; it holds the
keyboard only while the search is open.

## Gaze, dwell and focus

Looking at a window **selects** it: after a short dwell (500 ms of steady gaze) it gets the halo, and
the search, Fill and nudges act on it. Looking never moves the keyboard or the pointer by itself, so a
glance at another window does not steal your typing. To work in the window you look at, **confirm**:
press **Ctrl+Down** (the `fit_target` key from Studio's Input tab) or tap the touchpad once with three
fingers. The window is staged, raised and gets the keyboard, and the pointer goes to the point you are
looking at. Leaving the Overview with a flick in or SUPER+TAB confirms the window you land on the same
way.

For the first three windows you dwell on in a session, a small hint under the window's label says
which key confirms ("Ctrl+Down or a three-finger tap to focus", with your configured key). It stops
after your first confirm.

## The pointer and other keys

- **A window is always staged** while the canvas has windows: at start, and when the staged window
  closes or leaves the canvas, the most recently used one is staged and the camera lands on it. This
  staging is quiet: it never takes the keyboard or moves the pointer. Only a confirm, a click or the
  pointer crossing into a window does.
- **SUPER+F** is taken over while the canvas runs: it is *Fill* (see Keys) and never makes a
  window fullscreen. A window that asks for fullscreen itself, such as a browser after F11, is put
  back at once and filled instead, because a fullscreen window would cover the canvas output. With
  no window on the stage, SUPER+F confirms the window you look at first and fills it once it is staged.
- SUPER+number, SUPER+SHIFT+number and the scratchpad work as usual. A foreign workspace or the
  scratchpad that lands on the canvas output is sent to the laptop so the canvas never freezes.
  SUPER+SHIFT+number takes a window off the canvas and gives it back its borders and its tiled
  state, or its floating size and position; Stop does the same for every window.
- **The pointer** is held inside the window you are working in. Its movement past the edge
  continues over the canvas, and crossing into a neighbouring window brings that window to the
  front and puts the pointer on it. In the glasses the pointer you see is the real one, drawn into
  the window image with its menus. When that image is unavailable, the renderer draws an arrow
  instead. The spectator and the windowed preview show the same.
- **Three-finger taps**: a single tap confirms 0.4 s after it (when no second tap follows); a
  **double tap** still recenters and moves the pointer back to the centre of your laptop screen (the
  first display that is not an XR output), for example to use Studio. A swipe cancels a pending tap.
- A click in the windowed preview or the spectator selects the window under it and puts the
  pointer there. Studio's **Land on window** confirms like Ctrl+Down.

## Troubleshooting

- **I can look at windows but not type or click**: looking only selects; press your `fit_target` key
  (Ctrl+Down by default) or tap once with three fingers. After it, `pose.sock.controls.hover` in the
  runtime directory shows `v4 … 1 0x…` (the window address) and `viewer.log` shows `Canvas: confirm 0x…`
  and `Canvas: land on …`. When the confirm is logged but the pointer does not move, the controls are
  older than this release: reinstall them.
- **`viewer.log` says `Canvas: N windows, staged none`**: the adapter stages the most recent canvas
  window within half a second of the canvas start. If it never does, the installed controls predate
  M7: reinstall them (`make install-controls`, or Studio's setup). A confirm still stages a window with
  older controls.
- **"Could not start stereo … Connect or turn on a computer display"**, or stereo starts with
  "Recording window skipped: no computer display": the laptop display is off, usually after a Stop that
  failed to leave side-by-side. Studio now starts without the recording window and says so in the
  Recording card and in `display-events.jsonl` (`spectator-skipped`). Turn the laptop display on
  (Controls) to get the recording window back; turning it on by hand still refuses without a display.
- **SUPER+left-drag moves the real window** (the panel stays, the window slides off the stage and the
  pointer confinement looks wrong): the controls were not reinstalled after the update, so Omarchy's
  *Move window* drag is still bound. Reinstall them and restart the canvas.
- **SUPER+left-drag does nothing on the laptop screen** during a canvas session: expected, the canvas
  takes the chord everywhere. SUPER+CTRL+Left/Right (Omarchy's group focus keys) are resize keys during
  the session too; both come back when the canvas stops.
- **Typing in Overview or SUPER+CTRL+G does nothing in the glasses**: the search field lives in the
  Studio plugin, so Studio must have been started once in this Quickshell session (it stays loaded
  when hidden). Check that `pose.sock.controls.prompt` in the runtime directory says `v1 <pid> <seq> 1 …`
  while the search is open; the field answers in `pose.sock.controls.search`. The field opens by itself
  only when you zoom out from a window; an Overview shown at start or after Esc closed a search has
  none (so it never grabs the keyboard unasked): press SUPER+CTRL+G.
- **ALT+TAB, SUPER+TAB or SUPER+arrows do Omarchy's thing during a canvas session**: the takeover
  switch is off (Canvas tab), or the controls predate M4: reinstall them.
- **SUPER+wheel switches workspaces during a canvas session**: the takeover switch is off (Canvas tab),
  or the controls are older than this release: reinstall the shortcuts (`make install-controls`, or
  Studio's setup). SUPER+CTRL+Page_Up/Page_Down scroll either way once the controls are current.
- **A window vanished above or below the view**: the cylinder has no top or bottom row. Scroll
  (SUPER+wheel, SUPER+CTRL+Page_Up/Page_Down, a vertical 4-finger pan), land on it with SUPER+Up/Down,
  or open the Overview, which shows the whole height (the radar strip too).

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
- **Switching modes stopped XR and started it again**: the renderer did not acknowledge the switch
  within 3 seconds, so Studio used the stop-first fallback (`backend.log` in the state directory says `live mode switch
  not acknowledged; restarting the XR view`). The `mode` field of `pose.sock.stats` shows which scene the
  renderer runs; `viewer.log` shows `Scene: switched to canvas` (or `monitors`) on success, and
  `Scene: switch to … refused: …` or `Window canvas unavailable: …` when the renderer could not build
  the new scene. The session continues in the new mode after the restart.
- **"Turn the laptop display back on before switching the render mode."**: a live switch needs a
  computer display to return the canvas windows to. Turn the laptop display on (Controls) and switch
  again, or stop XR first.
- **After a crash** (Studio or the renderer killed), the next Studio start finds
  `canvas-session.json`, puts the windows back, removes the canvas output and turns the canvas rules
  off. A journal from another Hyprland session is dropped, because its window addresses no longer
  exist. If windows still sit on `omxr-park`, run **Stop & close canvas** once, or move them with
  SUPER+SHIFT+number.
- **Studio still shows the old mode or no Canvas tab** after an update: reinstall Studio and
  rescan plugins (see the README), then reopen Studio.

## End-to-end manual test checklist

This is the consolidated manual test for a release. It collects the glasses checks of milestones
M2–M7 into one pass; the automated gates (`make check`, `check-san`, `check-notifications`,
`check-workspace-focus`, `check-environment`, `check-preview`, `smoke`, `smoke-canvas`, `check-ui`,
`check-lint`) run first. The runtime directory is `$XDG_RUNTIME_DIR/omarchy-xr/` (`pose.sock.stats`,
the `pose.sock.controls.*` mailboxes), the state directory `~/.local/state/omarchy-xr/`
(`layout.json`, `viewer.tsv`, `canvas.json`, `canvas-session.json`, `viewer.log`, `backend.log`).

Before starting, note `sha256sum ~/.local/state/omarchy-xr/{layout.json,viewer.tsv}` and
`hyprctl binds -j > /tmp/binds-before.json`.

### Presentation matrix

Run each row with three or more windows open (a browser, a terminal, a video) and one notification
(`notify-send test`). "Glasses flat" is **Utilities → Preview & cleanup → Open fullscreen mono**
(`--display` on the glasses), the spectator is the flat window of Studio's **Recording** switch
during stereo.

| Mode | Presentation | Expected |
|---|---|---|
| ☐ Virtual monitors | Direct stereo | Monitors in SBS stereo; the lease is held (no VITURE desktop output); notification cards beside the monitors; flick up dismisses, flick down cycles. |
| ☐ Virtual monitors | Glasses flat | Same monitors in mono; the notification card shows (mono HUD). |
| ☐ Virtual monitors | Windowed preview | Same in a window; card shows; **R**, **F**, wheel, **Esc** work. |
| ☐ Virtual monitors | Spectator | Mirrors the stereo view including the card; unchanged from 0.3.1. |
| ☐ Window canvas | Direct stereo | Windows on the cylinder in SBS stereo; SUPER+wheel and SUPER+CTRL+Page_Up/Down scroll it; the staged window live with menus and the native cursor; the card floats inside the ring in the lower part of the view, over the windows. |
| ☐ Window canvas | Glasses flat | Same cylinder in mono, scrolling the same; card inside the ring; XR cursor or native cursor on the staged window. |
| ☐ Window canvas | Windowed preview | Same in a window; windowed keys (`/ F O P Tab Alt+arrows PageUp PageDown F1 Esc`); Overview drag works, also up and down. |
| ☐ Window canvas | Spectator | Same picture as the glasses (overlays, cues, card, cursor). |

### Start, stop, migration and restore

- [ ] Window canvas → Start stereo: the `OMXR-…-canvas` output is created, every regular window moves to
  `omxr-park` (special workspaces and excluded classes stay), `canvas-session.json` lists their origins.
- [ ] Stop: every window returns to its origin workspace, tiled windows tiled again, floating ones at
  their size and position; the canvas output and its rules are gone; SUPER+F is Omarchy's fullscreen
  again (`hyprctl binds -j` equals `/tmp/binds-before.json` for the canvas chords).
- [ ] `hyprctl reload` mid-session: rules reinstalled, the canvas keeps working, SUPER+F still Fill.
- [ ] `kill -9` the backend during a canvas session, reopen Studio: windows restored, output and rules
  removed.
- [ ] After a canvas session `layout.json` and `viewer.tsv` hash as before, and monitor mode still
  starts.
- [ ] SUPER+3, SUPER+SHIFT+3 and the scratchpad never freeze the canvas; SUPER+SHIFT+number takes a
  window off the canvas with its borders and tiling back.

### Live switch

Run in direct stereo and once in the windowed preview.

- [ ] Monitors → Window canvas while in stereo: the glasses stay in SBS (no black flash to the desktop
  mode, no lease re-request; `viewer.log` shows `Scene: switched to canvas` and no second
  `OpenGL:` start line); windows migrate to `omxr-park`; the monitor outputs disappear only after
  `pose.sock.stats` reports `"mode":"canvas"`.
- [ ] Window canvas → Monitors while in stereo: windows return to their origin workspaces and tiling,
  the monitor layout comes back at its saved size, the canvas output and rules are removed, the canvas
  chords are Omarchy's again, SUPER+F is fullscreen again.
- [ ] No workspace other than `omxr-canvas`/`omxr-park` sits on the canvas output at any point
  (`hyprctl workspaces -j`), and no output overlaps another during the switch (`hyprctl monitors -j`).
- [ ] Monitors → canvas → monitors: `layout.json` and `viewer.tsv` hash as before.
- [ ] Studio shows "Switching the XR view…" then "Switched to …"; the footer and Canvas tab follow.
- [ ] Refused while the laptop display is off: Studio says "Turn the laptop display back on before
  switching the render mode." and nothing changes.
- [ ] Fallback once: `kill -STOP $(pgrep -x omarchy-xr)` right before switching. After about 3 s
  `backend.log` shows `live mode switch not acknowledged; restarting the XR view`, the frozen renderer
  is ended, and XR comes back in the new mode in the same presentation (stereo again for stereo).
  (A stale `controls.version` cannot force the renderer's own refusal: Studio reads the same file and
  refuses the canvas first.)

### Input

- [ ] Click and type into windows via `fit_target`, mouse crossing and the SDL click in the preview;
  stage + warp within 50 ms.
- [ ] Menus, tooltips and the native cursor on the staged window (`pose.sock.stats` `stage.shown`
  true); the spectator and preview show the XR cursor when the region is unavailable.
- [ ] 60 s real-mouse sweep with the laptop display off, across the right edge of the canvas output:
  focus never moves, a live sliver is never focused, the staged window never covers the strip.
- [ ] SUPER+F and browser F11 never make a window fullscreen (F11 fills instead).
- [ ] The three-finger double tap releases the pointer to the laptop screen centre.
- [ ] (M7) Canvas start: `viewer.log` shows `staged 0x…` with the first list; the keyboard stays where it
  was until a confirm.
- [ ] (M7) Ctrl+Down and a three-finger single tap on a gazed window: staged, raised, focused, pointer at
  the gaze point, typing arrives; `viewer.log` `Canvas: confirm 0x…`; the double tap still releases.
- [ ] (M7) The confirm hint shows under the label on the first dwells, is readable with a visible accent
  tint, and never shows after the first confirm.
- [ ] (M7) Stereo start with the laptop display off starts without the recording window and says so.

### Navigation

- [ ] SUPER+CTRL+G opens the search in stereo, typing in Overview searches too; the camera follows the
  best match and the palette never covers it; Enter lands, Shift+Enter summons, Esc clears then
  reverts; a window off the canvas is brought over.
- [ ] The Quickshell prompt holds the keyboard only while open; focus returns to the staged window.
- [ ] ALT+TAB: tap flips to the previous window, hold shows the list, release lands (1.5 s fallback).
- [ ] SUPER+TAB, SUPER+arrows, SUPER+SHIFT+arrows drive the canvas; with the takeover switch off they
  are Omarchy's while SUPER+F, SUPER+CTRL+G, SUPER+ALT+P stay canvas keys.
- [ ] Fill: ≈ 90 % of the view with native text; restore in the three cases (untouched, moved,
  resized).
- [ ] Ctrl+A arranges by kind without overlap; Ctrl+Z / Ctrl+Shift+Z undo and redo.
- [ ] SUPER+ALT+P pins body-locked and readable while turning; unpin puts it back.
- [ ] Radar strip and F1 help readable; overlays follow the head lazily (no jitter within 12°).
- [ ] A new window pulses briefly where it lands; one placed behind you shows the accent chevron at the
  view edge, which disappears when you look at it or after 3 s.
- [ ] In the windowed preview, a left-drag in Overview moves the window under the pointer in any
  direction in 20 px steps; the place survives a restart; Ctrl+Z undoes;
  a still click selects instead.
- [ ] (M7) SUPER+F with nothing staged fills the gazed window; SUPER+F again restores.
- [ ] (M7) SUPER+left-drag moves the staged window along the ring (20 px snap), Ctrl+Z undoes, the place
  survives a restart; the real window stays at the stage origin (`hyprctl clients -j`).
- [ ] (M7) Let go of SUPER before the mouse button: the drag ends on the button release (plain release bind).
- [ ] (M7) SUPER+right-drag from any corner resizes, the panel follows, the window is back at the origin
  within a second; SUPER+CTRL+arrows step 100 px.
- [ ] (M7) Closing the staged window stages and lands on the next most recent one.
- [ ] (M7) Stop: SUPER+mouse:272 is Omarchy's *Move window* again and SUPER+CTRL+Left/Right its group
  focus keys (`hyprctl binds -j`), SUPER+CTRL+Up/Down unbound.
- [ ] (M7) Studio's Input tab: the title "Fit selected monitor / focus window" fits its column.

### Capture rates

- [ ] `pose.sock.stats` in stereo with real head motion: focused 60, the others at the ladder rate,
  `usedMpix ≤ effectiveMpix`, `calibration` 1 in steady state.
- [ ] A 30 fps video in a near window plays smoothly with no dropped captures.
- [ ] One hour in canvas mode: the renderer's GPU memory stays flat.
- [ ] The omarchy-shell recording indicator stays steady while rates and places change.
- [ ] Studio's budget readout ("Using X of Y Mpix/s", self-limited, latency, slivers) updates.
- [ ] `make check-canvas-live` passes.

### Notifications

- [ ] Monitor mode: cards beside the workspace in view, in stereo, flat and preview.
- [ ] Canvas mode: cards inside the ring in the lower part of the view, over the windows, never behind
  a window or overhead, also after turning 180°.
- [ ] Flick up dismisses the gazed card (in XR and on the desktop), flick down cycles, in both modes.
- [ ] Glasses flat and windowed preview show the cards (mono HUD); `make check-preview` is
  pixel-identical.

### Environments, theme and tracking

- [ ] The environment from `environment.tsv` shows in both modes and survives a live switch.
- [ ] The theme accent colours halos, the radar, cues and cards after an Omarchy theme change.
- [ ] Prediction and recenter behave the same in both modes (recenter aims the camera straight ahead).

### Studio

- [ ] The mode selector works while stopped and while viewing; it is disabled with the v6 hint for
  older controls.
- [ ] The Canvas tab saves its settings (decimal fields, exclusions, takeover switch).
- [ ] The footer shows "Window canvas · N windows"; the budget readout updates.
- [ ] View controls: Overview, Land on window, Search, Fill, Arrange, Undo in canvas mode; the monitor
  controls in monitor mode.

### Recovery

- [ ] Direct-mode lease loss (unplug and replug the glasses) regenerates textures in both modes.
- [ ] `kill -9` the renderer: Studio cleans up (canvas windows restored, outputs removed) in both modes.

### M7 glasses PR checklist

Copied from the plan (§7 M7); the items above cover the same ground inside the full pass.

- [ ] Start stereo in canvas mode: `viewer.log` shows `staged 0x…` with the first list; the staged window shows menus and the native cursor; the keyboard stays where it was until a confirm.
- [ ] Ctrl+Down on a gazed window: staged, raised, focused, pointer at the gaze point, typing arrives; the same with a three-finger single tap; a double tap still recenters and releases the pointer.
- [ ] The hint appears under the halo on the first dwells and disappears after the first confirm.
- [ ] SUPER+F with nothing staged fills the gazed window; SUPER+F again restores.
- [ ] SUPER+left-drag moves the staged window along the ring (snapped), Ctrl+Z undoes, the place survives a restart; the real window stays at the stage origin (`hyprctl clients -j`).
- [ ] SUPER+right-drag from any corner resizes; the panel follows; the window is back at the origin within a second. SUPER+CTRL+arrows step 100 px.
- [ ] Close the staged window: the next MRU is staged and landed.
- [ ] Stop: SUPER+mouse:272 is Omarchy's *Move window* again (`hyprctl binds -j`), SUPER+CTRL+Up/Down unbound, SUPER+CTRL+Left/Right Omarchy's group focus keys again.
- [ ] Stereo start with the laptop display off: starts without the recording window; the message says so.
- [ ] A `{release=true}` bind on `SUPER + mouse:272` fires on real Hyprland (the drag ends), and `hl.unbind` removes the mouse bind.

### M8 glasses PR checklist

Copied from the plan (§7 M8).

- [ ] SUPER+wheel scrolls the cylinder up and down smoothly; windows stay upright, text at eye level stays sharp; the scroll stops half a view beyond the highest and lowest window.
- [ ] SUPER+CTRL+Page_Up/Page_Down scroll a page; a vertical 4-finger pan scrolls, a horizontal one still turns the view along the ring; 3-finger flicks and taps unchanged.
- [ ] A new window opens next to where you look, in x and y, without overlap; a dialog opens over its parent.
- [ ] SUPER+SHIFT+Up three times and SUPER+left-drag upward leave the window above the old rows; Ctrl+Z undoes; the place survives a restart.
- [ ] SUPER+Up/Down land on the windows above and below and bring them to eye level; confirm, search Enter and ALT+TAB do the same.
- [ ] Ctrl+A packs the windows into a block around the gaze; the Overview shows all of it; the radar shows the height and the view rectangle.
- [ ] Windows scrolled out of view above or below go idle (`Capture:` lines) and come back when scrolled in.
- [ ] Stop: `hyprctl binds -j` shows Omarchy's *Scroll active workspace forward/backward* on SUPER+mouse_down/up again and no SUPER+CTRL+Page binds.
- [ ] A `canvas-memory.tsv` from before M8 restores the layout unchanged.
