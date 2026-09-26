#pragma once
#include "layout.hpp"
#include "curvature.hpp"
#include "surface.hpp"
#include <algorithm>
#include <cmath>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Window canvas model (docs/infinite-canvas-plan.md §4.3): a 2D pixel plane wrapped on the
// shared cylinder, periodic in x, unbounded in y, translated by the scroll. Camera math adapted from phantomat (BSD-3-Clause, see THIRD_PARTY_NOTICES.md).
namespace canvas {
// Canvas pixels, y down like PanelLayout (up is negative).
struct Rect {
    float x=0, y=0, w=0, h=0;
    float cx() const { return x+w/2; }
    float cy() const { return y+h/2; }
};
// 900 px per world unit as everywhere, so pxPerDeg at R 2.4 matches the glasses' native density.
struct Ring {
    float radius=2.4f;
    float pxPerDeg() const { return 900*radius*spatial::pi/180; }
    float period() const { return 360*pxPerDeg(); }
    // Into (-P/2, P/2].
    float wrap(float dx) const {
        const float p=period(); float r=std::fmod(dx,p);
        if(r<=-p/2) r+=p; else if(r>p/2) r-=p;
        return r;
    }
    // Into [0, P).
    float unwrap(float x) const { const float p=period(); float r=std::fmod(x,p); if(r<0) r+=p; return r>=p ? 0 : r; }
    float heading(float x) const { return x/pxPerDeg(); }
};
struct Fov {
    float vertical=28, aspect=16.f/9;
    float horizontal() const { return 2*std::atan(std::tan(vertical*spatial::pi/360)*aspect)*180/spatial::pi; }
};
struct Metrics { float viewW=0, viewH=0, rowHeight=0; };
inline Metrics metrics(const Ring& ring, const Fov& fov) {
    const float viewW=fov.horizontal()*ring.pxPerDeg(), viewH=fov.vertical*ring.pxPerDeg();
    return {viewW, viewH, std::max(50.f, std::round(.8f*viewH/50)*50)};
}
inline float ease(float value, float target, float dt) { return value+(target-value)*(-std::expm1(-std::max(dt,0.f)/.15f)); }
struct Camera {
    float focusX=0, focusY=0, zoom=1;
    float targetFocusX=0, targetFocusY=0, targetZoom=1;
    // Projected px the cylinder is translated up by (M8): eye level is y' = 0.
    float scrollY=0, targetScrollY=0;
    float anchorX=0, anchorY=0, targetAnchorX=0, targetAnchorY=0;
    bool anchored=false;
    // A period eases x along the shorter way round the ring.
    void tick(float dt, float period=0) {
        const auto along=[&](float& v, float t) {
            if(period<=0) { v=ease(v,t,dt); return; }
            float d=std::fmod(t-v,period); if(d>period/2) d-=period; else if(d<-period/2) d+=period;
            v=ease(v,v+d,dt);
        };
        along(focusX,targetFocusX); focusY=ease(focusY,targetFocusY,dt); zoom=ease(zoom,targetZoom,dt);
        scrollY=ease(scrollY,targetScrollY,dt);
        along(anchorX,targetAnchorX); anchorY=ease(anchorY,targetAnchorY,dt);
    }
    bool moving() const {
        return std::abs(targetFocusX-focusX)>.5f || std::abs(targetFocusY-focusY)>.5f || std::abs(targetZoom-zoom)>1e-3f ||
            std::abs(targetScrollY-scrollY)>.5f;
    }
};
// Wrapping the centre keeps a window whole on one side of the focus.
inline Rect project(const Rect& r, const Camera& c, const Ring& ring) {
    const float w=r.w*c.zoom, h=r.h*c.zoom;
    const float x=c.focusX+ring.wrap(r.cx()-c.focusX)*c.zoom, y=c.focusY+(r.cy()-c.focusY)*c.zoom-c.scrollY;
    return {x-w/2, y-h/2, w, h};
}
// The canvas x and y under a projected point (the inverse of project).
inline float unprojectX(const Camera& c, const Ring& ring, float projectedX) { return ring.unwrap(c.focusX+ring.wrap(projectedX-c.focusX)/std::max(c.zoom,.01f)); }
inline float unprojectY(const Camera& c, float projectedY) { return c.focusY+(projectedY+c.scrollY-c.focusY)/std::max(c.zoom,.01f); }
inline PanelLayout toLayout(const std::string& output, const Rect& projected, float brightness=100) {
    return {output, projected.x, projected.y, projected.w, projected.h, 0, brightness};
}
inline float workZoom(const Rect& r, const Metrics& m) {
    return std::min({1.f, .9f*m.viewW/std::max(r.w,1.f), .9f*m.viewH/std::max(r.h,1.f)});
}
// Buffer pixels, snapped so the logical size at outputScale is whole.
inline Rect fillSize(const Metrics& m, float outputScale) {
    const float s=std::max(outputScale,.1f);
    return {0, 0, std::floor(.9f*m.viewW/s)*s, std::floor(.9f*m.viewH/s)*s};
}
struct Arc { float start=0, length=0; };
// The complement of the largest empty stretch of ring between window extents.
inline Arc usedArc(const std::vector<Rect>& rects, const Ring& ring) {
    if(rects.empty()) return {};
    const float p=ring.period();
    std::vector<std::pair<float,float>> spans;
    for(const auto& r:rects) { const float s=ring.unwrap(r.x); spans.push_back({s, s+std::min(r.w,p)}); }
    std::sort(spans.begin(), spans.end());
    float reach=spans[0].second, bestGap=-1, bestStart=spans[0].first;
    for(size_t i=1;i<spans.size();++i) {
        const float gap=spans[i].first-reach;
        if(gap>bestGap) { bestGap=gap; bestStart=spans[i].first; }
        reach=std::max(reach, spans[i].second);
    }
    const float seam=spans[0].first+p-reach;
    if(seam>=bestGap) { bestGap=seam; bestStart=spans[0].first; }
    if(bestGap<=0) return {spans[0].first, p};
    return {bestStart, p-bestGap};
}
// scrollY: the focus at eye level (M8).
struct Fit { float focusX=0, focusY=0, zoom=1; bool wide=false; float scrollY=0; };
inline float median(std::vector<float> v) {
    if(v.empty()) return 0;
    std::nth_element(v.begin(), v.begin()+v.size()/2, v.end());
    return v[v.size()/2];
}
// Overview: the used arc fills the view width and 0.9 of its height; tiny windows get the wide
// (at most 90°) overview the head pans across instead.
inline Fit fitBounds(const std::vector<Rect>& rects, const Ring& ring, const Metrics& m) {
    if(rects.empty()) return {};
    const auto arc=usedArc(rects, ring);
    const float centre=arc.start+arc.length/2;
    float lo=INFINITY, hi=-INFINITY, top=INFINITY, bottom=-INFINITY;
    std::vector<float> widths;
    for(const auto& r:rects) {
        const float rel=ring.wrap(r.cx()-centre);
        lo=std::min(lo, rel-r.w/2); hi=std::max(hi, rel+r.w/2);
        top=std::min(top, r.y); bottom=std::max(bottom, r.y+r.h); widths.push_back(r.w);
    }
    // Symmetric about the arc centre so no window wraps to the other side of the focus.
    const float spanX=std::max(2*std::max(hi,-lo),1.f), spanY=std::max(bottom-top,1.f), vertical=.9f*m.viewH/spanY;
    Fit fit{ring.unwrap(centre), (top+bottom)/2, std::clamp(std::min({1.f, m.viewW/spanX, vertical}),.08f,1.f)};
    const float mid=median(widths), wanted=3*ring.pxPerDeg()/std::max(mid,1.f);
    if(mid*fit.zoom<3*ring.pxPerDeg() && fit.zoom<1) {
        const float wide=std::min({wanted, 90*ring.pxPerDeg()/spanX, vertical, 1.f});
        if(wide>fit.zoom) { fit.zoom=wide; fit.wide=true; }
    }
    fit.scrollY=fit.focusY;
    return fit;
}
// Keeps the anchor's projected position fixed under the target camera: F2 = (A(z-z2) + F(1-z)) / (1-z2).
inline void zoomAt(Camera& c, float anchorX, float anchorY, float factor, const Ring& ring) {
    const float z=c.targetZoom, z2=std::clamp(z*factor,.08f,1.f);
    if(!c.anchored) { c.anchorX=anchorX; c.anchorY=anchorY; }
    c.anchored=true; c.targetAnchorX=anchorX; c.targetAnchorY=anchorY;
    if(z2>=.999f) { c.targetFocusX=ring.unwrap(anchorX); c.targetFocusY=anchorY; }
    else {
        const float dx=ring.wrap(c.targetFocusX-anchorX), dy=c.targetFocusY-anchorY;
        c.targetFocusX=anchorX+dx*(1-z)/(1-z2); c.targetFocusY=anchorY+dy*(1-z)/(1-z2);
    }
    c.targetZoom=z2;
}
// The arrange grid row (M8: unbounded; nothing else clamps y).
inline int rowFor(float y, const Metrics& m) { return int(std::lround(y/std::max(m.rowHeight,1.f))); }
// Angular cull against the heading, on the circle, and against a projected height band around centreY.
inline std::vector<size_t> visibleIndices(const std::vector<PanelLayout>& projected, float headingDeg, float halfSpanDeg, const Ring& ring,
                                          float centreY=0, float halfHeightPx=INFINITY) {
    std::vector<size_t> out;
    for(size_t i=0;i<projected.size();++i) {
        const auto& p=projected[i];
        const float half=ring.heading(p.width)/2;
        float d=std::fmod(ring.heading(p.x+p.width/2)-headingDeg,360.f);
        if(d>180) d-=360; else if(d<=-180) d+=360;
        if(std::abs(d)<=halfSpanDeg+half && std::abs(p.y+p.height/2-centreY)<=halfHeightPx+p.height/2) out.push_back(i);
    }
    return out;
}
// A projected window fully outside the view around the heading, or above or below it (the new-window edge cue).
inline bool offView(const Rect& projected, float headingDeg, float halfSpanDeg, const Ring& ring,
                    float centreY=0, float halfHeightPx=INFINITY) {
    return std::abs(std::remainder(ring.heading(projected.cx())-headingDeg, 360.f)) > halfSpanDeg+ring.heading(projected.w)/2 ||
        std::abs(projected.cy()-centreY) > halfHeightPx+projected.h/2;
}
// Canvas px per viewport px at the camera zoom (the Overview mouse drag).
inline float dragScale(const Metrics& m, float viewportPx, float zoom) { return m.viewW/std::max(viewportPx, 1.f)/std::max(zoom, .01f); }
// workspaceBend's k = 2π/(span+gap) = 1/R: the ring centre is the eye.
inline Cylinder cylinder(const Ring& ring, float gapPx) {
    spatial::Workspace w; w.degrees=360; w.follow=true; w.gap=gapPx/900;
    return {0, 0, 2*spatial::pi*ring.radius-gapPx/900, ring.radius, w};
}
struct Settings {
    int fps=60;
    float radius=2.4f, gapPx=60, dimUnmatched=.35f, labelDeg=.8f, outputScale=1, captureBudgetMpix=300;
    std::string adoptPolicy="all";
    bool takeoverKeys=true;   // the optional chords (SUPER+TAB, ALT+TAB, SUPER(+SHIFT)+arrows), via the .mode flag
    int refreshHz=60;         // canvas output refresh: the governor's request→ready calibration threshold
    std::vector<std::string> excludes;
};
template<class T> bool readField(std::istream& in, T& value, T lo, T hi) {
    in>>std::ws; if(in.eof()) return false;
    if(!(in>>value) || !(value>=lo && value<=hi)) throw std::runtime_error("Invalid canvas setting");
    return true;
}
// `# canvas v1 fps radius gapPx dimUnmatched labelDeg outputScale captureBudgetMpix adoptPolicy takeoverKeys refreshHz`
// then `exclude <pid|class>` rows. Missing trailing fields keep defaults; later versions may append.
inline Settings parseSettings(std::istream& in) {
    Settings s; std::string line;
    if(!std::getline(in,line) || line.empty()) return s;
    if(line!="# canvas v1" && !line.starts_with("# canvas v1 ")) throw std::runtime_error("Invalid canvas settings header");
    std::istringstream v(line.substr(11));
    (void)(readField(v,s.fps,1,120) && readField(v,s.radius,1.f,10.f) && readField(v,s.gapPx,0.f,500.f) &&
        readField(v,s.dimUnmatched,0.f,1.f) && readField(v,s.labelDeg,.1f,5.f) && readField(v,s.outputScale,1.f,2.f) &&
        readField(v,s.captureBudgetMpix,50.f,2000.f) && (v>>s.adoptPolicy));
    if(s.adoptPolicy.empty() || s.adoptPolicy.find_first_not_of("abcdefghijklmnopqrstuvwxyz-")!=std::string::npos)
        throw std::runtime_error("Invalid canvas adopt policy");
    int takeover=1;
    if(readField(v,takeover,0,1)) { s.takeoverKeys=takeover==1; readField(v,s.refreshHz,30,240); }
    while(std::getline(in,line)) {
        if(line.empty() || line[0]=='#') continue;
        std::istringstream row(line); std::string key, token, extra;
        if(!(row>>key>>token) || key!="exclude" || row>>extra) throw std::runtime_error("Invalid canvas settings row: "+line);
        s.excludes.push_back(token);
    }
    return s;
}
}
