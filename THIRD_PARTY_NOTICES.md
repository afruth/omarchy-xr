# Third-party notices

Omarchy XR's distribution terms do not replace any third-party licence.

## Included glasses runtime

`usr/lib/omarchy-xr/sdk/libglasses.so`: VITURE XR Glasses SDK 2.4.0,
Linux x86_64. Copyright (C) 2025 VITURE Inc. All rights reserved.
Distributed only as part of this application under the
[VITURE SDK agreement](https://www.viture.com/viture-sdk-license-agreement)
(effective September 2025). The package preserves the unmodified runtime bytes.
The developer ZIP, headers, API documentation, samples, Carina/VIO libraries,
OpenCV libraries and SDK-bundled libusb shared library are not distributed.
This adapter supports Gen1/Gen2 devices, including Pro 2; it does not support
Carina 6DoF or cameras.

VITURE publishes an [SDK open-source notice](https://www.viture.com/viture-sdk-license-agreement-sider).
Its XR Glasses SDK inventory includes the following components. The inventory
is SDK-wide; it does not identify which are embedded in `libglasses.so`.

| Component | Copyright/attribution | Licence |
| --- | --- | --- |
| FlatBuffers | Copyright 2014 Google Inc. | Apache-2.0 |
| HIDAPI | Copyright (c) 2010 Signal 11 Software | BSD-3-Clause |
| yaml-cpp | Copyright (c) 2008-2015 Jesse Beder | MIT |
| Eigen | Copyright (c) The Eigen Authors | MPL-2.0 |
| libusb | Copyright 2001 Johannes Erdfelt; 2020-2023 libusb-cmake community | LGPL-2.1 |
| tiny-AES-c | kokke | Unlicense |
| libuvc | Copyright (C) 2010-2015 Ken Tossell | BSD-3-Clause |
| cpp-httplib | Copyright (c) 2017 yhirose | MIT |
| Mbed TLS | Copyright The Mbed TLS Contributors | Apache-2.0 OR GPL-2.0-or-later |

Applicable upstream licence texts are in `packaging/licenses/` in source and
`/usr/share/licenses/omarchy-xr-bin/` in packages. No SDK-internal component was
modified by this project. Source and relinking obligations, where applicable,
are additional to preserving licence texts; see the release review in
`docs/sdk-redistribution.md`. A URL or a licence copy is not a substitute for
corresponding source or relinkable objects.

## Wayland protocols

The renderer compiles code generated from the protocol definitions in
`protocols/`. Their copyright notices and permission terms are retained in
`WAYLAND-PROTOCOLS.txt`. They remain under their original permissive licences.
This includes `hyprland-toplevel-export-v1.xml` (Copyright 2022 Vaxry,
BSD-3-Clause) and `wlr-foreign-toplevel-management-unstable-v1.xml`
(Copyright 2018 Ilia Bozhinov), which the window canvas links only because the
first references it.

## Ported algorithms

The window canvas ports pure logic from
[phantomat](https://github.com/kaolti/phantomat) (`kaolti/phantomat`),
Copyright (c) 2023, Hypr Development and Copyright (c) 2026, Zsolt Kacso,
BSD-3-Clause: the canvas camera (zoom about a point, fit to bounds, pan steps),
auto-placement, arrange, the directional neighbour and nudge rules, and the
window memory format with its claim rules (`src/canvas_model.hpp`,
`src/canvas_placement.hpp`, `src/canvas_memory.hpp`). The code was adapted to
a periodic, cylindrical canvas; no phantomat rendering, input or compositor code
is included. The upstream licence text is in `PHANTOMAT-BSD.txt`.

## System dependencies

SDL, Mesa/OpenGL, Wayland, libdrm, GLib, Pango, Cairo, json-c, Python, polkit,
systemd/libudev, Quickshell and Hyprland are installed separately by the package
manager and remain under their respective licences. This application uses
Omarchy's installed QML components; it does not copy their implementation.

VITURE and Omarchy names identify compatibility, not endorsement.
