#pragma once
#include <cmath>
#include <cstdint>

// Direct presentation skips the draw and the page flip while the image would not change; the
// glasses keep scanning out the last buffer. What goes into a frame, sampled after the tick's
// updates and just before the draw:
//   - view (quaternion) and pan: the head pose with the camera's easing;
//   - content: the sum of every surface's uploaded frames (captures wait for damage);
//   - state: a hash of the rest (surface geometry, halos, cursor, accent, environment settings);
//   - animating: something that moves by time alone (a HUD card, an overlay, a fade, camera easing);
//   - ambient: the slowest redraw period a time-driven background needs, 0 for none.
namespace idle {
struct Frame {
    float view[4]={1, 0, 0, 0};
    float pan[3]={0, 0, 0};
    std::uint64_t content=0, state=0;
    bool animating=false;
    double ambient=0;
};

struct Settings {
    // A quarter of a glasses pixel: 28° over 1080 rows is 4.5e-4 rad per pixel.
    double angle=1.1e-4;
    // The same quarter pixel on a panel 1 world unit away (900 px per unit).
    double pan=2.8e-4;
    // After a view or state change the scene keeps drawing this long, so easing tails and filtered
    // head motion settle without a visible step.
    double hold=.3;
    // A safety net: something outside the frame description still shows within this time.
    double keepalive=1;
};

// The angle between two unit quaternions, in radians.
inline double angleBetween(const float a[4], const float b[4]) {
    const double dot=std::abs(double(a[0])*b[0]+double(a[1])*b[1]+double(a[2])*b[2]+double(a[3])*b[3]);
    return 2*std::acos(std::min(1.0, dot));
}

struct Gate {
    Settings settings;
    bool enabled=true;
    Frame drawn;
    bool haveDrawn=false;
    double drawnAt=0, holdUntil=0;
    unsigned skipped=0;   // skipped since the last report
    std::uint64_t skippedTotal=0;

    // True when the frame has to be drawn; the caller then draws and presents it.
    bool due(const Frame& f, double now) {
        if (!enabled || !haveDrawn) return take(f, now, true);
        const bool moved=f.state!=drawn.state || angleBetween(f.view, drawn.view)>settings.angle
            || std::hypot(f.pan[0]-drawn.pan[0], f.pan[1]-drawn.pan[1], f.pan[2]-drawn.pan[2])>settings.pan;
        if (moved || f.animating) return take(f, now, true);
        // A new capture frame is shown once; it settles nothing, so it does not hold (a blinking cursor
        // costs one frame per blink).
        if (f.content!=drawn.content) return take(f, now, false);
        if (now<holdUntil || now-drawnAt>=settings.keepalive || (f.ambient>0 && now-drawnAt>=f.ambient)) return take(f, now, false);
        ++skipped; ++skippedTotal;
        return false;
    }
    // A forced frame (a new lease, a scene switch) draws and restarts the hold.
    void invalidate() { haveDrawn=false; }
private:
    bool take(const Frame& f, double now, bool hold) {
        drawn=f; haveDrawn=true; drawnAt=now;
        if (hold) holdUntil=now+settings.hold;
        return true;
    }
};

// FNV-1a over the fields that describe the rest of a frame.
struct Hash {
    std::uint64_t value=1469598103934665603ull;
    Hash& add(std::uint64_t v) {
        for (int i=0; i<8; ++i) { value^=(v>>(i*8))&0xff; value*=1099511628211ull; }
        return *this;
    }
    // Floats are quantised first so that an easing value at rest hashes the same every frame.
    Hash& add(float v, float step=1e-3f) { return add(std::uint64_t(std::int64_t(std::llround(double(v)/step)))); }
    Hash& add(bool v) { return add(std::uint64_t(v)); }
    Hash& add(int v) { return add(std::uint64_t(std::int64_t(v))); }
    Hash& add(unsigned v) { return add(std::uint64_t(v)); }
};
}
