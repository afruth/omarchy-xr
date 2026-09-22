#pragma once
#include <algorithm>

// Spectator cadence under GPU pressure. The stereo scene has priority: the mono window drops to
// 10 fps once a frame's GPU time passes 60% of the refresh period and pauses past 85%. It recovers
// one step at a time, and only after two seconds below 45%, so it never flickers between levels.
struct SpectatorGovernor {
    enum Level { Full = 0, Reduced = 1, Paused = 2 };
    Level level = Full;
    double calmSince = -1;

    static constexpr double fullInterval = 1.0 / 30, reducedInterval = 1.0 / 10;

    // gpuMs is the last complete frame's spectator + scene GPU time; periodMs is the refresh period.
    Level update(double gpuMs, double periodMs, double now) {
        if (periodMs <= 0) return level;
        const double load = gpuMs / periodMs;
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
