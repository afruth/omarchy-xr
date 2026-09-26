import random
import unittest

from canvas_ladder import FLOOR_HZ, Decision, Window, plan, step_below, used_mpix


def name(i):
    return f"0x{1000 + i}"


def small(n):
    """A staged 1080p window and n-1 other visible 1080p windows, largest first (tests/capture_governor.cpp)."""
    return [Window(name(0), 1920, 1080, staged=True, angular=50)] + [
        Window(name(i), 1920, 1080, angular=40 - i, mru=i) for i in range(1, n)]


def desk():
    """The S1b set: 1 staged 1080p, 10 visible 720p with distinct angular sizes, 39 off-screen."""
    return ([Window(name(0), 1920, 1080, staged=True, angular=40)]
            + [Window(name(i), 1280, 720, angular=30 if i % 5 == 0 else 5 + i, mru=i) for i in range(1, 11)]
            + [Window(name(i), 1280, 720, visible=False, mru=i) for i in range(11, 50)])


def rates(windows, out):
    return [out[w.name].rate_hz for w in windows]


def count(out, tier, hz):
    return sum(d.tier == tier and d.rate_hz == hz for d in out.values())


class LadderTests(unittest.TestCase):
    def test_step_below(self):
        self.assertEqual([step_below(hz) for hz in (61, 60, 39.9, 25, 6, 5.9)], [60, 60, 30, 24, 6, 0])

    def test_s1c_table(self):
        for n, want in ((2, [60, 40]), (4, [60, 24, 24, 24]), (6, [60, 15, 15, 15, 15, 10]),
                        (11, [60, 10, 10, 10, 10, 6, 6, 6, 6, 6, 6])):
            windows = small(n)
            out = plan(windows, False)
            self.assertEqual(rates(windows, out), want, n)
            self.assertLessEqual(used_mpix(windows, out), 300)
            self.assertEqual([out[w.name].tier for w in windows[1:]], ["near" if i <= 4 else "far" for i in range(1, n)])
        windows = desk()
        out = plan(windows, False)
        self.assertEqual((count(out, "focused", 60), count(out, "near", 30), count(out, "far", 10), count(out, "idle", 0)),
                         (1, 4, 6, 39))
        self.assertAlmostEqual(used_mpix(windows, out), 290.304, places=3)
        self.assertEqual({w for w, d in out.items() if d.tier == "near"}, {name(5), name(10), name(9), name(8)})

    def test_places_and_lanes(self):
        windows = small(2)
        out = plan(windows, False)
        self.assertEqual(out[name(0)], Decision("focused", 60, 2, "stage"))
        self.assertEqual(out[name(1)], Decision("near", 40, 2, "sliver"))
        self.assertEqual(plan(small(4), False)[name(1)], Decision("near", 24, 1, "park"))
        # A pinned window never becomes a sliver: the park pull ceiling caps it at 30 Hz.
        pinned = [windows[0], Window(name(1), 1920, 1080, angular=39, pinned=True)]
        self.assertEqual(plan(pinned, False)[name(1)], Decision("near", 30, 1, "park"))
        # The canvas fps caps every tier.
        self.assertEqual(rates(windows, plan(windows, False, max_hz=30)), [30, 30])

    def test_overview(self):
        for n, hz in ((30, 10), (40, 6), (50, 6)):
            windows = [Window(name(i), 1280, 720, angular=3, mru=i) for i in range(n)]
            out = plan(windows, True)
            self.assertTrue(all(d == Decision("overview", hz, 1, "park") for d in out.values()), n)
            self.assertLessEqual(used_mpix(windows, out), 300)
        windows = [Window(name(0), 1920, 1080, staged=True, angular=5)] + [
            Window(name(i), 1280, 720, angular=100 - i, mru=i) for i in range(1, 50)]
        out = plan(windows, True)
        self.assertEqual((out[name(0)].rate_hz, count(out, "overview", 6), count(out, "idle", 0)), (60, 31, 18))
        # Idled from the bottom of the rank.
        self.assertEqual([out[name(i)].rate_hz == 6 for i in range(1, 50)], [i <= 31 for i in range(1, 50)])
        self.assertLessEqual(used_mpix(windows, out), 300)
        # 60 live 1080p windows at native presented size: past 512 MB the lowest-ranked go idle.
        big = [Window(name(i), 1920, 1080, angular=100 - i, mru=i, demand_w=1920, demand_h=1080) for i in range(60)]
        out = plan(big, True, budget_mpix=2000)
        live = [out[w.name].rate_hz > 0 for w in big]
        self.assertEqual(live, sorted(live, reverse=True))
        self.assertEqual(sum(live), int((512 * 2**20 - 60 * 256 * 256 * 4) // (12 * 1920 * 1080 - 256 * 256 * 4)))

    def test_budget(self):
        # The S1b set at the self-calibrated 270 Mpix/s: far drops to 6 Hz.
        windows = desk()
        self.assertEqual((count(plan(windows, False, 270), "near", 30), count(plan(windows, False, 270), "far", 6)), (4, 6))

    def test_panic(self):
        # GPU p80 over 85 %: every non-focused window at 6 Hz, the staged one keeps 60, nothing is a sliver.
        windows = desk()
        out = plan(windows, False, panic=True)
        self.assertEqual(out[name(0)], Decision("focused", 60, 2, "stage"))
        self.assertEqual((count(out, "near", 6), count(out, "far", 6)), (4, 6))
        self.assertEqual(plan(small(2), False, panic=True)[name(1)], Decision("near", 6, 1, "park"))

    def test_never_exceeded(self):
        rng = random.Random(42)
        for _ in range(400):
            n, budget, max_hz = rng.randint(1, 80), rng.choice((300, 50 + rng.randrange(600))), rng.choice((60, 40, 30, 20, 10))
            windows = [Window(name(i), 200 + rng.randrange(3640), 150 + rng.randrange(2000), staged=i == 0 and rng.random() < .66,
                              visible=rng.random() < .75, angular=rng.randrange(60), mru=rng.randrange(50),
                              pinned=rng.random() < 1 / 15) for i in range(n)]
            out = plan(windows, rng.random() < 1 / 3, budget, max_hz)
            staged = sum(w.w * w.h * out[w.name].rate_hz for w in windows if w.staged) / 1e6
            self.assertLessEqual(used_mpix(windows, out), max(budget, staged) + 1e-9)
            for w in windows:
                d = out[w.name]
                self.assertEqual(d.in_flight, 2 if d.rate_hz > 30 else 1)
                self.assertEqual(d.place == "sliver", d.rate_hz > 30 and not w.staged and not w.pinned)
                if not w.staged:
                    self.assertTrue(d.rate_hz == 0 or FLOOR_HZ <= d.rate_hz <= max_hz)
                    self.assertEqual(d.rate_hz == 0, d.tier == "idle")


if __name__ == "__main__":
    unittest.main()
