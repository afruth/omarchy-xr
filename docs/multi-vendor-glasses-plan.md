# Omarchy XR — RayNeo and XREAL glasses: research and plan

Status: **planning only** (2026-10-02). Nothing below is implemented. The maintainer has
no RayNeo or XREAL hardware, so every phase that touches a device is blocked on a test
unit or an outside tester (§8). The research in §2 reflects public sources as of this date.

## 1. Goal

Support RayNeo and XREAL glasses next to VITURE with the same feature set: head tracking,
recenter, stereo side-by-side (SBS) video and the dedicated DRM-leased output. Keep one
renderer and one Studio; the vendor differences stay inside the glasses backend.

Non-goals: 6DoF/SLAM (Air 2 Ultra cameras, XREAL Eye), on-glasses buttons beyond what is
needed for display mode, firmware updates, RayNeo X-series standalone glasses.

## 2. Research findings

### 2.1 Official SDKs

| Vendor | Official SDK | Linux host support | Usable here |
|---|---|---|---|
| VITURE | VITURE XR Glasses SDK 2.4.0 (C, `libglasses.so`) | Yes | Yes, bundled (`docs/sdk-redistribution.md`) |
| RayNeo | RayNeo ARDK (OpenXR Unity, MIT; native Android) | No — targets the standalone X-series glasses, not the tethered Air series | No |
| XREAL | XREAL SDK 3.x (Unity XR plugin, Android) | No | No |

RayNeo also has `libRayNeoXRMiniSDK.so`, a closed x86_64/aarch64 library that
[XRLinuxDriver](https://github.com/wheaney/XRLinuxDriver) ships under an "official
collaboration". It exports `RegisterIMUEventCallback`, `StartXR`, `OpenIMU`, `Recenter`,
`GetHeadTrackerPose` (quaternion + position), `SwitchTo3D` / `SwitchTo2D` and
`GetSideBySideStatus`. No public licence or redistribution terms exist. It also statically
links libusb and re-exports ~100 `libusb_*` symbols; XRLinuxDriver documents that loading it
into the same process as VITURE's libraries makes those bind to the wrong libusb and crash.

XREAL has never cooperated with Linux driver authors; all Linux support is reverse-engineered.

### 2.2 Open-source drivers

| Library | Licence | Devices | Transport | Provides |
|---|---|---|---|---|
| [nrealAirLinuxDriver](https://gitlab.com/wheaney/nrealAirLinuxDriver) (`interface_lib`) | MIT | XREAL Air, Air 2, Air 2 Pro, Air 2 Ultra, XBX A01/A01+ (VID `3318`) | hidapi (BSD) | Raw IMU samples + device calibration JSON; MCU display mode incl. `3840x1080` SBS at 60/72/90 Hz |
| [xreal_one_driver](https://github.com/rohitsangwan01/xreal_one_driver) | MIT | XREAL One, One Pro (and likely 1S) | TCP to `169.254.2.1:52998` over the glasses' USB network interface | IMU stream; C bindings over a Rust core |
| [RayNeo-Air-3S-Pro-OpenVR](https://github.com/verncat/RayNeo-Air-3S-Pro-OpenVR) | MIT | RayNeo Air 3S Pro, Air 4 Pro (VID `1bbb`, PID `af50`), GT (`3941:af50`) | libusb (LGPL, already in `packaging/licenses`) | Raw IMU (accel/gyro/mag/temp/tick), `Rayneo_DisplaySet3D` / `Set2D`, device info, attach/detach |

Not usable: wheaney's `xrealAirDeviceKit` and `xrealOneDeviceKit` have no licence file
(all rights reserved). XRLinuxDriver itself is GPL-3.0 and would conflict with this
project's reserved-rights terms; it is a reference for behaviour only, not a code source.

### 2.3 Consequences

- Both vendors are reachable with MIT code. Nothing has to be redistributed under vendor
  terms, unlike VITURE.
- Unlike VITURE's SDK, every open driver yields **raw IMU only**. We need our own 3DoF
  fusion (gyro integration with accelerometer tilt correction; magnetometer yaw correction
  where available), plus bias and calibration handling.
- XREAL One/One Pro run their own on-glasses 3DoF ("anchor"/"follow" modes on the X1 chip).
  The glasses must be in plain screen-follows-head mode or the two stabilisations fight.
  Whether this can be set over the protocol is unknown (§9).

## 3. Where VITURE is assumed today

| Place | Assumption |
|---|---|
| `studio/sdk.py` `SDK.devices()` | Matches only USB vendor `35ca`; one pair of VITURE glasses |
| `studio/sdk.py` `SDK.connect()` | Requires `libglasses.so` and accepted VITURE terms before starting the worker |
| `studio/sdk_worker.py` | `xr_device_provider_*` calls, Carina polling, VITURE mode journal and display-mode verification |
| `studio/dedicated_helper.py` | Privileged EDID handoff refuses any display whose EDID name lacks `VITURE` |
| `studio/backend.py` | User-facing "Connect one pair of VITURE glasses" errors; waits for a `3840x1080` mode after requesting stereo |
| `packaging/70-omarchy-xr.rules` | `uaccess` only for vendor `35ca` |
| Studio UI / README | VITURE naming in setup, status and terms |

What is already vendor-neutral: the renderer consumes `euler-nwu-v2 <mono> <roll> <pitch>
<yaw> <device-ts>` packets on the pose socket (`src/pose_socket.hpp`, `src/tracking.hpp`)
and does not know which glasses produced them. Stereo, DRM lease and canvas code only see
a 3840×1080 output.

## 4. Decisions

| # | Decision |
|---|---|
| V1 | One **worker process per vendor**, all speaking the existing worker protocol (stdin commands, state file, pose socket). Separate processes keep each vendor's libusb/hidapi copy isolated, which also sidesteps the RayNeo closed-library symbol clash. |
| V2 | **Open-source drivers first** for both vendors (§2.2). The closed RayNeo library is optional and only pursued if RayNeo grants redistribution terms in writing (§7). |
| V3 | Vendor C libraries are built from pinned commits into small shared objects under `/usr/lib/omarchy-xr/drivers/` and loaded with `ctypes`, as `libglasses.so` is today. No Rust toolchain at runtime; the XREAL One driver is built at package time. |
| V4 | **One shared fusion module** for all raw-IMU backends, emitting the same `euler-nwu-v2` packets as VITURE. VITURE keeps its SDK fusion. |
| V5 | Device detection selects the backend by USB VID/PID from a single table. Still exactly one pair of glasses at a time. |
| V6 | The VITURE terms gate applies only to the VITURE backend. RayNeo/XREAL need no extra terms beyond the MIT notices in `THIRD_PARTY_NOTICES.md`. |
| V7 | The EDID handoff allowlist becomes a per-vendor list of exact EDID monitor names, captured from real devices. No wildcard matching. |
| V8 | New vendors ship as **experimental** until tested on hardware; Studio labels them so. |

## 5. Design

### 5.1 Backend interface (Python, `studio/`)

`SDK` becomes a facade over a `GlassesBackend` chosen at connect time:

```text
detect()        -> [(vendor, vid, pid, model)]   # sysfs scan, one VID/PID table
worker_argv()   -> argv for the vendor worker
requirements()  -> missing-runtime / terms errors for Studio (VITURE only today)
capabilities    -> {stereo_modes, refresh_rates, recenter, native_fusion, edid_names}
```

The worker side keeps today's command set (`stereo on|off`, `restore`, `restore-rate`,
`verify-*`, `recenter`) so `backend.py` does not branch per vendor. Error strings move to
the backend so they can say "RayNeo" or "XREAL" instead of "VITURE".

### 5.2 Fusion (`studio/fusion.py` or C++ in the worker)

- Input: timestamped gyro (rad/s), accel (m/s²), optional magnetometer, in the device frame.
- Per-vendor axis remap into NWU, from calibration data where the device provides it
  (XREAL Air calibration JSON) or a fixed table.
- Complementary or Madgwick-style filter; startup gyro-bias estimate while still; slow
  accelerometer tilt correction; yaw is free-running (recenter resets it) unless a
  magnetometer proves stable enough.
- Output at the IMU rate, throttled to what the pose socket needs.
- Evaluate x-io Technologies' Fusion (MIT) before writing our own.
- Testable offline: recorded IMU traces under `tests/` with known still/rotate segments.

Python may be too slow for 500–1000 Hz IMU streams with per-sample filtering; if the
prototype shows that, the fusion moves into a small C++ worker that shares
`src/tracking.hpp` maths.

### 5.3 XREAL Air family

Worker opens the IMU and MCU HID interfaces via `interface_lib`, loads calibration,
streams samples into fusion. Stereo: switch MCU display mode to the SBS mode matching the
current rate (60 → `3840x1080_60_SBS`, 72 → 72, 90 → 90, 120 → 90), then the existing
`backend.py` wait for a `3840x1080` host mode applies unchanged. Restore maps back.

### 5.4 XREAL One / One Pro

Worker reads the IMU over TCP. Needs the glasses' USB network interface configured
(link-local `169.254.2.0/24`); check whether NetworkManager does this unprompted on Omarchy.
SBS switching and the on-glasses stabilisation mode are open questions (§9); until
answered, the backend reports `stereo_modes: []` and Studio offers tracking-only preview.

### 5.5 RayNeo Air family

Worker uses the MIT RayNeo library: `Rayneo_Start`, `Rayneo_EnableImu`, IMU callback into
fusion, `Rayneo_DisplaySet3D` / `Set2D` for stereo. Air 3S Pro and Air 4 Pro share
`1bbb:af50` and cannot be told apart by USB ID; use device info if it distinguishes them,
otherwise show "RayNeo Air 3S Pro / Air 4 Pro". Older Air 2/2s/NXTWEAR models are unknown
to this library; they need testing or the closed library.

### 5.6 Packaging and permissions

- `packaging/70-omarchy-xr.rules`: add `uaccess` rules for `3318` (usb + hidraw),
  `1bbb` and `3941` (usb). Exact interface classes confirmed on hardware.
- `PKGBUILD.in`: build the three drivers from pinned commits and record SHA-256s like the
  VITURE runtime.
- `THIRD_PARTY_NOTICES.md`: add the MIT notices (and hidapi/x-io if used).
- `dedicated_helper.py` and its polkit policy: per-vendor EDID name allowlist (V7).

## 6. Work packages

| WP | Scope | Hardware needed |
|---|---|---|
| W1 | Backend facade, VID/PID table, VITURE moved behind it with no behaviour change; vendor-neutral messages | No — VITURE regression test only |
| W2 | Fusion module with recorded-trace tests (synthetic traces first, real ones later) | No |
| W3 | Build integration for the three MIT drivers, udev rules, notices; drivers compiled but not yet wired | No |
| W4 | XREAL Air worker: IMU + fusion + SBS switching | XREAL Air / Air 2 / Air 2 Pro |
| W5 | RayNeo worker: IMU + fusion + SBS switching | RayNeo Air 3S Pro or Air 4 Pro |
| W6 | XREAL One worker: IMU + fusion; SBS if possible | XREAL One or One Pro |
| W7 | EDID allowlist + dedicated output per vendor | Each device |
| W8 | Studio: vendor name, experimental label, capability-driven controls; README and distribution docs | Partly |

W1–W3 can be done now and are worth doing even without new hardware: W1 cleans up the
VITURE coupling and W2 is independently testable. W4–W7 wait for hardware.

## 7. Optional: RayNeo closed library

Only if the open library falls short (older models, recenter quality, SBS reliability).
Send RayNeo a request similar to the VITURE one in `docs/sdk-redistribution.md`: permission
to bundle `libRayNeoXRMiniSDK.so` unchanged inside the application, its licence and
third-party inventory (it embeds libusb, LGPL), and any telemetry behaviour. If granted,
it runs in its own worker process per V1 and is loaded `RTLD_LOCAL | RTLD_DEEPBIND`.

## 8. Hardware plan

Without devices, the realistic options are: buy one of each vendor's most common current
model (RayNeo Air 3S Pro, XREAL One or Air 2), borrow, or recruit testers through the
marketplace listing with a debug build that records IMU traces and EDIDs. A tester build
needs only W1–W3 plus a `omarchy-xr-probe` command that dumps USB IDs, EDID monitor names,
a 30-second IMU trace and display-mode switch results to a file the tester sends back.

## 9. Open questions

1. EDID monitor names for each model (needed for V7).
2. XREAL One/One Pro: is SBS 3840×1080 available to a Linux host, and how is it switched?
   Can the on-glasses 3DoF stabilisation be disabled over the protocol, or only from the
   glasses' own menu?
3. RayNeo: which refresh rates does SBS support, and does `Rayneo_DisplaySet3D` change the
   EDID the host sees (as VITURE does) or keep the mode list?
4. Is a magnetometer-based yaw correction usable on RayNeo, or does it drift near laptops?
5. IMU rates and timestamp units per device, and whether Python fusion keeps up (§5.2).
6. Does the open RayNeo library cover Air 2/2s, or only the 3S Pro generation?

## Sources

- [wheaney/XRLinuxDriver](https://github.com/wheaney/XRLinuxDriver) — device table, RayNeo closed-library header, XREAL SBS mode mapping (GPL-3.0, reference only)
- [RayNeo SDK overview (Extentos)](https://extentos.com/docs/ecosystem/platforms/rayneo)
- [XREAL SDK 3.0.0 release note](https://docs.xreal.com/Release%20Note/XREAL%20SDK%203.0.0), [XREAL Developer](https://developer.xreal.com/)
- [nrealAirLinuxDriver](https://gitlab.com/wheaney/nrealAirLinuxDriver)
- [rohitsangwan01/xreal_one_driver](https://github.com/rohitsangwan01/xreal_one_driver)
- [verncat/RayNeo-Air-3S-Pro-OpenVR](https://github.com/verncat/RayNeo-Air-3S-Pro-OpenVR)
