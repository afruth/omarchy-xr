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
XR controls v7 replaced the canvas's own keys, and its takeovers of Omarchy chords, with one XR key
layer shared by both modes (see **Keys**). Controls v10 let XR search bring a window from outside XR
to the virtual monitor you look at. Controls v9 enforce desktop/Canvas separation and explicit
window transfers. Controls v8 added the performance card, the zoom levels and
XR+F (codes 29–32).

## Requirements

- **XR controls v10.** Window canvas relies on the controls adapter (`xr-controls.lua`) to list
  windows, stage them, bind the XR keys and keep windows out of fullscreen. After updating, reinstall
  the controls once: **Utilities → Setup & integrations**, then **Set up everything** or the shortcuts
  & gestures button, or `make install-controls` in a source checkout. Until then Studio says "Window
  canvas needs XR controls v10" and the **Window canvas** option is disabled; the renderer itself
  refuses `--canvas` below v9, the last version the canvas scene depends on.
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
   or the canvas output and its rules). The XR keys stay bound; the canvas-only ones (nudge, resize,
   pin, arrange, undo, scroll) are bound only in the canvas and pass through to applications in
   virtual monitors mode.

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
open or move to the canvas later are still adopted. Windows that were on the glasses join the canvas
when stereo starts either way: the glasses stop being a desktop display, and Hyprland would otherwise
hide them on a laptop workspace.

**While it runs**, windows opened on the laptop stay on the laptop, even when another window from
the same browser process is on the canvas. Windows opened or explicitly moved onto the canvas join
it. To transfer a laptop window, select it in XR search and press Enter. Ordinary focus commands
cannot transfer windows. A small window opened on the canvas, such as a dialog, is staged in front
of its parent. Canvas windows have no borders,
rounding, shadows, blur, dimming or animations, so the captured image is just the window.

**At stop** (**Stop stereo**, **Close preview** or **Stop & close canvas**) each window goes back to
its original workspace. A tiled window is tiled again; a floating one gets back its size and
position. A window you moved off the canvas yourself stays where you put it; if it was tiled
before, it is tiled again. Windows that had no recorded origin go to the laptop's active workspace.
Then the canvas output and its rules are removed, and the XR keys are unbound.

## New windows

A new window opens next to where you look, sideways and up or down, without overlap. A window that
opens while the canvas runs is landed on at once: the camera brings it to the middle of your view at its
working zoom, and it is staged and gets the keyboard, so a window you open is never lost beside or behind
you (not while the search is open; several at once: the last one). It also gets a short halo pulse
(300 ms). If the camera cannot follow, an accent-coloured chevron at the edge of the view points towards
it for up to 3 seconds, or until you turn and its centre is in view. The windows moved to the canvas at
start, and a canvas you switch to, are not landed on and show neither.

## Moving windows

**XR+left-drag** moves the window you work in, in any direction, snapped to 20 px: it follows the
pointer's travel (also past the edge of the window), and the new place is remembered for the next
start. Windows never overlap: the ones in the way slide aside as you drag, pushing their own
neighbours on in turn, and flow back to where they were once the dragged window has passed. What is
still pushed aside when you let go stays there, and **XR+Z** puts everything back in one step. Letting
go of the XR modifier before the button also drops it (the plain button release still ends the drag),
and a drag that holds still for 3 s ends there. Only the panel moves: the real window stays where the
canvas keeps it, so the pointer, menus and the native cursor stay right. When the window leaves your
view, the camera follows it after the drop.

**XR+right-drag** resizes the window you work in, from any corner; the panel follows. Half a second
after you let go, the window is put back at the stage origin and trimmed to the canvas output, so a
top-left corner drag cannot leave it misplaced. **XR+comma** / **XR+period** make it narrower or wider
and **XR+Shift+comma** / **XR+Shift+period** shorter or taller, in 100 px steps. A window that grows
pushes its neighbours aside the same way; they do not come back when it shrinks. **XR+Return** (Fill)
pushes them aside too, and restoring the window lets them flow back, unless you moved it into their
old places in between. **XR+Shift+arrows** nudges it by 100 px, **Shift+Enter** in the search summons
a window next to you, and **XR+A** arranges the windows (see **Keys**).

The windowed preview has no mouse drag of its own; the XR keys and XR+drag work there as in the
glasses, because they are Hyprland bindings. The spectator has no mouse drag either.

## Notifications

Notifications work in both modes. In canvas mode the cards float inside the ring, never behind a
window, in the lower part of your view, and are drawn over the windows. **XR+N** dismisses the card
you look at (with none gazed, the front card) and **XR+Shift+N** cycles through the stack, exactly as
in monitor mode. With a multitouch touchpad, a three-finger flick up on a card dismisses it and a flick
down cycles.

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

The canvas uses the XR key layer, the same keys as virtual monitors mode. **XR** is a held modifier,
**CTRL+ALT** by default. The layer is bound only while XR runs, and nothing of Omarchy's is taken over:
SUPER+TAB, SUPER+F, ALT+TAB, SUPER+arrows and SUPER+wheel keep Omarchy's meaning during a canvas
session. **XR+H** shows the keys in the glasses (and in the preview); the help is generated from the
keys actually bound, so it always matches your settings. These are the defaults:

| Keys | What they do |
|---|---|
| **XR+Space** | Recenter. |
| **XR+G**, held | Grab: the scene follows your head while you hold it and stays where it is when you let go, so looking at a window, holding XR+G and turning back to straight ahead brings that window to the front. Head pitch also scrolls the cylinder. The grab ends on release, on a second press, on any other XR action, when tracking goes stale or after 30 s. |
| **XR+=** / **XR+-** | Zoom in / out one step; repeats while held. |
| **XR+/** | Search your windows by title, class or kind (browser, terminal, editor, …). After you zoom out to Overview (XR+Down or a flick out) you can also just start typing: the search field opens with it. |
| type, **↑/↓**, **Tab/Shift+Tab** | Filter and move the selection; the camera follows the best match and the other windows dim. |
| **Enter** / **Shift+Enter** | Land on the selection / summon it next to you first. A window that is not on the canvas yet is marked *bring to canvas* and is brought over. |
| **Ctrl+1…8** | Land on that row of the results. |
| **Esc** | Clears the text; a second Esc closes the search and puts the camera and focus back where they were. An open help closes first. |
| **XR+Up** | Zoom in a level: from the Overview to the window you look at in a monitor-sized frame, then to that window filling the view. Zooming in focuses it: it is staged, raised, gets the keyboard, and the pointer goes to the point you look at. |
| **XR+Down** | Zoom out a level: window → monitor frame → Overview (a filled window is restored first). |
| **XR+F** (hold) | Move the window you look at with your head: it stays in front of you while the cylinder stays put, the other windows make room as it passes, and on release it snaps into a free place. XR+Z undoes the whole move. |
| **Three-finger tap** | Focus the window you look at without changing the zoom. |
| **XR+Return** | Fill: the window you work in grows to about 90 % of your view with sharp native text. Press again to restore: untouched, it gets its old size and place back; moved, it keeps the new place with the old size; resized in between, it fills again (and a later restore still returns to the size from before Fill). A flick in fills too, a flick out restores. With no window on the stage, XR+Return focuses the window you look at first and then fills it. |
| **XR+Left** / **XR+Right** | Land on the previous / next window in ring order, wrapping from the last to the first; every landing brings its window to eye level. Windows above or below are reached by scrolling or the search. |
| **XR+Shift+wheel** | Scroll the cylinder up or down, a fifth of the view per notch. |
| **XR+Page_Up / Page_Down** | Scroll the cylinder a page up or down. |
| **XR+wheel** | Zoom in / out. |
| **XR+middle button**, held | Grab, as XR+G. |
| **4-finger pan** | Sideways turns the view along the ring; up and down scrolls the cylinder (multitouch touchpads only). |
| **XR+Shift+arrows** | Nudge the window you work in by 100 px, also up and down; the windows in the way slide aside. |
| **XR+left-drag** | Move the window you work in, in any direction (20 px steps, **XR+Z** undoes it, the place is remembered). |
| **XR+right-drag** | Resize it; the panel follows. When the drag ends the window is put back at the stage origin. |
| **XR+comma** / **XR+period** | Resize it by 100 px: narrower / wider. With **Shift**: shorter / taller. |
| **XR+A**, **XR+Z**, **XR+Shift+Z** | Arrange: group windows by kind, then application, into a block around where you look, without overlap, and show the Overview; undo and redo arrange, nudge, move and summon. They work from any view; **Ctrl+A**, **Ctrl+Z** and **Ctrl+Shift+Z** still work in the search field. Studio's **Arrange** and **Undo** work too. |
| **XR+P** | Pin the window you work in to your view (body-locked), or unpin it. |
| **XR+N** / **XR+Shift+N** | Dismiss the notification card you look at (else the front card) / show the next one. |
| **XR+Home** | Move the pointer back to the centre of your laptop screen, for example to use Studio. |
| **XR+H** | This help, from any view. **F1** still opens it in the search field. |

Every key is editable in Studio under **Controls → XR keys**, the key map: pick another modifier at
the top, or click a row and press a key. Conflicts with another XR key, an Omarchy binding (named) or a
reserved chord (such as CTRL+ALT+F1…F12) are shown inline on the row, and **Save** stays disabled while
there are any. The canvas-only keys (scroll, nudge, resize, pin, arrange, undo and redo) carry a
"canvas" tag and are bound only while the canvas runs. The mouse actions are fixed but follow the
modifier. A compositor fullscreen (Omarchy's SUPER+F, or F11 in a browser) is reverted at once in the
canvas and fills the window instead (see **The pointer and other keys**).

The **radar strip** under the view in Overview and search shows the whole ring around you and the
height above and below eye level: every window as a mark, the window you work in in the accent colour,
and the rectangle you are looking at.

Studio's **View controls** show **Overview**, **Land on window**, **Search**, **Fill**, **Arrange**
and **Undo** in canvas mode.

**The windowed preview** is presentational: it has no keys of its own and takes no clicks, mouse
drags, mouse look, pan or zoom; only **Esc** closes it. The XR layer drives it through Hyprland,
exactly as it drives the glasses. Search typing (in the preview and in the glasses) goes to a small
search field that Studio keeps loaded on the canvas output; it holds the keyboard only while the
search is open.

## Gaze, dwell and focus

Looking at a window, or just beside it, marks it as the **candidate** at once: a thin accent rim, so you
always see what you are looking at. Keep looking and it is **selected**: after a short dwell (500 ms of steady gaze) it gets the halo, and
the search, Fill and nudges act on it. Looking never moves the keyboard or the pointer by itself, so a
glance at another window does not steal your typing. To work in the window you look at, **confirm**:
zoom in with **XR+Up** (or a flick in), or, with a multitouch touchpad, tap it once with three fingers. The window is
staged, raised and gets the keyboard, and the pointer goes to the point you are looking at.

The window receiving keyboard input has a **Keyboard input** label, including when pinned.
The gaze rim and selection halo remain distinct from this input indicator. A contextual hint
under the selected window names the configured confirmation key whenever it differs from
the input window. **Canvas settings → Show hints for working in another window** turns it off.

**Require confirmation before pointer changes windows** prevents movement beyond the current
window from transferring input focus. Explicit confirmation still works. **Bring new windows
into view automatically** can be disabled to keep the camera and input on your current work;
new windows still announce themselves with the pulse/edge cue. Likely parent dialogs still
come into view. The parent check uses app process and floating status, since the window protocol
does not supply a parent identity.

The Canvas tab's **Your windows** map and title picker expose Focus, Bring here, Pin/Unpin,
and Return to desktop without needing to find a window in the glasses first. The map represents
the full 360° cylinder; its edges join and window proportions are preserved. The title picker
provides keyboard access when a window is too small on the map.

## The pointer and other keys

- **A window is always staged** while the canvas has windows: at start, and when the staged window
  closes or leaves the canvas, the most recently used one is staged and the camera lands on it. This
  staging is quiet: it never takes the keyboard or moves the pointer. Only a confirm, a click or the
  pointer crossing into a window does.
- **Fullscreen** never sticks in the canvas. **SUPER+F** stays Omarchy's fullscreen key, but a
  window made fullscreen by the compositor, with SUPER+F or by itself (a browser after F11), is put
  back at once and filled instead, because a fullscreen window would cover the canvas output. Fill
  itself is **XR+Return**; with no window on the stage, it confirms the window you look at first and
  fills it once it is staged.
- SUPER+number, SUPER+SHIFT+number and the scratchpad work as usual. A foreign workspace or the
  scratchpad that lands on the canvas output is sent to the laptop so the canvas never freezes.
  SUPER+SHIFT+number takes a window off the canvas and gives it back its borders and its tiled
  state, or its floating size and position; Stop does the same for every window.
- **The pointer** is held inside the window you are working in. Its movement past the edge
  continues over the canvas, and crossing into a neighbouring window brings that window to the
  front and puts the pointer on it. In the glasses the pointer you see is the real one, drawn into
  the window image with its menus. When that image is unavailable, the renderer draws an arrow
  instead. The spectator and the windowed preview show the same.
- **XR+Home** moves the pointer back to the centre of your laptop screen (the first display that is
  not an XR output), for example to use Studio.
- **Three-finger taps** (multitouch touchpads only; Studio shows the gesture settings only when it
  found one): a single tap confirms 0.4 s after it (when no second tap follows); a **double tap**
  recenters and moves the pointer back to the laptop screen, like XR+Home. A swipe cancels a pending
  tap.
- The windowed preview and the spectator take no clicks. Studio's **Land on window** confirms like
  zooming in with XR+Up.

## Troubleshooting

- **I can look at windows but not type or click**: looking only selects; zoom in with XR+Up
  (Ctrl+Alt+Up by default) or tap once with three fingers. After it, `pose.sock.controls.hover` in the
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
- **XR keys do nothing**: the layer is bound only while XR runs, and it needs XR controls v10
  (see Requirements). Check **Controls → XR keys** in Studio for conflicts: a chord that collides with
  an Omarchy binding or another XR key is shown there, and the XR+H help lists only the keys actually
  bound. An application that uses the same chord (for example CTRL+ALT+arrows) loses it while XR runs;
  pick another modifier there if that gets in the way.
- **SUPER+left-drag moves the real window, not the panel**: SUPER+left-drag is Omarchy's *Move
  window* drag again. To move a window along the ring use XR+left-drag.
- **Typing in Overview or XR+/ does nothing in the glasses**: the search field lives in the
  Studio plugin, so Studio must have been started once in this Quickshell session (it stays loaded
  when hidden). Check that `pose.sock.controls.prompt` in the runtime directory says `v1 <pid> <seq> 1 …`
  while the search is open; the field answers in `pose.sock.controls.search`. The field opens by itself
  only when you zoom out from a window; an Overview shown at start or after Esc closed a search has
  none (so it never grabs the keyboard unasked): press XR+/.
- **ALT+TAB, SUPER+TAB, SUPER+arrows or SUPER+wheel do Omarchy's thing during a canvas session**:
  expected, the canvas no longer takes them over. Use XR+Left/Right or the search to change windows,
  XR+Down for the Overview and XR+Shift+wheel or XR+Page_Up/Page_Down to scroll.
- **The view keeps turning with my head**: a grab is running (XR+G or the XR+middle button). It ends
  on release, on a second press, on any other XR action or after 30 s.
- **A window vanished above or below the view**: the cylinder has no top or bottom row, and
  XR+Left/Right only walk the ring. Scroll (XR+Shift+wheel, XR+Page_Up/Page_Down, a vertical
  4-finger pan, or head pitch during a grab), find it with XR+/, or open the Overview, which shows the
  whole height (the radar strip too).

- **A thin strip of windows at the right edge of the canvas output** (8 px wide, visible on the
  canvas output or in a screenshot of it): these are live slivers, windows the canvas captures above
  30 Hz. This is expected; they ignore the pointer and go back to the hidden workspace when their
  rate drops. `pose.sock.stats` lists them with `"place":"sliver"`.
- **Some thumbnails freeze in Overview or with many windows in view**: the canvas is far over its
  capture budget and keeps the last picture of the smallest windows rather than slowing everything
  down. The budget line in Studio's Canvas tab shows the use. Raise the capture budget (the
  development machine handled up to about 350 Mpix/s) or close or exclude windows you do not need;
  zooming in on a window also brings its neighbours back to life.
- **"Window canvas needs XR controls v10"**: reinstall the controls (see Requirements). A controls
  file edited by hand, or an older one restored by a sync tool, shows the same hint.
- **A window stays fullscreen during a canvas session** (after SUPER+F or F11): the controls are not
  active for this session. Check that the session was started from Studio and that
  `pose.sock.controls.mode` in the runtime directory (`$XDG_RUNTIME_DIR/omarchy-xr/`, next to
  `pose.sock.stats`) says `canvas`.
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
| ☐ Virtual monitors | Direct stereo | Monitors in SBS stereo; the lease is held (no VITURE desktop output); notification cards beside the monitors; XR+N dismisses, XR+Shift+N cycles (with a touchpad, flick up and down too). |
| ☐ Virtual monitors | Glasses flat | Same monitors in mono; the notification card shows (mono HUD). |
| ☐ Virtual monitors | Windowed preview | Same in a window; card shows; the XR keys drive it; clicks, drags and the wheel do nothing in it; **Esc** closes it. |
| ☐ Virtual monitors | Spectator | Mirrors the stereo view including the card; unchanged from 0.3.1. |
| ☐ Window canvas | Direct stereo | Windows on the cylinder in SBS stereo; XR+Shift+wheel and XR+Page_Up/Page_Down scroll it; the staged window live with menus and the native cursor; the card floats inside the ring in the lower part of the view, over the windows. |
| ☐ Window canvas | Glasses flat | Same cylinder in mono, scrolling the same; card inside the ring; XR cursor or native cursor on the staged window. |
| ☐ Window canvas | Windowed preview | Same in a window; the XR keys drive it as in the glasses (XR+/, XR+Up, XR+Return, XR+Left/Right, scrolling); no keys, clicks or mouse drag of its own; **Esc** closes it. |
| ☐ Window canvas | Spectator | Same picture as the glasses (overlays, cues, card, cursor). |

### Start, stop, migration and restore

- [ ] Window canvas → Start stereo: the `OMXR-…-canvas` output is created, every regular window moves to
  `omxr-park` (special workspaces and excluded classes stay), `canvas-session.json` lists their origins.
- [ ] Stop: every window returns to its origin workspace, tiled windows tiled again, floating ones at
  their size and position; the canvas output and its rules are gone; the XR layer is unbound
  (`hyprctl binds -j` equals `/tmp/binds-before.json`), and the XR chords reach applications again
  within 250 ms.
- [ ] No Omarchy chord changes while XR runs: during a canvas session and a monitor session, `hyprctl
  binds -j` holds `/tmp/binds-before.json` unchanged plus only the `XR: …` bindings; SUPER+TAB,
  SUPER+F, ALT+TAB, SUPER+arrows and SUPER+wheel do what they do without XR.
- [ ] `hyprctl reload` mid-session: rules and the XR layer reinstalled, the canvas keeps working,
  XR+Return still Fill.
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
  the monitor layout comes back at its saved size, the canvas output and rules are removed, the
  canvas-only XR keys (nudge, resize, pin, arrange, undo, scroll) pass through to applications again.
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

- [ ] Click and type into windows via XR+Down and mouse crossing; stage + warp within 50 ms. A click
  in the windowed preview does nothing.
- [ ] Menus, tooltips and the native cursor on the staged window (`pose.sock.stats` `stage.shown`
  true); the spectator and preview show the XR cursor when the region is unavailable.
- [ ] 60 s real-mouse sweep with the laptop display off, across the right edge of the canvas output:
  focus never moves, a live sliver is never focused, the staged window never covers the strip.
- [ ] SUPER+F and browser F11 never leave a window fullscreen in the canvas (both fill instead).
- [ ] XR+Home, and with a touchpad the three-finger double tap, release the pointer to the laptop
  screen centre.
- [ ] (M7) Canvas start: `viewer.log` shows `staged 0x…` with the first list; the keyboard stays where it
  was until a confirm.
- [ ] (M7) XR+Down and (with a touchpad) a three-finger single tap on a gazed window: staged, raised,
  focused, pointer at the gaze point, typing arrives; `viewer.log` `Canvas: confirm 0x…`; the double
  tap still releases.
- [ ] (M7) The confirm hint shows under the label on the first dwells with the configured chord
  ("Ctrl+Alt+Down or a three-finger tap to focus"), is readable with a visible accent tint, and never
  shows after the first confirm.
- [ ] (M7) Stereo start with the laptop display off starts without the recording window and says so.

### Navigation

- [ ] XR+/ opens the search in stereo, typing in Overview searches too; the camera follows the
  best match and the palette never covers it; Enter lands, Shift+Enter summons, Esc clears then
  reverts; a window off the canvas is brought over.
- [ ] The Quickshell prompt holds the keyboard only while open; focus returns to the staged window.
- [ ] XR+Left/Right land on the previous/next window in ring order and wrap from the last to the
  first; held, they repeat.
- [ ] XR+Up toggles the Overview; XR+Shift+arrows nudge (repeating while held); XR+Space recenters;
  XR+= / XR+- and XR+wheel zoom, the keys repeating while held.
- [ ] Grab: hold XR+G while looking at a window and turn the head; the window stays fixed in view, head
  pitch scrolls the cylinder, and after release the window stays in front. The same with the XR+middle
  button. Letting go of the modifier first still ends it (by the next press, any other XR action or
  within 30 s); `viewer.log` logs every start and end.
- [ ] Fill (XR+Return): ≈ 90 % of the view with native text; restore in the three cases (untouched,
  moved, resized).
- [ ] XR+A arranges by kind without overlap; XR+Z / XR+Shift+Z undo and redo from any view; Ctrl+A,
  Ctrl+Z, Ctrl+Shift+Z still work in the search field.
- [ ] XR+P pins body-locked and readable while turning; unpin puts it back.
- [ ] Radar strip readable; XR+H shows the help in both scenes (and F1 in the search field), listing
  exactly the keys bound, also after an edit in Studio; overlays follow the head lazily (no jitter
  within 12°).
- [ ] A new window pulses briefly where it lands; one placed behind you shows the accent chevron at the
  view edge, which disappears when you look at it or after 3 s.
- [ ] In the windowed preview, a mouse drag, a click and the wheel do nothing; XR+left-drag moves the
  staged window as in the glasses.
- [ ] (M7) XR+Return with nothing staged fills the gazed window; XR+Return again restores.
- [ ] (M7) XR+left-drag moves the staged window along the ring (20 px snap), XR+Z undoes, the place
  survives a restart; the real window stays at the stage origin (`hyprctl clients -j`).
- [ ] (M7) Let go of the XR modifier before the mouse button: the drag ends on the button release
  (plain release bind); a drag held still for 3 s ends there.
- [ ] (M7) XR+right-drag from any corner resizes, the panel follows, the window is back at the origin
  within a second; XR+comma / XR+period and XR+Shift+comma / XR+Shift+period step 100 px.
- [ ] (M7) Closing the staged window stages and lands on the next most recent one.
- [ ] (M7) Stop: SUPER+mouse:272 is still Omarchy's *Move window* and SUPER+CTRL+arrows its own keys
  (`hyprctl binds -j`); no `XR: …` binding is left.

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
- [ ] XR+N dismisses the gazed card, or the front card when none is gazed; XR+Shift+N cycles; in both
  modes.
- [ ] With a touchpad: flick up dismisses the gazed card (in XR and on the desktop), flick down cycles,
  in both modes.
- [ ] Glasses flat and windowed preview show the cards (mono HUD); `make check-preview` is
  pixel-identical.

### Environments, theme and tracking

- [ ] The environment from `environment.tsv` shows in both modes and survives a live switch.
- [ ] The theme accent colours halos, the radar, cues and cards after an Omarchy theme change.
- [ ] Prediction and recenter behave the same in both modes (recenter aims the camera straight ahead).

### Studio

- [ ] The mode selector works while stopped and while viewing; it is disabled with "Window canvas
  needs XR controls v10" for older controls.
- [ ] The Canvas tab saves its settings (decimal fields, exclusions); it has no takeover switch.
- [ ] **Controls → XR keys**: changing the modifier re-renders every chord and re-binds live after
  Save (`hyprctl binds -j`, the XR+H help); capture, clear and reset a row; a chord that collides
  with an Omarchy binding or another XR key shows the conflict inline, names the binding, and Save
  stays disabled.
- [ ] The gesture settings show only on a machine with a multitouch touchpad; otherwise one line says
  gestures need one.
- [ ] The footer shows "Window canvas · N windows"; the budget readout updates.
- [ ] View controls: Overview, Land on window, Search, Fill, Arrange, Undo in canvas mode; the monitor
  controls in monitor mode.

### Recovery

- [ ] Direct-mode lease loss (unplug and replug the glasses) regenerates textures in both modes.
- [ ] `kill -9` the renderer: Studio cleans up (canvas windows restored, outputs removed) in both modes.

### M7 glasses PR checklist

*These PR checklists record the v6 keys that were tested at the time; with the XR layer, SUPER+F is
XR+Return, SUPER+left/right-drag is XR+left/right-drag, SUPER+CTRL+arrows are XR+comma/period (Shift
for height), SUPER+wheel is XR+Shift+wheel, SUPER+arrows are XR+Left/Right and Ctrl+Down is XR+Down.*

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

### M9 glasses PR checklist

- [ ] SUPER+left-drag a window into its neighbour: the neighbour slides aside (and pushes the next one on), then flows back as the dragged window passes; after the drop no two windows overlap; Ctrl+Z restores all of them.
- [ ] SUPER+SHIFT+Right into a neighbour pushes it right; SUPER+SHIFT+Down into one below pushes it down.
- [ ] SUPER+right-drag and SUPER+CTRL+Right/Down grow the window into its neighbours: they move aside while it grows; the pushed places survive a restart.
- [ ] SUPER+F pushes the neighbours out of the filled window's way; SUPER+F again brings them back where they were.
- [ ] A pinned window neither pushes nor moves.
