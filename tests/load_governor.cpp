#include "load_governor.hpp"
#include <cassert>
#include <iostream>

int main() {
    SpectatorGovernor g;
    const double period = 1000.0 / 60;
    double t = 0;
    auto frames = [&](int n, double ms) { SpectatorGovernor::Level last = g.level; for (int i = 0; i < n; ++i) { last = g.update(ms, period, t); t += 1.0 / 60; } return last; };
    // Light frames keep the spectator at full rate, and the interval matches 30 fps.
    assert(frames(100, 3) == SpectatorGovernor::Full);
    assert(g.interval() > 0.033 && g.interval() < 0.034);
    // Single spikes are ignored: five heavy frames in a window of thirty stay under the 80th percentile.
    assert(frames(5, 16) == SpectatorGovernor::Full);
    assert(frames(25, 3) == SpectatorGovernor::Full);
    // Sustained 65% load drops one level; sustained 90% pauses.
    assert(frames(30, 10.8) == SpectatorGovernor::Reduced && g.interval() > 0.09);
    assert(frames(30, 15) == SpectatorGovernor::Paused && g.interval() < 0);
    // Recovery needs two calm seconds after the heavy frames leave the window, one step at a time.
    assert(frames(60, 5) == SpectatorGovernor::Paused);          // 1 s: still counting
    assert(frames(90, 5) == SpectatorGovernor::Reduced);         // 2.5 s: one step
    assert(frames(60, 5) == SpectatorGovernor::Reduced);         // 1 s: still counting
    assert(frames(90, 5) == SpectatorGovernor::Full);
    // Reduced does not pause below 85%, and an unknown period changes nothing.
    assert(frames(30, 11) == SpectatorGovernor::Reduced);
    assert(frames(30, 13) == SpectatorGovernor::Reduced);
    assert(g.update(100, 0, t) == SpectatorGovernor::Reduced);
    std::cout << "Spectator governor: percentile thresholds, spike immunity and stepwise recovery passed\n";
}
