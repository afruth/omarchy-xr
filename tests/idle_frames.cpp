#include "idle_frames.hpp"
#include "vblank.hpp"
#include <cmath>
#include <iostream>

namespace {
int failures=0;
void expect(bool ok, const char* what) { if (!ok) { std::cerr << what << '\n'; ++failures; } }
idle::Frame still() { return {}; }
}

int main() {
    {
        idle::Gate gate;
        expect(gate.due(still(), 0), "The first frame must draw");
        expect(gate.due(still(), .1), "A frame inside the hold after a change must draw");
        expect(!gate.due(still(), .5), "An unchanged frame after the hold must be skipped");
        expect(gate.skipped==1, "Skipped frames must be counted");
        expect(!gate.due(still(), .9), "Unchanged frames keep being skipped");
        expect(gate.due(still(), 1.51), "The keepalive must draw an unchanged frame");
    }
    {
        idle::Gate gate; gate.due(still(), 0); gate.due(still(), .5);
        auto moved=still();
        // 0.05 mrad of yaw: below a quarter pixel.
        moved.view[0]=float(std::cos(.25e-4)); moved.view[2]=float(std::sin(.25e-4));
        expect(!gate.due(moved, .6), "Sub-pixel head noise must not draw");
        moved.view[0]=float(std::cos(1e-3)); moved.view[2]=float(std::sin(1e-3));
        expect(gate.due(moved, .7), "A head turn over a quarter pixel must draw");
        expect(gate.due(moved, .8), "The hold keeps drawing after a head turn");
        expect(!gate.due(moved, 1.1), "A head at rest after the hold is skipped");
    }
    {
        idle::Gate gate; gate.due(still(), 0); gate.due(still(), .5);
        auto slow=still();
        // Drift accumulates against the last drawn view, not the previous frame.
        for (int i=1; i<=10; ++i) { const double a=i*.15e-4; slow.view[0]=float(std::cos(a)); slow.view[2]=float(std::sin(a)); gate.due(slow, .5+i*.01); }
        expect(gate.haveDrawn && idle::angleBetween(gate.drawn.view, slow.view)<1.1e-4, "Slow drift must redraw once it passes the threshold");
    }
    {
        idle::Gate gate; gate.due(still(), 0); gate.due(still(), .5);
        auto frame=still(); frame.content=1;
        expect(gate.due(frame, .6), "A new capture frame must draw");
        expect(!gate.due(frame, .61), "A capture frame must not start a hold");
        frame.state=42;
        expect(gate.due(frame, 1.0), "A state change must draw");
        auto panned=frame; panned.pan[0]=1e-3f;
        expect(gate.due(panned, 1.5), "A pan must draw");
        auto animating=panned; animating.animating=true;
        expect(gate.due(animating, 2.0) && gate.due(animating, 2.01), "An animation draws every frame");
    }
    {
        idle::Gate gate; gate.due(still(), 0);
        auto ambient=still(); ambient.ambient=.25;
        expect(gate.due(ambient, .31), "An ambient background redraws after the hold");
        expect(gate.due(ambient, .56), "An ambient background redraws at its period");
        expect(!gate.due(ambient, .7), "An ambient background skips inside its period");
    }
    {
        idle::Gate gate; gate.enabled=false; gate.due(still(), 0);
        expect(gate.due(still(), 5), "A disabled gate draws every frame");
        idle::Gate forced; forced.due(still(), 0); forced.due(still(), .5);
        forced.invalidate();
        expect(forced.due(still(), .6), "An invalidated gate draws the next frame");
    }
    {
        expect(idle::Hash{}.add(.5f).value==idle::Hash{}.add(.5002f).value, "Quantised floats at rest must hash equal");
        expect(idle::Hash{}.add(.5f).value!=idle::Hash{}.add(.51f).value, "Distinct floats must hash differently");
        expect(idle::Hash{}.add(1u).add(2u).value!=idle::Hash{}.add(2u).add(1u).value, "Hash order must matter");
    }
    {
        // 60 Hz, period 16666 us: the anchor is the latest extrapolated vblank at or before now.
        expect(vblankAnchorUs(1000000, 1000000, 60)==1000000, "A fresh vblank is its own anchor");
        expect(vblankAnchorUs(1000000, 1040000, 60)==1000000+2*16666, "Skipped flips extrapolate whole periods");
        expect(vblankAnchorUs(1000000, 900000, 60)==1000000 && vblankAnchorUs(1000000, 1040000, 0)==1000000, "No refresh or a past time keeps the last vblank");
    }
    if (failures) return 1;
    std::cout << "Idle frames: gate, hash and vblank anchor ok" << std::endl;
    return 0;
}
