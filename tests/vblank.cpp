#include "vblank.hpp"
#include <cmath>
#include <iostream>

int main() {
    // 60 Hz period is 16666 us; 1.5 periods is 24999 us. A doubled frame counts.
    if (vblankIntervalMissed(0, 16700, 60)) {
        std::cerr << "A normal 60 Hz interval was counted as a missed vblank\n";
        return 1;
    }
    if (!vblankIntervalMissed(0, 33333, 60)) {
        std::cerr << "A doubled 60 Hz interval was not a missed vblank\n";
        return 1;
    }
    if (vblankIntervalMissed(0, 25000, 0) || vblankIntervalMissed(5000, 4000, 60)) {
        std::cerr << "Unknown refresh or a backward timestamp counted as a miss\n";
        return 1;
    }
    // 120 Hz: one extra frame (about 16.7 ms) is over 1.5 periods.
    if (!vblankIntervalMissed(0, 16700, 120)) {
        std::cerr << "A skipped 120 Hz vblank was not counted\n";
        return 1;
    }
    if (latchMarginMs(0, 0) != 3 || latchMarginMs(2, 0) != 3.5 || latchMarginMs(1, 4) != 6.5) {
        std::cerr << "Latch margin must cover CPU and GPU p99 plus 1.5 ms\n";
        return 1;
    }
    // 60 Hz period is 16666 us. A 3 ms margin leaves the deadline 13666 us after the flip.
    if (!latchWaiting(1001000, 1000000, 60, 3) || latchWaiting(1015000, 1000000, 60, 3) || latchWaiting(1001000, 1000000, 0, 3)) {
        std::cerr << "Late latch waited at the wrong time\n";
        return 1;
    }
    const double mid = predictionTargetSeconds(0.001, true, 0, 60, 16.7);
    if (mid < 0.020 || mid > 0.030) {
        std::cerr << "Prediction target is not the middle of the next scanout\n";
        return 1;
    }
    const double later = predictionTargetSeconds(0.020, true, 0, 60, 16.7);
    if (later <= 0.020 || later > 0.050) {
        std::cerr << "Prediction aimed at a scanout that had already started\n";
        return 1;
    }
    // Misses widen the margin at once, up to 6 ms, and the extra drains at 1 ms per 20 s.
    MissPenalty penalty;
    if (penalty.value(100) != 0) { std::cerr << "Miss penalty must start at zero\n"; return 1; }
    penalty.miss(100);
    penalty.miss(100, 2);
    if (penalty.value(100) != 3 || std::abs(penalty.value(120) - 2) > 1e-9) {
        std::cerr << "Miss penalty must add 1 ms per miss and drain 1 ms per 20 s\n";
        return 1;
    }
    penalty.miss(120, 50);
    if (penalty.value(120) != 6 || penalty.value(1000) != 0 || penalty.value(900) != 0) {
        std::cerr << "Miss penalty must stay within 0..6 ms and tolerate a backward clock\n";
        return 1;
    }
    std::cout << "Vblank interval: 1.5 period threshold passed\n";
}
