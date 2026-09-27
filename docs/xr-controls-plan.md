# Omarchy XR — Universal XR controls: design and implementation plan

Status: **implemented** (2026-09-27, controls v7), work packages K1–K8 below. It supersedes the
per-mode key sets and takeovers of controls v6. Where the implementation settled a detail differently
from the first draft, this document describes what was built.

## 1. Goal

One set of controls that:

- works the same in **both scene modes** (virtual monitors and Window canvas), with each action
  meaning the closest equivalent in each mode;
- works with **keyboard only**, and with **keyboard plus mouse**, without relying on multitouch
  touchpad gestures (on some machines, such as the GPD Pocket 4, the touchpad reaches Linux as a
  relative mouse and gestures never fire);
- lives on **one dedicated modifier layer**, so no Omarchy binding is taken over;
- can be **viewed and edited in one place** in Studio, and shown in the headset.

Non-goals: new touchpad gestures (the existing gestures stay as an optional extra where the hardware
supports them, see §7); interaction in the windowed preview (it is presentational, see §6).

## 2. Decisions

| # | Decision |
|---|---|
| D1 | The XR layer is a held modifier. The default is **`CTRL + ALT`**; the modifier is configurable in Studio. |
| D2 | Every key in the layer is editable in Studio; one screen shows and edits all of them. |
| D3 | All takeovers are dropped: `SUPER+Tab`, `SUPER+F`, `ALT+Tab` / `ALT+SHIFT+Tab`, `SUPER+arrows`, `SUPER+SHIFT+arrows`, `SUPER+CTRL+arrows`, `SUPER+wheel`, `SUPER+CTRL+G`, `SUPER+ALT+P`, `SUPER+CTRL+Page_Up/Down` and the old `CTRL+Up` / `CTRL+Down` defaults. Omarchy's bindings stay untouched while XR runs. |
| D4 | "Zoom in on what you look at" targets the **gazed window** in both modes. |
| D5 | "Fill" fills the view with the **selected window** in both modes. |
| D6 | "Previous / next" **cycles through windows** in both modes; the camera follows. |
| D7 | No separate "turn" action; head tracking turns the view and previous/next moves between windows. Scrolling the canvas up and down stays. |
| D8 | **Search works in virtual monitors mode** too, in this project. |
| D9 | The windowed preview gets no interactions of its own. The XR layer is global (Hyprland), so it drives the preview anyway. |
| D10 | Mouse actions are fixed (not editable) but follow the configured modifier. |
| D11 | **Grab** (hold to rotate the cylinder): while the key is held, the scene is locked to your head, so whatever you look at moves with you. On release the scene stays in its new place. It works like a recenter onto any window you carry to a comfortable position. |

The `CTRL + ALT` layer has two Omarchy bindings today (`CTRL+ALT+Delete`, `CTRL+ALT+Tab`); the
defaults below avoid both, and avoid `CTRL+ALT+F1…F12` (virtual-terminal switching). The layer is
only bound while the viewer runs, so applications get these chords back when XR stops. Known
trade-off: some applications use `CTRL+ALT+arrows` (for example VS Code's add-cursor-above/below);
while XR runs the layer wins. A different modifier (D1) avoids this.

## 3. The action set

`XR` is the configured modifier. Keys are Hyprland key names; a `SHIFT +` prefix is part of the key.

### 3.1 Keyboard

| Action id | Default | Virtual monitors | Window canvas | Before controls v7 |
|---|---|---|---|---|
| **View** |||||
| `recenter` | XR + `space` | Recenter | Recenter | mode 3 in both |
| `grab` | XR + `G`, **held** | Scene follows your head while held, stays on release (§5.7) | same; head pitch scrolls the canvas | new |
| `zoom_in` | XR + `equal` (repeats) | Zoom one step in | same | mode 4, unbound by default |
| `zoom_out` | XR + `minus` (repeats) | Zoom one step out | same | mode 5, unbound by default |
| `overview` | XR + `Up` | Fit all monitors | Overview on/off | mode 1 / mode 8 |
| `focus` | XR + `Down` | Fit the gazed **window** and focus it | Focus (confirm) the gazed window | monitors fit only the monitor (mode 2); canvas mode 18 |
| `fill` | XR + `Return` | Fill the view with the selected window (§5.3) | Fill / restore | canvas mode 10 only |
| **Windows** |||||
| `previous` / `next` | XR + `Left` / `Right` | Previous / next window across the XR monitors, camera follows (§5.4) | Previous / next window along the ring, wrapping | canvas neighbour (mode 14) only |
| `scroll_up` / `scroll_down` | XR + `Page_Up` / `Page_Down` | — (no vertical extent) | Scroll the canvas a page | canvas mode 19 |
| `search` | XR + `slash` | Search all windows; landing switches the XR monitor to the window's workspace and fits it (§5.5) | Search | canvas mode 9 only |
| **Canvas arrangement** |||||
| `nudge_left/right/up/down` | XR + `SHIFT + Left/Right/Up/Down` | — | Nudge 100 px | canvas mode 15 |
| `narrower` / `wider` | XR + `comma` / `period` | — | Resize 100 px | canvas resize keys |
| `shorter` / `taller` | XR + `SHIFT + comma` / `SHIFT + period` | — | Resize 100 px | canvas resize keys |
| `pin` | XR + `P` | — | Pin / unpin | canvas mode 16 |
| `arrange` | XR + `A` | — | Arrange | mode 13, Studio or search field only |
| `undo` / `redo` | XR + `Z` / XR + `SHIFT + Z` | — | Undo / redo | search field or Studio only |
| **Notifications and system** |||||
| `notification_dismiss` | XR + `N` | Dismiss the gazed card, else the front card | same | gesture only (mode 6) |
| `notification_next` | XR + `SHIFT + N` | Cycle the stack | same | gesture only (mode 7) |
| `pointer_home` | XR + `Home` | Pointer to the laptop screen's centre | same | double-tap gesture only |
| `help` | XR + `H` | Help overlay listing the current keys | same | canvas F1 in the search field only |

"—" actions are not bound in virtual monitors mode (the key passes through to applications).

### 3.2 Mouse (hold XR)

| Input | Action | Modes |
|---|---|---|
| wheel | zoom in / out | both |
| `SHIFT` + wheel | scroll the canvas | canvas |
| left-drag | move the window you work in | canvas (today's `SUPER+left-drag`) |
| right-drag | resize it | canvas (today's `SUPER+right-drag`) |
| middle button, held | `grab` (§5.7) | both |

## 4. Settings model

### 4.1 Profile (`$XDG_STATE_HOME/omarchy-xr/controls-settings.json`, version 2)

```json
{
  "version": 2,
  "modifier": "CTRL + ALT",
  "fingers": 3,
  "keys": {"recenter": "space", "zoom_in": "equal", "nudge_left": "SHIFT + Left", "…": "…"}
}
```

- `modifier`: two or more of `SHIFT`, `CTRL`, `ALT`, `SUPER`, and at least one of `CTRL`, `ALT` or
  `SUPER`.
- `keys`: every action id from §3.1. The value is a key name, optionally with `SHIFT +`; an empty string
  disables the action.
- Validation (`studio/input_settings.py`, rewritten):
  - Each effective chord (`modifier + key`) is unique.
  - It must not collide with any non-XR Hyprland binding (the existing `hyprctl binds -j` check).
  - It must not collide with `CTRL+ALT+F1…F12`.
  - Errors name the action and the colliding binding.
- The allowed key names expand from today's list to: letters, digits, arrows, `Home`, `End`,
  `Page_Up`, `Page_Down`, `Return`, `space`, `Tab`, `BackSpace`, `Insert`, `Delete`, `minus`,
  `equal`, `comma`, `period`, `slash`, `semicolon`, `apostrophe`, `bracketleft`, `bracketright`,
  `backslash` and `grave`.
- Migration: a version-1 profile (`fingers` plus five chords) loads as the version-2 defaults, keeping
  `fingers`. The old chords are dropped (they were `CTRL+arrow` takeovers of application keys), and
  the release notes say so.

### 4.2 Lua mailbox (`controls-settings.tsv`, version 2)

```
v2
modifier<TAB>CTRL + ALT
fingers<TAB>3
key<TAB>recenter<TAB>space
key<TAB>nudge_left<TAB>SHIFT + Left
…
```

The Lua side keeps its rule of never executing settings. It validates each line against a fixed
action table and a character whitelist, and it ignores unknown action ids so older files stay readable.

### 4.3 Effective-keys mailbox (`pose.sock.keys`, new)

Lua writes the modifier (a `modifier<TAB>…` row) and the bindings it actually installed (action id,
chord, and whether the action is live in the current mode). The renderer's help overlay (`help`) and Studio's key reference both read it.
This guarantees the displayed keys match the real ones even if an edit was refused.

## 5. Behaviour details

### 5.1 Shared action codes

The `.controls` codes (`publish(0, mode, token)`) stay the transport. Every action publishes **one code in
both scenes**, and the renderer's `sceneKey(mode, token)` gives it the scene's meaning: the canvas keeps
`canvasKey`, the monitor scene has `monitorKey`. Notification keys and grab are scene-independent.

| Code | Action | Token | Virtual monitors | Window canvas |
|---|---|---|---|---|
| 3, 4, 5 | recenter, zoom in, zoom out | — | as before | as before |
| 8 | `overview` | — | fit every monitor | Overview on/off |
| 9 | `search` | — | search (§5.5) | search |
| 10 | `fill` | — | frame the gazed monitor (Lua maximized the window, §5.3) | Fill / restore |
| 13, 15, 16, 19 | arrange, nudge, pin, scroll | direction / scroll token | — | as before |
| 17 | `help` | — | help card | help card |
| 18 | `focus` | — | frame the gazed window (§5.3) | confirm |
| 20 / 21 | `notification_dismiss` / `notification_next` | gazed card or `-` (front card) | same | same |
| 22 | `previous` / `next` | `prev` / `next` | frame the pane Lua published (§5.4) | ring order, wrapping |
| 26 | `grab` | `begin` / `end` | §5.7 | §5.7 |
| 27 / 28 | `undo` / `redo` | — | — | undo / redo |

`pointer_home` stays in Lua (`releasePointer()`, now for any XR monitor). In virtual monitors mode Lua
runs `fill`, `previous`/`next` and `search` itself first (maximize, focus and pane, window list) and then
publishes the code. The renderer and Studio require controls v7 for the canvas.

### 5.2 Window awareness in virtual monitors mode (new)

Monitors mode knows monitors, not windows, and the controls adapter already maps a look pixel to the
window under it (the dwell's pointer warp) and publishes a window's rectangle on its monitor (`.pane`,
for the flick-in pane fit). The XR layer builds on both instead of a new client mailbox: the renderer
asks for the gazed window with a new pointer serial at the current look pixel, the adapter focuses the
window there and republishes `.pane`, and the camera frames that pane. For search, the adapter
publishes every regular window in `.windows` (the canvas row format with the 4-field header, no canvas
output).

### 5.3 `focus` and `fill` in monitors mode

- `focus`: the camera fits the gazed window's rect on its monitor (the pane fit). A pane that already
  holds the look point is framed at once; otherwise the renderer waits up to 0.5 s for the adapter's
  new pane and then falls back to the monitor fit (no window there, or a presentation without gaze
  pointer control, where the adapter does not act on pointer serials).
- `fill`: Lua makes the selected window fill its monitor with Hyprland fullscreen mode 1
  ("maximize": the bar stays, the other windows stay visible elsewhere), and the camera fits that
  monitor. Pressing it again restores the window. Unlike the canvas, fullscreen is harmless here,
  because every monitor is its own output.

### 5.4 `previous` / `next`

- Canvas: the ring order. Reuse `Verb::Neighbour` left/right, but wrap from the last window to the
  first.
- Monitors: the XR monitors from left to right in layout order, then each monitor's visible windows
  from left to right and top to bottom, wrapping. Each step focuses the window and runs `focus` on
  it, so the camera follows.
- Only windows on visible workspaces take part. Search (§5.5) reaches the others.
- The adapter focuses the window, points at its centre and publishes its pane with the code; the
  renderer frames that pane (or, after 0.5 s without a new one, the last pane).

### 5.5 Search in monitors mode

- Reuses the canvas search engine (`src/canvas_search.hpp`: fuzzy title/class/category with
  recency ranking) and the Quickshell search field (`studio/SearchPrompt.qml` /
  `SearchPromptWindow.qml`, `.search` mailbox) unchanged.
- The candidates are every regular window, including other workspaces, from the adapter's `.windows`
  (refreshed every second while XR runs, and at once when the search key is pressed).
- The monitor scene has no palette of its own: the renderer ranks the windows (most recently focused
  first for an empty query) and writes the top eight rows to `.results`, which the prompt lists under
  its field. The prompt sits on the gazed XR monitor, so the list shows in the glasses. The camera does
  not move while searching.
- `Enter` (or Ctrl+1…8) lands through `.land`: the adapter focuses the window, which brings its
  workspace up on its monitor, points at it and publishes its pane; the camera frames that pane.
  `Esc` closes the search; nothing moved, so nothing is restored.

### 5.6 Held keys

`zoom_in` and `zoom_out` bind with `repeating=true` (Hyprland honours the keyboard repeat rate).
Nudge and resize repeat too. The canvas resize-staging logic already coalesces steps.

### 5.7 `grab`: hold to rotate the cylinder

Hold the key and look at something: from then on it stays in the same place in your view, whatever
you do with your head. Turn your head and the whole scene turns with you. Release the key and the
scene stays in its new position. A typical use is looking at a window, holding `grab`, and turning
back to straight ahead, which brings that window to the front. This is a targeted recenter that
needs no gaze selection.

- **Horizontal (both modes):** Recenter already works by setting the camera's heading reference
  (`tracking::Camera::neutralYaw`, `src/tracking.hpp`) to the current head yaw. While the grab is
  held, the renderer does this on every tracking sample, keeping the offset from grab start:
  `neutralYaw = neutralAtStart + (yaw − yawAtStart)`. The view's heading relative to the head stays
  constant, so the scene rotates with the head. On release it stops updating, and the new
  `neutralYaw` is kept. Gravity and pitch are untouched, as with recenter.
- **Vertical (canvas):** head pitch during the grab scrolls the cylinder by the matching arc
  (`canvas->scrollBy`), so a window can also be carried up or down to eye level. The canvas's scroll
  limits still apply.
- **Vertical (monitors):** none. The monitor arc keeps its height, as with recenter.
- **Input:** Lua binds the chord twice, as a press (`begin`) and as a `release=true` bind (`end`),
  without `repeating`. These are the same mechanics as today's Alt-release of the canvas switcher.
- **Safety:** if the release is missed (for example, the modifier was let go first and Hyprland did
  not report the key release), the grab still ends when:
  - the same chord is pressed again,
  - any other XR action runs,
  - tracking goes stale,
  - the viewer stops,
  - or 30 seconds pass.
  The renderer logs every grab start and end.
- While grabbing, gaze dwell, the halo and camera animations (fit, landing, recenter) are
  suspended, so the scene doesn't also move by itself. A pending fit is dropped.
- The mouse equivalent is holding the middle button with XR held (§3.2).

## 6. Windowed preview

The SDL key and mouse handlers in `src/main.cpp` (`keyDown`, `canvasKeyDown`, the wheel, rotate,
pan, click and drag handlers) are removed. `Esc` and closing the window still end the preview.
Everything else comes from the XR layer through Hyprland. The windowed preview's help text and the
`window-canvas.md` preview rows are updated.

## 7. Touchpad gestures

The existing gestures stay available on machines where libinput reports a touchpad: three-finger
swipe zoom and flicks, three-finger taps, four-finger pan and notification flicks. They are not part
of the shared set and are not extended. Studio's **Gestures** section is shown only when
`xr-touchpads.lua` lists a device. Otherwise one line says gestures need a multitouch touchpad and
that every action has a key.

## 8. Studio: the key map

It replaces **Shortcuts & gestures** on the Controls tab. It's one screen, readable at a glance:

```
 XR key layer                                   [ CTRL + ALT  ▾ ]   ⚠ 1 conflict
 ─────────────────────────────────────────────────────────────────────────────
 View                                    Windows
   Recenter              CTRL+ALT+Space    Previous window     CTRL+ALT+←
   Grab (hold)           CTRL+ALT+G
   Zoom in               CTRL+ALT+=        Next window         CTRL+ALT+→
   Zoom out              CTRL+ALT+-        Scroll up (canvas)  CTRL+ALT+PgUp
   Overview / fit all    CTRL+ALT+↑        …
   Focus gazed window    CTRL+ALT+↓      Notifications & system
   Fill                  CTRL+ALT+Enter    Dismiss             CTRL+ALT+N
 Canvas arrangement                         …
   Nudge left            CTRL+ALT+Shift+←
   …                                     Mouse (hold CTRL+ALT)
                                           Wheel zoom · Shift+wheel scroll ·
                                           drag move · right-drag resize ·
                                           middle-hold grab
 [ Save ]  [ Reset all ]  [ Print cheat sheet ]
```

- **Modifier picker** at the top. Changing it re-renders every chord and re-runs the conflict check
  at once.
- **Every row is a key-capture button.** Click it (or Enter/Space when it has focus) and press a key.
  The row records the key, including Shift, and shows it in the button. Esc cancels, Backspace clears
  (disabled). Each row has a small reset-to-default.
- **Conflicts show inline** on the row: another XR action, an Omarchy binding (named, from `hyprctl
  binds`), or a reserved chord. The header counts them. Save is disabled while any exist.
- **Canvas-only rows** carry a small "canvas" tag. Rows are grouped exactly as in §3.1, in two columns
  on wide windows and one on narrow ones.
- The whole screen is keyboard operable (Tab order follows the rows, arrow keys move between
  rows), with accessible names that include the current chord.
- **Save** writes the profile and the TSV and asks Lua to refresh, the existing `save_controls`
  path with its rollback.
- **Print cheat sheet** opens the same table as plain text in a terminal pager. The in-headset `help`
  shows the same table from `pose.sock.keys`.
- The Controls tab also gets a compact read-only **Keys** card with the six View actions and a
  "Show all keys" link.

## 9. Work packages

| WP | Scope | Main files | Tests |
|---|---|---|---|
| K1 | Settings v2: schema, validation, conflict report, migration, allowed keys | `studio/input_settings.py`, `studio/backend.py` (`save_controls`, a new `controls_conflicts` query) | `tests/test_input_settings.py` (new): uniqueness, Omarchy collisions, VT keys, migration, round trip |
| K2 | Lua XR layer: bind the table from the TSV, `repeating`, the press/release `grab` pair, the mouse binds, drop every takeover and `SUPER+F`, `pointer_home`, write `.keys`, `CONTROLS_VERSION = 7` | `config/xr-controls.lua` | `tests/controls.lua`: every action publishes its code in both modes, no takeover remains, rebinding on a settings change, the expired-handle rule still holds |
| K3 | Renderer: `sceneKey` dispatch, codes 20–26, keyboard notification actions, canvas previous/next wrapping, `grab` (heading follow, canvas pitch scroll, the safety ends) | `src/main.cpp`, `src/live_controls.hpp`, `src/notification_hud.hpp`, `src/tracking.hpp` | unit tests for the dispatch, the notification target fallback, and a `grab` test (a yaw sweep keeps the view offset, release keeps the new heading, stale tracking and timeout end it) |
| K4 | Monitors-mode windows: gaze-to-window through a pointer serial and `.pane`, `focus` pane fit, `fill` (maximize), previous/next order | `config/xr-controls.lua`, `src/live_controls.hpp`, `src/main.cpp` | `tests/controls.lua` monitor windows, `tests/workspace_focus.cpp` layer keys |
| K5 | Monitors-mode search: `.windows` in monitors mode, ranking in the renderer, `.results` listed by the prompt, landing through `.land` | `config/xr-controls.lua`, `src/live_controls.hpp`, `src/main.cpp`, `studio/SearchPrompt.qml`, `studio/SearchPromptWindow.qml` | `tests/workspace_focus.cpp` search, `tests/qml/tst_search_prompt.qml`, `tests/controls.lua` |
| K6 | Studio key map (§8), the Keys card, gestures shown only with a touchpad, requiring controls v7 | `studio/MonitorStudio.qml`, new `studio/KeyMap.qml`, `studio/backend.py`, `studio/canvas.py` | `tests/qml/tst_key_map.qml`: capture, clear, conflict display, modifier change, keyboard navigation |
| K7 | Help overlay from `.keys` in both modes; remove the preview interactions (§6) | `src/help_keys.hpp`, `src/canvas_overlay.hpp`, `src/canvas_scene.hpp`, `src/main.cpp` | `tests/window_list.cpp` layer keys, `tests/canvas_preview.cpp` help card |
| K8 | Docs and release | `README.md`, `docs/window-canvas.md`, `docs/architecture.md`, release notes | — |

Order: K1 → K2 → K3 give a working keyboard layer for today's actions in both modes. K4 and K5 add
the monitors-mode window features. K6 can start in parallel with K2 against the K1 schema. K7 and K8
close the work.

Compatibility: the canvas takeover setting (`takeoverKeys` in `canvas.tsv` field 9 and the `.mode`
flag) is removed from Studio. Field 9 is still written as `0` so older Lua reads the header, and Lua
v7 ignores it. Studio requires controls v7 for both modes and offers **Update XR controls** below
that, through the existing setup action.

## 10. Manual acceptance checklist

- [ ] Every §3.1 action works from the keyboard in virtual monitors mode (or passes through where it
      shows "—") and in canvas mode, in direct stereo and glasses flat.
- [ ] No Omarchy binding changes while XR runs. `SUPER+Tab`, `SUPER+F`, `ALT+Tab`, `SUPER+arrows`
      and `SUPER+wheel` behave as without XR.
- [ ] XR-layer chords reach applications again within 250 ms of the viewer stopping.
- [ ] `zoom_in` / `zoom_out` / nudge / resize repeat while held.
- [ ] §3.2 mouse actions work while XR is held.
- [ ] `grab`: the looked-at window stays fixed in view while turning the head. After release it stays
      in front, in both modes and in canvas also vertically. Letting go of the modifier first still
      ends the grab (by the next press or within 30 s).
- [ ] Changing the modifier in Studio re-binds live. A conflicting choice is refused with the named
      binding.
- [ ] The key map: capture, clear, reset, conflict display, keyboard-only operation, narrow layout.
- [ ] `help` shows the effective keys in both modes, including after an edit.
- [ ] Monitors mode: `focus` fits the gazed window, `fill` fills and restores it, previous/next walks
      the visible windows across monitors, and search lands on a window on another workspace.
- [ ] On the GPD Pocket 4 (no libinput touchpad), everything above works and Studio shows the
      gestures note.
