# Omarchy XR

An experimental spatial desktop for Omarchy / Hyprland and VITURE XR glasses.

**Current state:** a C++20 / SDL2 / OpenGL application with three panels in a 3D
arc. The center panel can display a live Wayland output; the side panels remain
synthetic. Mouse movement simulates head orientation. VITURE tracking, input
routing, multiple simultaneous captures, and stereo output are not implemented.
No Joy-Con integration is planned for the initial version.

## Local development

On Omarchy / Arch, dependencies are `gcc`, `make`, `pkgconf`, `sdl2-compat`, and
`libglvnd`, and `wayland`, with a working OpenGL driver. Install missing packages with:

```sh
omarchy pkg add gcc make pkgconf sdl2-compat libglvnd wayland
```

On Ubuntu: `g++ make pkg-config libsdl2-dev libgl1-mesa-dev libwayland-dev libwayland-bin`.

```sh
make              # debug build with warnings
make run          # launch the preview
make check        # pixel-conversion and CLI checks (no display required)
make smoke        # render ten frames and exit; requires a graphical session
```

Right-drag to look around, **R** to recenter, **Esc** to exit. The preview runs on
your normal monitor and does not change Hyprland configuration. VS Code build,
run, and check tasks are included. Generated files stay in `build/`.

The first renderer intentionally uses OpenGL 2.1 compatibility functionality to
keep the initial build small. GPU texture import and stereo rendering will need
a modern shader-based rendering path.

## Live capture

Run inside the Hyprland session:

```sh
./build/omarchy-xr --list-outputs
./build/omarchy-xr --capture XR-1
```

Replace `XR-1` with an output listed by the first command. To create an independent
virtual monitor without editing your saved configuration:

```sh
hyprctl output create headless XR-1
hyprctl monitors all
./build/omarchy-xr --capture XR-1
```

`hyprctl monitors all` shows the new monitor's active workspace. To put an app on
that workspace, use the current Lua-based Hyprland syntax below, replacing `1`
with the actual workspace number:

```sh
hyprctl eval 'hl.exec_cmd("foot", { workspace = "1 silent" })'
```

Keep the viewer on a **different output** from the captured desktop. Capturing
the viewer's own output causes a recursive mirror; output selection is explicit
and automatic feedback prevention is not yet implemented. The virtual desktop
may initially show only wallpaper until an application is opened on it.

After closing the viewer, remove the monitor you created:

```sh
hyprctl output remove XR-1
```

The viewer does not create, delete, or rearrange outputs itself. Manage any open
applications before removing a monitor. It captures the cursor and preserves
source aspect ratio. Clicking the panel does **not** yet interact with the source.
Capture uses `wlr-screencopy` version 1 and shared-memory buffers, with a target
maximum of about 30 captures/second. It is not zero-copy and is not a general
GNOME/KDE capture backend. A selected output disconnect or capture failure ends
the viewer with an explanatory error. There is no automatic reconnection yet.

To verify actual capture, not just rendering:

```sh
./build/omarchy-xr --capture XR-1 --smoke-test
```

This requires ten captured frames and checks OpenGL errors, with a ten-second
overall deadline. The separate capture connection keeps frame waiting out of the
render loop. `make check` tests padded rows, channel order, inverted frames, and
opaque alpha. CI currently covers those tests and synthetic rendering; live
Hyprland capture is a local integration test.

The vendored protocol XML comes from
[wlr-protocols](https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-screencopy-unstable-v1.xml)
and retains its embedded MIT notice. `wayland-scanner` generates bindings locally.

## Target experience

Three live virtual monitors arranged around the user, with rotational head
tracking, keyboard/mouse interaction, recentering, and saved panel placement.
Pro 2 offers 3DoF tracking; physical head translation is not tracked.

See [architecture and milestones](docs/architecture.md).

## Hardware and SDK

The glasses need a functioning DisplayPort-over-USB-C connection for video. USB
device detection alone is insufficient. SDK development and the mock preview can
be investigated separately from video output.

Obtain the current Linux SDK from [VITURE's developer portal](https://www.viture.com/developer).
SDK binaries are not included. Review vendor licensing before redistribution;
keep local SDK files under ignored `vendor/`. SDK loading and device permissions
will be implemented in the tracking milestone.

## License

MIT for original project code. Third-party dependencies and the VITURE SDK retain
their respective licenses. This project is not affiliated with VITURE or Omarchy.
