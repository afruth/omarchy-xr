#pragma once
#include <algorithm>
#include <cstdint>

// A flip is late when the gap since the previous vblank exceeds 1.5 refresh periods.
inline bool vblankIntervalMissed(std::uint64_t previousUs, std::uint64_t nowUs, unsigned refreshHz) {
    if (refreshHz == 0 || nowUs <= previousUs) return false;
    const std::uint64_t period = 1000000ull / refreshHz;
    return nowUs - previousUs > period + period / 2;
}

// CPU work and the GPU scene both have to finish before the flip.
inline double latchMarginMs(double cpuP99Ms, double gpuP99Ms) {
    return std::max(3.0, cpuP99Ms + gpuP99Ms + 1.5);
}

// Extra latch margin learned from missed vblanks. Each miss adds 1 ms at once and the extra drains at
// 1 ms per 20 s, so the loop settles on a margin that holds instead of alternating every second
// between late and early pose sampling.
struct MissPenalty {
    double ms=0, updated=-1;
    double value(double now) {
        if (updated >= 0 && now > updated) ms = std::max(0.0, ms - (now - updated) * 0.05);
        updated = now;
        return ms;
    }
    void miss(double now, unsigned count = 1) { ms = std::min(6.0, value(now) + count); }
};

inline bool latchWaiting(std::uint64_t nowUs, std::uint64_t lastVblankUs, unsigned refreshHz, double marginMs) {
    if (!refreshHz || !lastVblankUs || marginMs < 0) return false;
    const std::uint64_t period = 1000000ull / refreshHz;
    const auto marginUs = static_cast<std::uint64_t>(marginMs * 1000.0);
    if (marginUs + 500 >= period) return false;
    const std::uint64_t next = lastVblankUs + period;
    if (nowUs >= next) return false;
    return nowUs + 250 < next - marginUs;
}

// Host-monotonic seconds for the middle of the next scanout. Without a vblank, use half a frame.
inline double predictionTargetSeconds(double nowSeconds, bool hasVblank, std::uint64_t lastVblankUs, unsigned refreshHz, double frameMs) {
    if (hasVblank && refreshHz) {
        const double period = 1.0 / refreshHz;
        double scanout = lastVblankUs / 1e6 + period;
        while (scanout < nowSeconds) scanout += period;
        return scanout + period / 2;
    }
    const double halfMs = std::clamp(frameMs * 0.5, 0.0, 30.0);
    return nowSeconds + halfMs / 1000.0;
}
