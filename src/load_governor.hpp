#pragma once
#include <algorithm>
#include <array>

// Spectator cadence under GPU pressure. The stereo scene has priority: the mono window drops to
// 10 fps once the 80th percentile of the last half second's frame GPU time passes 60% of the
// refresh period, and pauses past 85%. It recovers one step at a time, and only after two seconds
// below 45%, so a single slow frame never flips the level.
struct SpectatorGovernor {
    enum Level { Full = 0, Reduced = 1, Paused = 2 };
    Level level = Full;
    double calmSince = -1;
    static constexpr int window = 30;
    std::array<double, window> recent{};
    int count = 0, next = 0;

    static constexpr double fullInterval = 1.0 / 30, reducedInterval = 1.0 / 10;

    // gpuMs is one complete frame's spectator + scene GPU time; periodMs is the refresh period.
    Level update(double gpuMs, double periodMs, double now) {
        if (periodMs <= 0) return level;
        recent[next] = gpuMs; next = (next + 1) % window; if (count < window) ++count;
        if (count < 10) return level;
        std::array<double, window> ranked = recent;
        std::sort(ranked.begin(), ranked.begin() + count);
        const double load = ranked[(count - 1) * 4 / 5] / periodMs;
        if (load > 0.85 && level != Paused) { level = Paused; calmSince = -1; }
        else if (load > 0.60 && level == Full) { level = Reduced; calmSince = -1; }
        else if (load < 0.45 && level != Full) {
            if (calmSince < 0) calmSince = now;
            else if (now - calmSince >= 2.0) { level = static_cast<Level>(level - 1); calmSince = -1; }
        } else calmSince = -1;
        return level;
    }
    // Seconds between spectator frames, or a negative value while paused.
    double interval() const { return level == Full ? fullInterval : level == Reduced ? reducedInterval : -1; }
    const char* name() const { return level == Full ? "30 fps" : level == Reduced ? "10 fps" : "paused"; }
};
