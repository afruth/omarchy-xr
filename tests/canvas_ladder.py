"""Settled pixel-budget ladder (docs/infinite-canvas-plan.md §4.4, S1c): the arithmetic of governor::Ladder
in src/capture_governor.hpp without its time hysteresis, so the live harness can predict the rates a
settled renderer reports in pose.sock.stats. tests/test_canvas_ladder.py pins it to the C++ test table.

The staged window runs 60 Hz and is counted first; Near (the top four by pinned, angular size, MRU when
zoomed in; pinned only in Overview) gets the highest step that leaves a 6 Hz floor for the tier below,
Far/Overview the highest step that fits the rest; when even 6 Hz does not fit, the lowest-ranked windows
go idle. The VRAM estimate counts the presented texture at min(demand, native) like the C++ Input.
"""
from dataclasses import dataclass

STEPS = (60, 40, 30, 24, 20, 15, 10, 6)
FOCUSED_HZ, NEAR_CAP, FAR_CAP, OVERVIEW_CAP, PARK_CAP, SLIVER_ABOVE, FLOOR_HZ, NEAR_COUNT = 60, 40, 10, 10, 30, 30, 6, 4
VRAM_CAP, THUMBNAIL = 512 << 20, 256 * 256 * 4


@dataclass(frozen=True)
class Window:
    name: str
    w: int
    h: int
    staged: bool = False
    visible: bool = True
    angular: float = 0.0
    mru: int = 0          # Hyprland focusHistoryID: lower is more recent
    pinned: bool = False
    demand_w: int = 1     # presented size (governor::Input::demandW/H), for the VRAM estimate
    demand_h: int = 1

    @property
    def texture(self) -> float:
        return 4.0 * min(self.w, self.demand_w) * min(self.h, self.demand_h)


@dataclass(frozen=True)
class Decision:
    tier: str             # focused, near, far, overview or idle
    rate_hz: int
    in_flight: int
    place: str            # stage, sliver or park


@dataclass
class _Slot:
    window: Window
    cap: int
    tier: str

    @property
    def px(self) -> float:
        return float(self.window.w * self.window.h)


def step_below(hz: float) -> int:
    return next((s for s in STEPS if s <= hz), 0)


def _tier_cost(tier: list[_Slot], hz: int) -> float:
    return sum(s.px * min(hz, s.cap) for s in tier)


def _fit(tier: list[_Slot], room: float) -> int:
    top = max((s.cap for s in tier), default=0)
    return next((hz for hz in STEPS if (hz <= top or hz == FLOOR_HZ) and _tier_cost(tier, hz) <= room), 0)


def _tiers(ranked: list[Window], zoomed_out: bool, max_hz: int) -> tuple[list[_Slot], list[_Slot]]:
    upper: list[_Slot] = []
    lower: list[_Slot] = []
    for w in ranked:
        near = len(upper) < NEAR_COUNT and (w.pinned or not zoomed_out)
        if near:
            upper.append(_Slot(w, min(PARK_CAP if w.pinned else NEAR_CAP, max_hz), "near"))
        else:
            lower.append(_Slot(w, min(OVERVIEW_CAP if zoomed_out else FAR_CAP, max_hz), "overview" if zoomed_out else "far"))
    return upper, lower


def _water_fill(upper: list[_Slot], lower: list[_Slot], fixed: float, total: float) -> dict[str, int]:
    up = _fit(upper, total - fixed - _tier_cost(lower, FLOOR_HZ)) or FLOOR_HZ
    used = fixed + _tier_cost(upper, up)
    lo = _fit(lower, total - used) or FLOOR_HZ
    used += _tier_cost(lower, lo)
    rate = {s.window.name: min(up, s.cap) for s in upper} | {s.window.name: min(lo, s.cap) for s in lower}
    for s in [*reversed(lower), *reversed(upper)]:
        if used <= total:
            break
        used -= s.px * rate[s.window.name]
        rate[s.window.name] = 0
    return rate


def _lanes(hz: int) -> int:
    return 2 if hz > 30 else 1


def _cap_vram(windows: list[Window], slots: list[_Slot], staged_hz: int, rate: dict[str, int]) -> None:
    used = sum((_lanes(staged_hz) + 2) * 8.0 * w.w * w.h + w.texture if w.staged else THUMBNAIL for w in windows)
    for s in slots:
        hz = rate[s.window.name]
        if not hz:
            continue
        need = _lanes(hz) * 8 * s.px + s.window.texture - THUMBNAIL
        if used + need <= VRAM_CAP:
            used += need
        else:
            rate[s.window.name] = 0


def plan(windows: list[Window], zoomed_out: bool, budget_mpix: float = 300, max_hz: int = 60,
         panic: bool = False) -> dict[str, Decision]:
    """panic: the GPU feedback's > 85 % state, every non-focused window capped at 6 Hz (Ladder::cap)."""
    staged_hz = min(FOCUSED_HZ, max_hz)
    fixed = sum(float(w.w * w.h * staged_hz) for w in windows if w.staged)
    candidates = [w for w in windows if not w.staged and (w.visible or w.pinned)]
    ranked = sorted(candidates, key=lambda w: (not w.pinned, -w.angular, w.mru, w.name))
    upper, lower = _tiers(ranked, zoomed_out, min(max_hz, FLOOR_HZ) if panic else max_hz)
    rate = _water_fill(upper, lower, fixed, budget_mpix * 1e6)
    _cap_vram(windows, upper + lower, staged_hz, rate)
    tier = {s.window.name: s.tier for s in upper + lower}
    out: dict[str, Decision] = {}
    for w in windows:
        if w.staged:
            out[w.name] = Decision("focused", staged_hz, _lanes(staged_hz), "stage")
            continue
        hz = rate.get(w.name, 0)
        place = "sliver" if hz > SLIVER_ABOVE and not w.pinned else "park"
        out[w.name] = Decision(tier[w.name] if hz else "idle", hz, _lanes(hz), place)
    return out


def used_mpix(windows: list[Window], decisions: dict[str, Decision]) -> float:
    return sum(w.w * w.h * decisions[w.name].rate_hz for w in windows) / 1e6
