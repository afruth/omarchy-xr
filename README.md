# Omarchy XR

An experimental spatial desktop for Omarchy / Hyprland and VITURE XR glasses.

**Current state:** a runnable C++20 / SDL2 / OpenGL development preview with three
synthetic panels in a 3D arc. Mouse movement simulates head orientation. It does
not yet capture desktops, read the VITURE SDK, route input to applications, or
provide stereo output. No Joy-Con integration is planned for the initial version.

## Local development

On Omarchy / Arch, dependencies are `gcc`, `make`, `pkgconf`, `sdl2-compat`, and
`libglvnd`, with a working OpenGL driver. Install missing packages with:

```sh
omarchy pkg add gcc make pkgconf sdl2-compat libglvnd
```

On Ubuntu: `g++ make pkg-config libsdl2-dev libgl1-mesa-dev`.

```sh
make              # debug build with warnings
make run          # launch the preview
make check        # CLI checks (no display required)
make smoke        # render ten frames and exit; requires a graphical session
```

Right-drag to look around, **R** to recenter, **Esc** to exit. The preview runs on
your normal monitor and does not change Hyprland configuration. VS Code build,
run, and check tasks are included. Generated files stay in `build/`.

The first renderer intentionally uses OpenGL 2.1 compatibility functionality to
keep the initial build small. GPU texture import and stereo rendering will need
a modern shader-based rendering path.

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
