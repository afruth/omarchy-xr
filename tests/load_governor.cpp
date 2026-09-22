#include "load_governor.hpp"
#include <cassert>
#include <iostream>

int main() {
    SpectatorGovernor g;
    const double period = 1000.0 / 60;
    // Light frames keep the spectator at full rate, and the interval matches 30 fps.
    for (int i = 0; i < 100; ++i) assert(g.update(3, period, i * .016) == SpectatorGovernor::Full);
    assert(g.interval() > 0.033 && g.interval() < 0.034);
    // 60% of the period drops one level at once; 85% pauses.
    assert(g.update(10.5, period, 10) == SpectatorGovernor::Reduced && g.interval() > 0.09);
    assert(g.update(14.5, period, 10.1) == SpectatorGovernor::Paused && g.interval() < 0);
    // A single calm frame does not recover: it takes two seconds below 45%.
    assert(g.update(5, period, 11) == SpectatorGovernor::Paused);
    assert(g.update(5, period, 12.5) == SpectatorGovernor::Paused);
    assert(g.update(5, period, 13.01) == SpectatorGovernor::Reduced);
    // A heavy frame during the calm window restarts the wait; recovery is one level at a time.
    assert(g.update(5, period, 14) == SpectatorGovernor::Reduced);
    assert(g.update(9, period, 14.5) == SpectatorGovernor::Reduced);   // 54%: neither calm nor heavy
    assert(g.update(5, period, 15) == SpectatorGovernor::Reduced);
    assert(g.update(5, period, 16.5) == SpectatorGovernor::Reduced);
    assert(g.update(5, period, 17.01) == SpectatorGovernor::Full);
    // Reduced does not pause below 85%, and an unknown period changes nothing.
    assert(g.update(11, period, 18) == SpectatorGovernor::Reduced);
    assert(g.update(13, period, 18.1) == SpectatorGovernor::Reduced);
    assert(g.update(100, 0, 18.2) == SpectatorGovernor::Reduced);
    std::cout << "Spectator governor: thresholds, hysteresis and stepwise recovery passed\n";
}
