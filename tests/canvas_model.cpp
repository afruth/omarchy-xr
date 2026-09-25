#include "canvas_model.hpp"
#include "canvas_overlay.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <sstream>
using namespace canvas;
static bool near(float a, float b, float eps) { return std::abs(a-b)<=eps; }
static const Ring ring{};
static const Metrics m=metrics(ring, Fov{});

static void constants() {
    assert(near(ring.pxPerDeg(), 37.7f, .1f) && near(ring.period(), 13572, .1f*360));
    assert(near(Fov{}.horizontal(), 47.81f, .01f) && near(m.viewW, 1802.4f, 1) && near(m.viewH, 1055.6f, 1) && m.rowHeight==850);
    assert(near(workZoom({0,0,1920,1080}, m), .847f, .005f) && workZoom({0,0,800,600}, m)==1);
    const auto fill=fillSize(m, 1), scaled=fillSize(m, 1.25f);
    assert(fill.w==1622 && near(fill.h, 950, 1) && near(scaled.w/1.25f, 1297, .01f) && near(scaled.h/1.25f, 759.5f, .6f) && scaled.w<=.9f*m.viewW);
    const float p=ring.period();
    assert(ring.wrap(p/2)==p/2 && near(ring.wrap(-p/2), p/2, .01f) && near(ring.wrap(p+10), 10, .01f) && near(ring.wrap(-p-10), -10, .01f));
    assert(ring.wrap(0)==0 && near(ring.wrap(3*p-1), -1, .05f) && near(ring.unwrap(-1), p-1, .01f) && ring.unwrap(p)==0);
    assert(rowFor(0,m)==0 && rowFor(900,m)==1 && rowFor(-900,m)==-1 && rowFor(5000,m)==1 && rowFor(-300,m)==0);
}
static bool periodicOverlap(const Rect& a, const Rect& b, float period) {
    for(float s:{-period, 0.f, period})
        if(a.x<b.x+s+b.w-.01f && b.x+s<a.x+a.w-.01f && a.y<b.y+b.h-.01f && b.y<a.y+a.h-.01f) return true;
    return false;
}
static void projection() {
    std::mt19937 rng(7);
    std::vector<Rect> grid;
    for(int row=-1;row<=1;++row) for(float x=0;x+1000<ring.period();x+=1100) grid.push_back({x, row*850.f-400, 1000, 800});
    for(float zoom:{.3f, 1.f}) for(int trial=0;trial<20;++trial) {
        Camera c; c.focusX=std::uniform_real_distribution<float>(0, ring.period())(rng); c.focusY=100; c.zoom=zoom;
        std::vector<Rect> out;
        for(const auto& r:grid) {
            const auto p=project(r, c, ring);
            assert(near(p.w, r.w*zoom, 1e-3f) && near(p.cx(), c.focusX+ring.wrap(r.cx()-c.focusX)*zoom, 1e-2f));
            if(zoom==1) assert(near(ring.wrap(p.x-r.x), 0, .01f) && near(p.y, r.y, .01f));
            out.push_back(p);
        }
        for(size_t i=0;i<out.size();++i) for(size_t j=i+1;j<out.size();++j) assert(!periodicOverlap(out[i], out[j], ring.period()));
    }
    const auto l=toLayout("0xabc", {1,2,3,4}, 80);
    assert(l.output=="0xabc" && l.x==1 && l.width==3 && l.height==4 && l.curvature==0 && l.brightness==80);
}
static void ringCentre() {
    const auto cyl=cylinder(ring, 60);
    for(int i=0;i<24;++i) {
        const float x=i*ring.period()/24-ring.period()/2;
        const auto pose=cyl.pose(PanelLayout{"w", x-300, -200, 600, 400});
        assert(near(std::hypot(pose.center.x, pose.center.z), ring.radius, 1e-3f) && near(pose.center.y, 0, 1e-4f));
        const float heading=std::atan2(pose.center.x, -pose.center.z)*180/spatial::pi;
        float d=std::fmod(heading-ring.heading(x)+540, 360.f)-180;
        assert(near(d, 0, .01f) && near(-pose.yaw*180/spatial::pi, ring.heading(x), .01f) && near(pose.surfaceBend, 1/ring.radius, 1e-5f));
    }
}
static float projectedX(const Camera& c, float x) { Camera t=c; t.focusX=c.targetFocusX; t.zoom=c.targetZoom; return project({x,0,0,0}, t, ring).x; }
static void zooming() {
    for(float factor:{.5f, .7f, .9f, 1.1f, 1.5f, 2.f}) for(float anchor:{0.f, 700.f, 13000.f}) {
        Camera c; c.targetFocusX=c.focusX=300; c.targetZoom=c.zoom=.4f;
        const float before=projectedX(c, anchor);
        zoomAt(c, anchor, 0, factor, ring);
        assert(c.targetZoom>=.08f && c.targetZoom<=1 && c.anchored && c.targetAnchorX==anchor);
        if(c.targetZoom<.999f) assert(near(ring.wrap(projectedX(c, anchor)-before), 0, 1e-2f));
        else assert(c.targetFocusX==ring.unwrap(anchor));
    }
    Camera c; c.targetZoom=.1f;
    zoomAt(c, 0, 0, .1f, ring); assert(c.targetZoom==.08f);
    zoomAt(c, 0, 0, 100, ring); assert(c.targetZoom==1);
    c.zoom=.2f; c.tick(1/60.f); assert(c.moving());
    for(int i=0;i<200;++i) c.tick(1/60.f);
    assert(!c.moving() && near(c.zoom, 1, 1e-3f));
    Camera seam; seam.focusX=ring.period()-10; seam.targetFocusX=10; seam.tick(.05f, ring.period());
    assert(seam.focusX>ring.period()-10); // eased the short way across the seam
}
static void fitting() {
    std::mt19937 rng(3);
    int wide=0;
    for(int trial=0;trial<40;++trial) {
        std::vector<Rect> rects;
        const float arc=std::uniform_real_distribution<float>(2000, ring.period())(rng), scale=trial%4==0 ? .15f : 1;
        for(int i=0;i<60;++i) {
            const float w=std::uniform_real_distribution<float>(400, 1920)(rng)*scale, h=std::uniform_real_distribution<float>(300, 1080)(rng)*scale;
            const int row=int(rng()%3)-1;
            rects.push_back({std::uniform_real_distribution<float>(0, arc)(rng)+trial*977.f, row*850-h/2, w, h});
        }
        const auto fit=fitBounds(rects, ring, m);
        Camera c; c.focusX=fit.focusX; c.focusY=fit.focusY; c.zoom=fit.zoom;
        float lo=INFINITY, hi=-INFINITY;
        for(const auto& r:rects) {
            const auto p=project(r, c, ring);
            lo=std::min(lo, p.x); hi=std::max(hi, p.x+p.w);
            assert(p.y>=fit.focusY-.45f*m.viewH-.5f && p.y+p.h<=fit.focusY+.45f*m.viewH+.5f);
            if(!fit.wide) assert(p.x>=fit.focusX-m.viewW/2-.5f && p.x+p.w<=fit.focusX+m.viewW/2+.5f);
        }
        if(fit.wide) { ++wide; assert(hi-lo<=90*ring.pxPerDeg()+1); }
    }
    assert(wide>0);
    const auto one=fitBounds({{100,-100,1000,600}}, ring, m);
    assert(one.zoom==1 && near(one.focusX, 600, .01f) && near(one.focusY, 200, .01f));
    const auto arc=usedArc({{13000,0,400,10},{200,0,400,10}}, ring);
    assert(near(arc.start, 13000, .01f) && near(arc.length, ring.period()-13000+600, .5f));
}
static void culling() {
    std::mt19937 rng(11);
    std::vector<PanelLayout> layouts;
    for(int i=0;i<80;++i) layouts.push_back({"w", std::uniform_real_distribution<float>(-8000, 20000)(rng), 0, std::uniform_real_distribution<float>(100, 3000)(rng), 500});
    for(int trial=0;trial<200;++trial) {
        const float heading=std::uniform_real_distribution<float>(-400, 400)(rng), half=30;
        std::vector<size_t> brute;
        for(size_t i=0;i<layouts.size();++i) {
            const float a=ring.heading(layouts[i].x), b=ring.heading(layouts[i].x+layouts[i].width);
            bool hit=false;
            for(int k=-3;k<=3;++k) hit|=a+360*k<=heading+half && b+360*k>=heading-half;
            if(hit) brute.push_back(i);
        }
        assert(visibleIndices(layouts, heading, half, ring)==brute);
    }
}
static Settings settings(const std::string& text) { std::istringstream in(text); return parseSettings(in); }
static bool rejects(const std::string& text) { try { settings(text); } catch(const std::runtime_error&) { return true; } return false; }
static void parsing() {
    const auto d=settings("");
    assert(d.fps==60 && d.radius==2.4f && d.gapPx==60 && d.dimUnmatched==.35f && d.adoptPolicy=="all" && d.excludes.empty());
    const auto s=settings("# canvas v1 120 3 40 .5 1 1.25 400 new 0 future 7\nexclude 1234\n\nexclude foot\n");
    assert(s.fps==120 && s.radius==3 && s.gapPx==40 && s.dimUnmatched==.5f && s.labelDeg==1 && s.outputScale==1.25f);
    assert(s.captureBudgetMpix==400 && s.adoptPolicy=="new" && s.excludes==std::vector<std::string>({"1234", "foot"}) && !s.takeoverKeys);
    assert(d.takeoverKeys && !settings("# canvas v1 60 2.4 60 0.35 0.8 1 300 all 0\n").takeoverKeys);
    assert(settings("# canvas v1 60 2.4 60 0.35 0.8 1 300 all\n").takeoverKeys && settings("# canvas v1 60 2.4 60 0.35 0.8 1 300 all 1 x\n").takeoverKeys);
    const auto partial=settings("# canvas v1 30 2\n");
    assert(partial.fps==30 && partial.radius==2 && partial.gapPx==60 && partial.captureBudgetMpix==300);
    for(auto bad:{"# canvas v1 0", "# canvas v1 121", "# canvas v1 60 .5", "# canvas v1 60 11", "# canvas v1 60 2 501",
                  "# canvas v1 60 2 60 .3 .8 3", "# canvas v1 60 2 60 .3 .8 1 40", "# canvas v1 60 2 60 .3 .8 1 2001",
                  "# canvas v1 sixty", "# canvas v10 60", "# settings 60", "# canvas v1 60\nfoo bar", "# canvas v1 60\nexclude a b",
                  "# canvas v1 60 2.4 60 0.35 0.8 1 300 all 2", "# canvas v1 60 2.4 60 0.35 0.8 1 300 all -1", "# canvas v1 60 2.4 60 0.35 0.8 1 300 all yes"})
        assert(rejects(bad));
}
// Overlay berths (canvas_overlay.hpp): pure lazy-follow math, no GL.
static overlay::space::Scene facing(float yawDeg, overlay::space::Vec eye={0, 0, 0}) {
    overlay::space::Scene scene; scene.view=tracking::conjugate(tracking::orientation(0, 0, yawDeg)); scene.eye=eye;
    return scene;
}
static float berthYaw(const overlay::Berth& b) { return std::atan2(b.position.x, -b.position.z)/overlay::degrees; }
static void berths() {
    namespace space=overlay::space;
    overlay::Berth b(0, -6);
    b.update(facing(0), 2.16f, 10);
    assert(near(space::length(b.position), 2.16f, 1e-4f) && near(berthYaw(b), 0, 1e-3f));
    assert(near(std::atan2(b.position.y, -b.position.z)/overlay::degrees, -6, 1e-3f));
    // Inside 12 degrees nothing moves, however long the head stays turned.
    for(double t=10;t<=13;t+=1/60.) b.update(facing(10), 2.16f, t);
    assert(near(berthYaw(b), 0, 1e-3f) && b.awaySince<0);
    // Beyond 12 degrees: held for 0.3 s, then retargeted and eased there (tau 0.15 s).
    overlay::Berth probe; probe.update(facing(20), 2.16f, 0);
    const float turned=berthYaw(probe);
    assert(std::abs(std::abs(turned)-20)<.01f);
    overlay::Berth c; c.update(facing(0), 2.16f, 20);
    double t=20;
    for(;t<20.25;t+=1/60.) { c.update(facing(20), 2.16f, t); assert(near(berthYaw(c), 0, 1e-3f)); }
    for(;t<20.32;t+=1/60.) c.update(facing(20), 2.16f, t);
    assert(c.awaySince<0 && std::abs(std::atan2(c.target.x, -c.target.z)/overlay::degrees-turned)<.01f);
    for(;t<21.5;t+=1/60.) c.update(facing(20), 2.16f, t);
    assert(near(berthYaw(c), turned, .01f) && space::length(space::sub(c.position, c.target))<1e-3f);
    // Eye-relative: a dolly carries the berth along, and a new distance applies without a retarget.
    const auto dollied=facing(20, {0, 0, -1});
    c.update(dollied, 1.2f, t);
    assert(near(space::length(c.target), 1.2f, 1e-4f) && near(berthYaw(c), turned, .01f));
    for(double u=t;u<t+1.5;u+=1/60.) c.update(dollied, 1.2f, u);
    assert(near(space::length(c.position), 1.2f, 1e-3f) && space::length(space::sub(c.centre(dollied), space::add(dollied.eye, c.target)))<1e-3f);
    // The ring reach: R from the centre, less from a dollied eye looking outwards.
    assert(near(overlay::ringReach(facing(0), 2.4f), 2.4f, 1e-4f) && near(overlay::ringReach(facing(0, {0, 0, -1}), 2.4f), 1.4f, 1e-3f));
}
int main() {
    constants(); projection(); ringCentre(); zooming(); fitting(); culling(); parsing(); berths();
    std::cout<<"Canvas model: ring constants, wrap, projection, ring centre, zoom anchor, overview fit, culling, settings and overlay berths passed\n";
}
