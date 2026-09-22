#pragma once
#include "targeting.hpp"
#include <cmath>
#include <optional>
#include <sstream>
#include <string>

// Gaze dwell: a look point counts as intended only after it has rested on one area of one monitor
// for a while with the head settled. Glances and fast turns never trigger anything. One dwell fires
// once; the gaze has to leave the area, or the head has to move, before it can fire again.
namespace gaze {
struct Settings {
    double dwellMs=1000;   // how long the look point has to rest
    double settleSpeed=10; // deg/s; above this the head is moving and nothing rests
    double radiusPx=120;   // monitor pixels the look point may wander while resting
    bool pointer=true;     // warp the desktop pointer to a dwelled look point
};
// gaze-v1 <dwellMs 100..10000> <settleSpeed 0..500> <radiusPx 0..2000> <pointer 0|1>
inline std::optional<Settings> parseSettings(const std::string& line) {
    std::istringstream in(line); std::string version,extra; Settings s; int pointer=1;
    if (!(in>>version>>s.dwellMs>>s.settleSpeed>>s.radiusPx>>pointer) || version!="gaze-v1" || (in>>extra)
        || !std::isfinite(s.dwellMs) || !std::isfinite(s.settleSpeed) || !std::isfinite(s.radiusPx)
        || s.dwellMs<100 || s.dwellMs>10000 || s.settleSpeed<0 || s.settleSpeed>500 || s.radiusPx<0 || s.radiusPx>2000
        || (pointer!=0 && pointer!=1)) return std::nullopt;
    s.pointer=pointer==1;
    return s;
}

struct Event { std::string output; float pixelX=0, pixelY=0; };

struct Dwell {
    Settings settings;
    std::string output;           // candidate monitor
    float anchorX=0, anchorY=0;   // where the current rest began
    double since=-1;              // when it began; -1: not resting
    bool fired=false;             // this rest already produced an event
    double progress() const { return since<0 ? 0 : 1; }
    // Feed the current look point (empty on a miss) and the head speed; returns an event when a
    // rest has lasted long enough. The pixel coordinates are the ones the hit carries.
    std::optional<Event> update(const std::optional<targeting::Hit>& hit, double headSpeed, double now) {
        if (!hit || headSpeed>settings.settleSpeed) { since=-1; fired=false; output.clear(); return std::nullopt; }
        const bool same=hit->output==output && std::hypot(hit->pixelX-anchorX, hit->pixelY-anchorY)<=settings.radiusPx;
        if (!same || since<0) { output=hit->output; anchorX=hit->pixelX; anchorY=hit->pixelY; since=now; fired=false; return std::nullopt; }
        if (fired || now-since<settings.dwellMs/1000) return std::nullopt;
        fired=true;
        return Event{hit->output, hit->pixelX, hit->pixelY};
    }
    // Fraction of the dwell time completed on the current rest, for a progress cue.
    double fraction(double now) const { return since<0 ? 0 : std::min(1.0, (now-since)/(settings.dwellMs/1000)); }
};
}
