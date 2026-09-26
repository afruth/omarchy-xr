"""Offline checks of the live canvas harness's verdict helpers (tests/live_canvas.py); no Hyprland needed."""
import pathlib
import tempfile
import time
import unittest

import live_canvas as lc

EDGE = lc.sc.OUT_X + lc.sc.OUT_W - lc.sc.SLIVER


def capture(address, tier, rate, place, **kw):
    """A pose.sock.stats capture row; kw: fps (default the rate), size (1920x1080), frames (False: none yet)."""
    (w, h), frames, fps = kw.get("size", (1920, 1080)), kw.get("frames", True), kw.get("fps")
    return {"output": address, "tier": tier, "rateHz": rate, "inFlight": 2 if rate > 30 else 1, "place": place,
            "fps": rate if fps is None else fps, "visible": True, "width": w if frames else 0,
            "height": h if frames else 0, "nativeWidth": w if frames else 0, "nativeHeight": h if frames else 0}


def report(captures, used, effective=300.0, clients=None):
    return {"canvasState": "work", "captures": captures, "_clients": clients or {},
            "budget": {"usedMpix": used, "effectiveMpix": effective, "panic": False}}


class ModelTests(unittest.TestCase):
    def test_two_windows(self):
        # ladder-2: the other window runs 40 Hz as a live sliver on the canvas workspace.
        r = report([capture("0xa", "focused", 60, "stage"), capture("0xb", "near", 40, "sliver")], 207.4,
                   clients={0xb: ("spikecanvas", EDGE)})
        self.assertEqual(lc.work_failures(r, True, {}), [])
        r["_clients"][0xb] = ("spikepark", 20000)
        self.assertEqual(len(lc.work_failures(r, True, {})), 1)

    def test_rate_and_fps(self):
        r = report([capture("0xa", "focused", 60, "stage", fps=57), capture("0xb", "near", 30, "park", fps=28)], 186.6)
        failures = lc.work_failures(r, True, {})
        self.assertEqual(len(failures), 3)   # 30 Hz is not the ladder's 40; focused under 58; 28 fps at 30 Hz
        self.assertTrue(any("model near/40/2/sliver" in f for f in failures), failures)

    def test_video_sliver(self):
        # Captures are delivered at the rate even when the client draws slower (a 30 fps video at 40 Hz).
        r = report([capture("0xa", "focused", 60, "stage"), capture("0xb", "near", 40, "sliver", fps=30.2)], 207.4,
                   clients={0xb: ("spikecanvas", EDGE)})
        self.assertEqual(lc.work_failures(r, True, {}), ["near 0xb 40 Hz at 30.2 fps"])

    def test_calibrated_budget(self):
        # At the self-calibrated 270 Mpix/s six 1080p windows still settle at 60 / 15 x 4 / 10.
        others = [capture(f"0x{i}", "near", 15, "park") for i in range(1, 5)] + [capture("0x5", "far", 10, "park")]
        r = report([capture("0xa", "focused", 60, "stage"), *others], 269.6, effective=270)
        self.assertEqual(lc.work_failures(r, True, {}), [])
        r["budget"]["usedMpix"] = 271
        self.assertEqual(lc.work_failures(r, True, {}), ["used 271.0 > 270.0 Mpix/s"])

    def test_idle_without_frames(self):
        # A visible window the budget idled before its first frame is sized from its client.
        others = [capture(f"0x{i}", "overview", 6, "park", size=(1280, 720)) for i in range(1, 32)]
        idle = [capture(f"0x{i}", "idle", 0, "park", fps=0, frames=False) for i in range(32, 50)]
        sizes = {f"0x{i}": (1280, 720) for i in range(32, 50)}
        r = report([capture("0xa", "focused", 60, "stage"), *others, *idle], 295.8)
        r["canvasState"] = "overview"
        self.assertEqual(lc.overview_failures(r, sizes), [])


class HelperTests(unittest.TestCase):
    def test_ladder_raises(self):
        lines = [(0.0, "near 40 Hz"), (1.0, "near 15 Hz, far 10 Hz"), (3.5, "near 24 Hz"), (4.0, "near 30 Hz")]
        self.assertEqual(lc.ladder_raises(lines), ["ladder near raised twice within 0.5 s (to 30 Hz)"])
        # A tier that left the summary (Overview) comes back without counting as a raise.
        self.assertEqual(lc.ladder_raises([(0, "near 15 Hz"), (1, "overview 10 Hz"), (1.5, "near 30 Hz"), (5, "near 40 Hz")]), [])
        self.assertEqual(lc.ladder_raises([(0, "focused only"), (0.5, "far 10 Hz")]), [])

    def test_memory(self):
        first, last = {"_memory": ("drm-total", 100 * 1024)}, {"_memory": ("drm-total", 142 * 1024)}
        self.assertEqual(lc.memory_failures(first, last), [])
        last["_memory"] = ("drm-total", 143 * 1024)
        self.assertEqual(lc.memory_failures(first, last), ["renderer drm-total grew 100 -> 143 MB"])
        self.assertEqual((lc._kib("12 MiB"), lc._kib("2048 KiB"), lc._kib("4096")), (12288, 2048, 4))

    def test_view_failures(self):
        report = {"captures": [{"output": "0xa", "visible": True}, {"output": "0xb", "visible": False}]}
        self.assertEqual(lc.view_failures(report), ["1 windows out of view (0xb); widen --fov/--size"])
        report["captures"][1]["visible"] = True
        self.assertEqual(lc.view_failures(report), [])

    def test_screencasts(self):
        casts = lc.Screencasts.__new__(lc.Screencasts)
        casts.events = [(1.0, "screencast>>1,window"), (9.0, "screencastv2>>1,window,x"), (9.0, "screencast>>1,window"),
                        (20.4, "screencast>>0,window"), (23.0, "screencast>>0,window")]
        self.assertEqual(len(casts.after(0)), 4)
        self.assertEqual(casts.after(8, (20.0,)), [(9.0, "screencast>>1,window"), (23.0, "screencast>>0,window")])

    def test_tiers_read(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "pose.sock.controls.tiers"
            tiers = lc.Tiers(path, "0xa")
            self.assertEqual(tiers.read(), [])                       # missing: no slivers
            boot = int(time.clock_gettime(time.CLOCK_BOOTTIME))
            path.write_text(f"v1 7 3 {boot} 0x1c sliver 0xb sliver 0xd park\n")
            self.assertEqual(tiers.read(), ["0xb", "0x1c"])          # address order, slivers only
            self.assertIsNone(tiers.read())                          # same seq (the heartbeat): nothing new
            path.write_text(f"v1 7 4 {boot - 5} 0xb sliver\n")
            self.assertEqual(tiers.read(), [])                       # stale: no slivers
            path.write_text(f"v1 7 4 {boot} 0xb sliver\n")
            self.assertEqual(tiers.read(), ["0xb"])                  # the heartbeat after a hitch brings them back
            path.write_text("v1 7 5 x 0xb\n")
            self.assertIsNone(tiers.read())                          # malformed


if __name__ == "__main__":
    unittest.main()
