// The real renderer in window canvas mode, offline: a hidden window, a synthetic window list and
// no compositor captures. Placement, landing, the camera invariants and the window file path.
#define main rendererMain
#include "../src/main.cpp"
#undef main
#include <cassert>

namespace {
struct Row { std::uint64_t address; unsigned w, h; int focus; const char* place; };
std::string windowsFile(unsigned long long seq, const std::vector<Row>& rows) {
    std::ostringstream out;
    out << "v1 test " << seq << " 0\n";
    for (const auto& r:rows)
        out << "0x" << std::hex << r.address << std::dec << ' ' << windows::encodeHex("foot") << ' ' << windows::encodeHex("term "+std::to_string(r.address))
            << ' ' << r.w << ' ' << r.h << " 0 0 " << r.focus << ' ' << r.place << " 0 " << 1000+r.address << " 0 1\n";
    return out.str();
}
std::vector<Row> twelve() {
    std::vector<Row> rows;
    const unsigned sizes[4][2]={{1280,720},{1600,900},{1000,700},{1400,800}};
    for (int i=0;i<12;++i) rows.push_back({0x5000u+unsigned(i), sizes[i%4][0], sizes[i%4][1], i==5 ? 0 : i+1, i==5 ? "stage" : "park"});
    return rows;
}
void write(const std::string& path, const std::string& text) {
    const auto before=std::filesystem::exists(path) ? std::filesystem::last_write_time(path) : std::filesystem::file_time_type{};
    std::ofstream(path) << text;
    // A later mtime even on coarse clocks: the renderer polls by mtime only.
    std::filesystem::last_write_time(path, std::max(std::filesystem::last_write_time(path), before+std::chrono::seconds(1)));
}
float angle(float degrees) { return std::abs(std::remainder(degrees, 360.f)); }
void noOverlap(const View& v) {
    const auto& g=v.sceneGeometry(); const float period=v.canvas->ring.period();
    for (size_t i=0;i<g.size();++i) for (size_t j=i+1;j<g.size();++j) {
        if (v.canvas->windows[i].gone || v.canvas->windows[j].gone) continue;
        assert(!canvas::overlaps({g[i].x, g[i].y, g[i].width, g[i].height}, {g[j].x, g[j].y, g[j].width, g[j].height}, 0, period));
    }
}
void ease(View& v, int frames) {
    for (int frame=0;frame<frames;++frame) { v.lastCameraTime=monotonicSeconds()-1./120; v.stepCanvas(v.easeCamera()); }
}
float stagedOffset(const View& v) {
    const auto* l=v.findLayout(v.canvas->stagedName); assert(l);
    return angle(v.canvas->ring.heading(l->x+l->width/2)-View::headingDeg(v.currentView()));
}
void panInsideRing(const View& v) {
    assert(std::hypot(v.targetPanX, v.targetPanY, v.targetPanZ)<=v.canvas->ring.radius-.3f+1e-3f);
    assert(std::hypot(v.panX, v.panY, v.panZ)<=v.canvas->ring.radius-.3f+1e-3f);
}
void placedAndLanded(View& v) {
    assert(v.canvas->live()==12 && v.canvas->stagedName=="0x5005" && v.canvas->state==canvas::Scene::State::Work);
    std::set<int> rows;
    for (const auto& w:v.canvas->windows) rows.insert(canvas::rowFor(w.rect.cy(), v.canvas->metrics));
    assert(rows.size()==3);
    noOverlap(v); panInsideRing(v);
    assert(stagedOffset(v)<1);
    v.navigate({View::Verb::Fit}); assert(v.canvas->state==canvas::Scene::State::Overview);
    ease(v, 90); noOverlap(v); panInsideRing(v);
    v.navigate({View::Verb::Fit}); assert(v.canvas->state==canvas::Scene::State::Work);
    ease(v, 90); noOverlap(v); panInsideRing(v);
    assert(stagedOffset(v)<1);
}
// Every verb is routed; none may run monitor math, and the eye never leaves the ring.
void verbs(View& v) {
    using V=View::Verb;
    for (const View::Move& m:{View::Move{V::Fit}, View::Move{V::Recenter}, View::Move{V::ZoomBy, .5f}, View::Move{V::Pan, 20, 10, true},
                              View::Move{V::FlickIn}, View::Move{V::FlickOut}, View::Move{V::FitTarget}, View::Move{.verb=V::FitOutput, .output="0x5001"}}) {
        v.navigate(m); ease(v, 10); panInsideRing(v);
    }
    v.sampleTarget(); v.projectPanels(1280, 720, monotonicSeconds()); v.placeNotification(monotonicSeconds());
    v.writeStats(monotonicSeconds(), 0, 0, false, 0, 0, 0, 0);
    for (int frame=0;frame<5;++frame) assert(v.tick());
    assert(v.monitorMathCalls==0);
}
void fadeAndPlace(View& v) {
    auto rows=twelve(); rows.erase(rows.begin()+2);
    windows::List list=*windows::parse(windowsFile(2, rows));
    const double t=monotonicSeconds();
    v.canvas->adopt(list, t); v.canvas->tick(t+.1, 0);
    assert(v.canvas->windows.size()==12 && v.canvas->live()==11 && v.findLayout("0x5002")->brightness<100);
    v.canvas->tick(t+.25, 0);
    assert(v.canvas->windows.size()==11 && !v.findLayout("0x5002"));
    rows.push_back({0x6000, 1500, 800, 20, "park"});
    v.canvas->adopt(*windows::parse(windowsFile(3, rows)), t+.3); v.canvas->tick(t+.3, 0);
    assert(v.canvas->live()==12 && v.findLayout("0x6000"));
    noOverlap(v);
}
void reloadFile(View& v) {
    auto rows=twelve(); rows.push_back({0x7000, 900, 600, 30, "park"});
    write(v.windowsPath, windowsFile(10, rows));
    v.nextWindowsCheck=0; v.reloadLayout();
    assert(v.canvas->live()==13 && v.canvas->find("0x7000") && v.canvas->find("0x5002") && !v.canvas->find("0x6000"));
    write(v.windowsPath, "v1 test 11 0\nnot a window\n");
    v.nextWindowsCheck=0; v.reloadLayout();
    assert(v.canvas->live()==13 && v.windowsRejected);
    write(v.windowsPath, windowsFile(4, rows));   // an older sequence number is not adopted
    v.nextWindowsCheck=0; v.reloadLayout();
    assert(v.windowsRejected && v.windowsSeq==10);
    noOverlap(v);
}
// Navigation (WP4). Helpers: a synthetic look point on a window, and the window list with another
// focus_history_id 0 row (the stand-in for Hyprland's focus).
targeting::Hit hitOn(const View& v, const std::string& name, float u=.5f, float w=.5f) {
    const auto* l=v.findLayout(name); assert(l);
    targeting::Hit hit; hit.output=name; hit.u=u; hit.v=w; hit.pixelX=u*l->width; hit.pixelY=w*l->height;
    return hit;
}
std::vector<Row> focusRows(std::uint64_t focused) {
    auto rows=twelve(); rows.push_back({0x7000, 900, 600, 30, "park"});
    for (auto& r:rows) if (r.address==focused) r.focus=0; else if (r.focus==0) r.focus=40;
    return rows;
}
void refocus(View& v, std::uint64_t focused) {
    write(v.windowsPath, windowsFile(v.windowsSeq+1, focusRows(focused)));
    v.nextWindowsCheck=0; v.reloadLayout();
    assert(!v.windowsRejected);
}
bool sameRotation(const tracking::Quaternion& a, const tracking::Quaternion& b) {
    return std::abs(a.w-b.w)<1e-6 && std::abs(a.x-b.x)<1e-6 && std::abs(a.y-b.y)<1e-6 && std::abs(a.z-b.z)<1e-6;
}
void work(View& v) {
    if (v.canvas->state!=canvas::Scene::State::Work) v.navigate({View::Verb::Fit});
    assert(v.canvas->state==canvas::Scene::State::Work);
    ease(v, 120); v.interactionUntil=0;
}
void invariant(View& v) {
    panInsideRing(v); noOverlap(v);
    if (const auto* l=v.findLayout(v.canvas->landed)) assert(navigation::viewingDistance(v.monitorPose(*l), {v.panX, v.panY, v.panZ})>=.3f);
    assert(v.monitorMathCalls==0);
}
// (a) After every verb and 120 easing frames the eye stays inside R - 0.3 and nothing overlaps.
void navigationInvariants(View& v) {
    using V=View::Verb;
    const auto step=[&](const View::Move& m) { v.navigate(m); invariant(v); ease(v, 120); invariant(v); };
    step({V::Fit});
    v.gaze.current=hitOn(v, "0x5005"); step({V::FitTarget}); assert(v.canvas->landed=="0x5005"); v.gaze.current.reset();
    step({V::Recenter});
    for (int i=0;i<10;++i) step({V::ZoomBy, .3f});
    for (int i=0;i<10;++i) step({V::ZoomBy, -.3f});
    step({V::Pan, 200, 50, true}); step({V::Pan, -50, 20, false});
    step({V::FlickIn}); step({V::FlickOut});
    v.interactionUntil=0; step({.verb=V::FitOutput, .output="0x5007"});
}
// (b) Flick out fits every window into the view; flick in lands on a fresh dwell, else on the staged window.
void workAndOverview(View& v) {
    work(v);
    v.navigate({View::Verb::FlickOut}); ease(v, 120);
    assert(v.canvas->state==canvas::Scene::State::Overview);
    const float heading=View::headingDeg(v.currentView()), half=v.canvasFov().horizontal()/2;
    const auto& ring=v.canvas->ring; const auto& m=v.canvas->metrics;
    for (const auto& l:v.sceneGeometry()) {
        assert(angle(ring.heading(l.x)-heading)<=half+.5f && angle(ring.heading(l.x+l.width)-heading)<=half+.5f);
        assert(l.y>=v.canvas->aimY-m.viewH/2-1 && l.y+l.height<=v.canvas->aimY+m.viewH/2+1);
    }
    const double now=monotonicSeconds();
    v.canvas->noteDwell("0x5003", now-1); v.navigate({View::Verb::FlickIn});
    assert(v.canvas->state==canvas::Scene::State::Work && v.canvas->landed=="0x5003");
    ease(v, 120); v.navigate({View::Verb::FlickOut}); ease(v, 120);
    v.canvas->noteDwell("0x5003", now-3); v.navigate({View::Verb::FlickIn});
    assert(v.canvas->landed=="0x5005");
    ease(v, 120);
}
// (c) Overview zoom keeps the gazed canvas point under the gaze; Work zoom dollies, then crosses out.
void zoomAnchor(View& v) {
    v.navigate({View::Verb::FlickOut}); ease(v, 120);
    auto& c=v.canvas->camera; const auto& ring=v.canvas->ring;
    const auto* l=v.findLayout("0x5004"); assert(l);
    const float x0=l->x+.3f*l->width, y0=l->y+.6f*l->height;
    const float ax=c.focusX+ring.wrap(x0-c.focusX)/c.zoom, ay=c.focusY+(y0-c.focusY)/c.zoom;
    for (int i=0;i<5;++i) {
        const float x=c.focusX+ring.wrap(ax-c.focusX)*c.zoom, y=c.focusY+(ay-c.focusY)*c.zoom;
        l=v.findLayout("0x5004"); targeting::Hit hit; hit.output="0x5004"; hit.pixelX=x-l->x; hit.pixelY=y-l->y; v.gaze.current=hit;
        v.navigate({View::Verb::ZoomBy, .12f}); ease(v, 30);
        const float moved=c.focusX+ring.wrap(ax-c.focusX)*c.zoom;
        assert(angle(ring.heading(moved)-ring.heading(x0))<.5f);
        assert(v.canvas->state==canvas::Scene::State::Overview);
    }
    v.gaze.current.reset(); ease(v, 60);
    v.canvas->lastDwellAt=-1e9; v.navigate({View::Verb::FlickIn}); ease(v, 120);
    const float targetZoom=c.targetZoom;
    for (int i=0;i<10;++i) { v.navigate({View::Verb::ZoomBy, .3f}); ease(v, 30); panInsideRing(v); assert(c.targetZoom==targetZoom && std::abs(c.zoom-targetZoom)<2e-3f); }
    assert(v.canvas->depth<v.canvas->ring.radius-.5f);
    const float full=canvas::workZoom(v.canvas->find(v.canvas->landed)->rect, v.canvas->metrics);
    for (int i=0;i<40 && v.canvas->state==canvas::Scene::State::Work;++i) { v.navigate({View::Verb::ZoomBy, -.3f}); ease(v, 10); panInsideRing(v); }
    assert(v.canvas->state==canvas::Scene::State::Overview && c.targetZoom<.9f*full);
    ease(v, 60);
}
// Point the view so the window shows about 60 % of itself.
void partlyVisible(View& v, const std::string& name) {
    const auto* w=v.canvas->find(name); assert(w);
    float best=0, bestShare=-1;
    for (float h=-180;h<180;h+=.25f) {
        v.canvas->cull(h, 20); const float share=v.canvas->visibleShare(*w);
        if (share<.9f && std::abs(share-.6f)<std::abs(bestShare-.6f)) { best=h; bestShare=share; }
    }
    v.canvas->cull(best, 20);
    assert(std::abs(v.canvas->visibleShare(*w)-.6f)<.1f);
}
// (d) Focus follow moves the camera only for a mostly hidden window, and an explicit verb wins.
void focusFollow(View& v) {
    work(v); v.navigate({View::Verb::FlickOut}); ease(v, 120); v.interactionUntil=0;
    auto rotation=v.targetRotation; auto focus=v.canvas->camera.targetFocusX;
    assert(v.canvas->visibleShare(*v.canvas->find("0x5007"))>=.9f);
    refocus(v, 0x5007);
    assert(v.canvas->focusedName=="0x5007" && sameRotation(rotation, v.targetRotation) && focus==v.canvas->camera.targetFocusX);
    work(v); rotation=v.targetRotation;
    partlyVisible(v, "0x5002"); refocus(v, 0x5002);
    assert(!sameRotation(rotation, v.targetRotation) && v.canvas->state==canvas::Scene::State::Work && v.canvas->landed=="0x5002");
    ease(v, 120);
    v.gaze.current=hitOn(v, "0x5005"); v.navigate({View::Verb::FitTarget}); v.gaze.current.reset();
    v.interactionUntil=monotonicSeconds()+.2; rotation=v.targetRotation;
    partlyVisible(v, "0x5009"); refocus(v, 0x5009);
    assert(sameRotation(rotation, v.targetRotation) && v.canvas->landed=="0x5005");
    ease(v, 120);
}
// (e) In Work a settled dwell selects (halo) and records the landing candidate, nothing else.
void dwellInWork(View& v) {
    work(v);
    const auto rotation=v.targetRotation; const auto staged=v.canvas->stagedName; const auto target=v.canvas->camera.targetFocusX;
    const auto serial=v.pointerSerial; const auto hover=v.hoverOutput;
    const auto hit=hitOn(v, "0x5004", .4f, .4f);
    const double start=monotonicSeconds()+1;
    for (double t=start;t<start+3;t+=.05) v.dwellOn(hit, t);
    assert(v.selection.output=="0x5004" && v.canvas->lastDwell=="0x5004");
    assert(sameRotation(rotation, v.targetRotation) && v.canvas->stagedName==staged && v.canvas->camera.targetFocusX==target);
    assert(v.canvas->state==canvas::Scene::State::Work && v.pointerSerial==serial && v.hoverOutput==hover);
}
// Scene-level cases without a View: records built directly, offline scenes.
windows::Record record(std::uint64_t address, const std::string& title, unsigned w, unsigned h, int focus, int pid, bool floating=false) {
    windows::Record r; r.address=address; r.cls="foot"; r.title=title; r.w=w; r.h=h; r.focusHistoryID=focus; r.pid=pid; r.floating=floating;
    return r;
}
windows::List listOf(std::vector<windows::Record> records) { windows::List list; list.records=std::move(records); return list; }
bool sameRect(const canvas::Rect& a, const canvas::Rect& b) { return a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h; }
std::string readFile(const std::string& path) { std::ifstream file(path); return std::string((std::istreambuf_iterator<char>(file)), {}); }
// M3 control plane. The mailboxes live beside the pose socket and belong to this process.
long bootNow() { timespec boot{}; clock_gettime(CLOCK_BOOTTIME, &boot); return boot.tv_sec; }
std::string mailbox(const View& v) { return v.posePath+".controls"; }
std::string mailboxFile(unsigned long long seq, const std::vector<Row>& rows, int atX=20000) {
    std::ostringstream out;
    out << "v1 " << getpid() << ' ' << seq << ' ' << bootNow() << " 20000 0 OMXRTEST-canvas\n";
    for (const auto& r:rows)
        out << "0x" << std::hex << r.address << std::dec << ' ' << windows::encodeHex("foot") << ' ' << windows::encodeHex("term "+std::to_string(r.address))
            << ' ' << r.w << ' ' << r.h << ' ' << atX << " 0 " << r.focus << ' ' << r.place << " 0 " << 1000+r.address << " 0 1\n";
    return out.str();
}
// The v4 line's fields: v4 owner serial enabled output u v pointerSerial px py.
std::vector<std::string> hoverFields(const View& v) {
    AsyncFile::instance().flush();
    std::istringstream in(readFile(mailbox(v)+".hover")); std::vector<std::string> out; std::string f;
    while (in>>f) out.push_back(f);
    assert(out.size()==10 && out[0]=="v4" && out[1]==std::to_string(getpid()));
    return out;
}
// The .windows mailbox list after XR staged a window: it is on the stage and focus_history_id 0. Lua
// publishes no .focus for XR's own staging, so a partly visible window keeps the camera where it is.
void restagedByXr(View& v, const std::string& name) {
    auto rows=twelve(); rows.push_back({0x8000, 900, 600, 50, "park"});
    const auto address=*windows::parseAddress(name);
    for (auto& r:rows) { r.place=r.address==address ? "stage" : "park"; r.focus=r.address==address ? 0 : r.focus==0 ? 40 : r.focus; }
    partlyVisible(v, name); v.interactionUntil=0;
    const auto rotation=v.targetRotation; const auto focus=v.canvas->camera.targetFocusX; const auto landed=v.canvas->landed;
    write(mailbox(v)+".windows", mailboxFile(v.windowsSeq+1, rows)); v.steer();
    assert(v.canvas->stagedName==name && v.canvas->focusedName==name);
    assert(sameRotation(rotation, v.targetRotation) && focus==v.canvas->camera.targetFocusX && landed==v.canvas->landed);
}
// (f) Without --canvas-windows-file the .windows mailbox feeds the scene through steer(); with it, never.
void mailboxList(View& v) {
    auto rows=twelve(); rows.push_back({0x8100, 900, 600, 50, "park"});
    write(mailbox(v)+".windows", mailboxFile(v.windowsSeq+1, rows));
    v.steer();
    assert(!v.canvas->find("0x8100"));
    v.windowsPath.clear(); rows.back().address=0x8000;
    write(mailbox(v)+".windows", mailboxFile(v.windowsSeq+2, rows));
    v.steer();
    assert(v.canvas->find("0x8000") && !v.canvas->find("0x8100") && v.canvas->stagedName=="0x5005");
    assert(v.canvas->outputName=="OMXRTEST-canvas" && v.canvas->outputX==20000 && v.canvas->outputY==0);
}
// (g) FitTarget on a gazed hit publishes it once in buffer px; a settled dwell in Work publishes nothing.
void hoverV4(View& v) {
    work(v);
    const auto serial=v.pointerSerial;
    v.gaze.current=hitOn(v, "0x5005", .25f, .5f); v.navigate({View::Verb::FitTarget}); v.gaze.current.reset();
    assert(v.pointerSerial==serial+1 && v.hoverOutput=="0x5005" && v.pointerX==400 && v.pointerY==450 && v.restageRequested.empty());
    assert(v.tick());
    auto f=hoverFields(v);
    assert(f[3]=="1" && f[4]=="0x5005" && f[7]==std::to_string(serial+1) && f[8]=="400" && f[9]=="450");
    ease(v, 60);
    const auto hit=hitOn(v, "0x5004", .4f, .4f); const double start=monotonicSeconds()+1;
    for (double t=start;t<start+3;t+=.05) v.dwellOn(hit, t);
    assert(v.pointerSerial==serial+1 && v.hoverOutput=="0x5005");
}
// (h) A cursor inside the staged window is the XR cursor; overflow into a neighbour restages it once.
void virtualCursor(View& v) {
    const auto* staged=v.canvas->staged(); assert(staged && staged->name=="0x5005" && staged->record.atX==20000);
    unsigned long long seq=1;
    const auto cursor=[&](double x, double y, double ox, double oy) {
        std::ostringstream line; line << "v1 " << getpid() << ' ' << seq++ << ' ' << x << ' ' << y << ' ' << ox << ' ' << oy << ' ' << bootNow() << '\n';
        write(mailbox(v)+".cursor", line.str()); v.steer();
    };
    cursor(20100, 50, 0, 0);
    assert(v.xrCursor.valid && v.xrCursor.window=="0x5005" && v.xrCursor.px==100 && v.xrCursor.py==50);
    for (int frame=0;frame<3;++frame) assert(v.tick());
    const canvas::CanvasWindow* neighbour=nullptr;
    // A neighbour whose centre no other window covers (a radius reload may leave windows overlapping).
    const auto covers=[&](const canvas::CanvasWindow& w, float x, float y) {
        return !w.gone && v.canvas->ring.unwrap(x-w.rect.x)<w.rect.w && y>=w.rect.y && y<w.rect.y+w.rect.h;
    };
    for (const auto& w:v.canvas->windows) {
        if (w.gone || w.staged) continue;
        const float x=v.canvas->ring.unwrap(w.rect.cx()), y=w.rect.cy();
        if (std::none_of(v.canvas->windows.begin(), v.canvas->windows.end(), [&](const auto& o) { return &o!=&w && covers(o, x, y); })) { neighbour=&w; break; }
    }
    assert(neighbour);
    const auto name=neighbour->name; const auto& s=v.canvas->staged()->rect;
    const double ox=v.canvas->ring.wrap(neighbour->rect.cx()-s.x)-(staged->record.w-1), oy=neighbour->rect.cy()-s.y-10;
    const auto serial=v.pointerSerial;
    cursor(20000+staged->record.w-1, 10, ox, oy);
    assert(!v.xrCursor.valid || v.xrCursor.window=="0x5005");
    assert(v.pointerSerial==serial+1 && v.hoverOutput==name && v.restageRequested==name && v.selection.output==name);
    assert(std::abs(v.pointerX-neighbour->pixelW/2.f)<1 && std::abs(v.pointerY-neighbour->pixelH/2.f)<1);
    assert(v.tick());
    assert(hoverFields(v)[4]==name);
    cursor(20000+staged->record.w-1, 10, ox, oy); cursor(20000+staged->record.w-1, 10, ox+1, oy);
    assert(v.pointerSerial==serial+1 && v.restageRequested==name);
    auto rows=twelve(); rows.push_back({0x8000, 900, 600, 50, "park"});
    const auto address=*windows::parseAddress(name);
    for (auto& r:rows) r.place=r.address==address ? "stage" : "park";
    write(mailbox(v)+".windows", mailboxFile(v.windowsSeq+1, rows)); v.steer();
    assert(v.canvas->stagedName==name && v.restageRequested.empty());
    restagedByXr(v, name);
    cursor(20010, 20, 0, 0);
    assert(v.xrCursor.valid && v.xrCursor.window==name && v.xrCursor.px==10 && v.xrCursor.py==20);
}
// The view (u, v) of a window's centre, as a 2D click would give it.
std::optional<std::pair<float,float>> projectCentre(const View& v, const std::string& name) {
    const auto* l=v.findLayout(name); if (!l) return std::nullopt;
    const auto c=spatial::vertex(v.monitorPose(*l), 0, 0);
    const auto p=targeting::rotate(v.currentView(), targeting::add({c.x, c.y, c.z}, {v.panX, v.panY, v.panZ}));
    const float t=std::tan(v.fov*pi/360), r=t*v.aspect();
    if (p.z>=0) return std::nullopt;
    const float u=.5f+p.x/-p.z/(2*r), w=.5f-p.y/-p.z/(2*t);
    if (u<.05f || u>.95f || w<.05f || w>.95f) return std::nullopt;
    return std::pair{u, w};
}
// (i) A click at a window's projected centre focuses that window at its buffer centre; the camera stays.
void clickPath(View& v) {
    v.navigate({View::Verb::FlickOut}); ease(v, 120);
    std::string name; std::pair<float,float> at;
    for (const auto& w:v.canvas->windows) if (!w.gone && !w.staged) if (const auto p=projectCentre(v, w.name)) { name=w.name; at=*p; break; }
    assert(!name.empty());
    const auto serial=v.pointerSerial; const auto rotation=v.targetRotation;
    v.clickAt(at.first, at.second);
    const auto* w=v.canvas->find(name);
    assert(v.pointerSerial==serial+1 && v.hoverOutput==name && sameRotation(rotation, v.targetRotation));
    assert(std::abs(v.pointerX-w->pixelW/2.f)<.02f*w->pixelW && std::abs(v.pointerY-w->pixelH/2.f)<.02f*w->pixelH);
    assert(v.tick());
    const auto f=hoverFields(v);
    assert(f[3]=="1" && f[4]==name && f[7]==std::to_string(serial+1));
    restagedByXr(v, name);
    restagedByXr(v, "0x5005");
}
// (j) The renderer's controls version gate: v6 or newer beside the pose socket.
void versionGate(const std::string& temp) {
    const std::string dir=temp+"/gate", pose=dir+"/pose.sock"; std::filesystem::create_directories(dir);
    std::string why;
    assert(!controlsVersionOk(pose, why) && why.find("missing")!=std::string::npos);
    std::ofstream(dir+"/controls.version") << "5\n"; assert(!controlsVersionOk(pose, why) && why=="found version 5");
    std::ofstream(dir+"/controls.version") << "6\n"; assert(controlsVersionOk(pose, why));
    std::ofstream(dir+"/controls.version") << "7\n"; assert(controlsVersionOk(pose, why));
    std::ofstream(dir+"/controls.version") << "six\n"; assert(!controlsVersionOk(pose, why));
    assert(!controlsVersionOk("", why));
}
// A closed window's entry is released, so the same class and title reopen at its rect under a new
// address; a taken rect starts the ring search there.
canvas::Rect reopenAndTaken(canvas::Scene& s) {
    const auto mail=record(0x9002, "mail", 1000, 600, 1, 2);
    s.adopt(listOf({record(0x9001, "notes", 1280, 720, 0, 1), mail}), 10);
    const auto notes=s.find("0x9001")->rect;
    s.adopt(listOf({mail}), 11); s.tick(11+canvas::fadeSeconds+.01, 0);
    assert(!s.find("0x9001") && s.windows.size()==1);
    s.adopt(listOf({record(0x9003, "notes", 1280, 720, 0, 3), mail}), 12);
    assert(sameRect(s.find("0x9003")->rect, notes));
    s.adopt(listOf({mail}), 13); s.tick(13+canvas::fadeSeconds+.01, 0);
    auto& other=s.windows[0]; assert(other.name=="0x9002");
    other.rect.x=notes.x; other.rect.y=notes.y; s.refresh(14);
    s.adopt(listOf({record(0x9004, "notes", 1280, 720, 0, 4), mail}), 14);
    const auto moved=s.find("0x9004")->rect, taken=s.find("0x9002")->rect;
    assert(!sameRect(moved, notes) && !canvas::overlaps(moved, taken, 0, s.ring.period()));
    assert(std::abs(s.ring.wrap(moved.cx()-notes.cx()))<=2*(notes.w+s.settings.gapPx) && std::abs(moved.cy()-notes.cy())<=2*(notes.h+s.settings.gapPx));
    return moved;
}
// The debounced file keeps windows and the settled camera; a new scene claims and restores them.
void sceneMemory(const std::string& temp) {
    const std::string path=temp+"/canvas-memory.tsv";
    canvas::Rect notes;
    {
        canvas::Scene s(canvas::Ring{}, canvas::Settings{}, path, true);
        notes=reopenAndTaken(s);
        s.tick(14.5, 0);
        s.rememberCamera=true; s.camera.targetFocusX=500; s.camera.targetFocusY=0; s.camera.targetZoom=.5f; s.snap();
        s.tick(20, 0); s.tick(21.5, 0);
        AsyncFile::instance().flush();
        const auto text=readFile(path);
        assert(text.find("window\tfoot\tnotes\t")!=std::string::npos && text.find("window\tfoot\tmail\t")!=std::string::npos);
        assert(text.find("camera\t500\t0\t0.5\n")!=std::string::npos && !s.memory.dirty);
    }
    canvas::Scene s(canvas::Ring{}, canvas::Settings{}, path, true);
    assert(s.memory.camera && s.restoreCamera({}));
    assert(s.state==canvas::Scene::State::Overview && s.camera.targetFocusX==500 && s.camera.targetZoom==.5f);
    s.adopt(listOf({record(0x9005, "notes", 1280, 720, 0, 5)}), 30);
    assert(sameRect(s.find("0x9005")->rect, notes));
}
// The buffer size wins over later list sizes; a dialog starts at its tiled window, other floating
// windows at the focus.
void sizeAndParent() {
    canvas::Scene s(canvas::Ring{}, canvas::Settings{}, "", true);
    const auto editor=[](unsigned w, unsigned h) { return record(0xa001, "editor", w, h, 0, 77); };
    s.adopt(listOf({editor(1280, 720)}), 1);
    auto& w=s.windows[0]; w.sourceWidth=1500; w.sourceHeight=900; s.sizeFromBuffer(w);
    assert(w.sized && w.rect.w==1500 && w.rect.h==900);
    s.adopt(listOf({editor(1100, 600)}), 2);
    assert(s.windows[0].rect.w==1500 && s.windows[0].rect.h==900 && s.windows[0].pixelW==1500);
    s.windows[0].rect.x=5000; s.refresh(2);
    s.adopt(listOf({editor(1100, 600), record(0xa002, "save as", 600, 400, 1, 77, true), record(0xa003, "picker", 600, 400, 2, 78, true)}), 3);
    const auto parent=s.find("0xa001")->rect, dialog=s.find("0xa002")->rect, other=s.find("0xa003")->rect;
    assert(std::abs(s.ring.wrap(dialog.cx()-parent.cx()))<=parent.w/2+dialog.w+2*s.settings.gapPx);
    assert(std::abs(s.ring.wrap(other.cx()))<std::abs(s.ring.wrap(other.cx()-parent.cx())));
}
// A full ring stacks the rest at the focus: snapped, unwrapped, logged.
void noFreePlace() {
    canvas::Scene s(canvas::Ring{}, canvas::Settings{}, "", true);
    std::vector<windows::Record> many;
    for (int i=0;i<40;++i) many.push_back(record(0xb000u+unsigned(i), "full "+std::to_string(i), 1600, 700, i, 100+i));
    s.adopt(listOf(many), 1);
    canvas::Rect fallback=canvas::snap({-800, -350, 1600, 700}); fallback.x=s.ring.unwrap(fallback.x);
    const auto stacked=std::count_if(s.windows.begin(), s.windows.end(), [&](const auto& w) { return sameRect(w.rect, fallback); });
    assert(s.live()==40 && stacked>=2);
    for (const auto& w:s.windows) assert(w.rect.x>=0 && w.rect.x<s.ring.period());
}
// deliver: every update is a new w x h CPU frame; demanded: the last setDemand visibility.
struct FakeSource final : FrameSource {
    bool living=true, deliver=false, demanded=true; unsigned w=1, h=1; std::string failure;
    double ready=-1;
    double requestToReadyMs() const override { return ready; }
    bool update(CapturedFrame& f) override {
        if(!deliver) return false;
        f.width=f.sourceWidth=w; f.height=f.sourceHeight=h; f.texture=0; f.rgba.assign(size_t(w)*h*4, 255);
        return true;
    }
    void service() override {}
    void setDemand(bool visible, unsigned, unsigned) override { demanded=visible; }
    void setFrameRate(unsigned, unsigned, double) override {}
    const char* transport() const override { return "fake"; }
    unsigned requests() const override { return 0; }
    double importLatencyMs() const override { return -1; }
    const std::string& error() const override { return failure; }
    bool alive() const override { return living; }
};
canvas::CanvasWindow& stagedWindow(View& v) {
    const auto it=std::find_if(v.canvas->windows.begin(), v.canvas->windows.end(), [&](const auto& w) { return !w.gone && w.staged; });
    assert(it!=v.canvas->windows.end());
    return *it;
}
// (k) Region frames of the staged window replace the export (idled) and hide the XR cursor; 0.6 s
// without one, or an error, brings the export back.
void stageSourceFallback(View& v) {
    auto& w=stagedWindow(v);
    auto owned=std::make_unique<FakeSource>(), region=std::make_unique<FakeSource>();
    auto *exported=owned.get(), *stage=region.get();
    stage->deliver=true; stage->w=w.pixelW; stage->h=w.pixelH;
    if (!w.texture) w.texture=gltex::create();
    w.source=std::move(owned); w.stageSource=std::move(region); w.visible=true;
    const auto before=w.frames;
    v.updateWindowCaptures();
    assert(w.stageFrame && w.stageFrames==1 && w.frames==before+1 && w.stageLastFrame>0 && w.width==w.pixelW);
    v.canvas->schedule(false, monotonicSeconds());
    assert(w.regionShown && !exported->demanded && stage->demanded);
    v.xrCursor={true, w.name, 10, 10};
    assert(!v.xrCursorLayout());
    stage->deliver=false;
    v.canvas->schedule(false, monotonicSeconds()+.6);
    assert(!w.regionShown && exported->demanded && v.xrCursorLayout());
    stage->deliver=true; v.updateWindowCaptures(); v.canvas->schedule(false, monotonicSeconds());
    assert(w.regionShown);
    stage->failure="Captured output was disconnected";
    v.canvas->schedule(false, monotonicSeconds());
    assert(!w.regionShown && exported->demanded);
    v.updateWindowCaptures();
    assert(!w.stageSource && !w.stageFrame && w.stageRetryAt>monotonicSeconds() && w.stageW);
    w.source.reset(); v.xrCursor={};
}
// (l) The region follows the staged window's rectangle; unstaging drops it. The offline scene never
// opens a source, so the rectangle fields are the observable.
void stageRegion(View& v) {
    const auto original=v.canvas->lastList;
    auto& w=stagedWindow(v); const auto name=w.name;
    assert(w.stageX==0 && w.stageY==0 && w.stageW==w.record.w && w.stageH==w.record.h);
    v.canvas->openStage(w, monotonicSeconds()+10); assert(!w.stageSource);
    auto moved=original;
    for (auto& r:moved.records) if (r.name()==name) { r.atX+=40; r.atY+=8; r.w-=100; }
    w.stageSource=std::make_unique<FakeSource>();
    v.canvas->adopt(moved, monotonicSeconds());
    const auto* again=v.canvas->find(name);
    assert(again->staged && !again->stageSource && again->stageX==40 && again->stageY==8 && again->stageW==again->record.w && again->stageRetryAt==0);
    auto other=original; std::string next;
    for (auto& r:other.records) {
        if (r.place==windows::Place::Stage) r.place=windows::Place::Park;
        else if (next.empty()) { r.place=windows::Place::Stage; next=r.name(); }
    }
    stagedWindow(v).stageSource=std::make_unique<FakeSource>();
    v.canvas->adopt(other, monotonicSeconds());
    const auto* old=v.canvas->find(name); const auto* now=v.canvas->find(next);
    assert(!old->staged && !old->stageSource && !old->stageW && now->staged && now->stageW==now->record.w);
    // No `stage` row (session start, the staged window closed): the focus fallback is parked, so it
    // gets no region and no XR cursor; the export serves it.
    auto parked=original;
    for (auto& r:parked.records) if (r.place==windows::Place::Stage) r.place=windows::Place::Park;
    stagedWindow(v).stageSource=std::make_unique<FakeSource>();
    v.canvas->adopt(parked, monotonicSeconds());
    auto& fallback=stagedWindow(v);
    assert(!canvas::Scene::onStage(fallback) && !fallback.stageSource && !fallback.stageW);
    fallback.stageSource=std::make_unique<FakeSource>(); fallback.stageLastFrame=monotonicSeconds();
    v.canvas->schedule(false, monotonicSeconds());
    assert(!fallback.regionShown); fallback.stageSource.reset(); fallback.stageLastFrame=0;
    v.lastCursor=windows::Cursor{"", 1, double(fallback.record.atX+10), double(fallback.record.atY+10)}; v.cursorAt=monotonicSeconds();
    v.updateVirtualCursor();
    assert(!v.xrCursor.valid);
    v.lastCursor.reset();
    v.canvas->adopt(original, monotonicSeconds());
    assert(v.canvas->stagedName==name);
}
// (m) A Studio session has no --canvas-windows-file: the first .windows mailbox list lands on its staged window.
void firstMailboxLands(SDL_Window* window, const std::string& temp) {
    const std::string dir=temp+"/fresh", pose=dir+"/pose.sock", canvasPath=dir+"/canvas.tsv", empty;   // the View keeps references
    std::filesystem::create_directories(dir);
    std::vector<Panel> none;
    View v(none,false,spatial::Workspace{80},24,empty,pose,false,false,64,28,canvasPath,60,false);
    v.window=window; v.mode=View::SceneMode::Canvas;
    v.canvas=std::make_unique<canvas::Scene>(canvas::Ring{}, canvas::Settings{}, "", true);
    v.environment=std::make_unique<SkyEnvironment>("");
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v.maxTexture);
    v.primeCamera(); v.startCanvas();
    assert(v.windowsSeq==0 && v.canvas->stagedName.empty());
    write(mailbox(v)+".windows", mailboxFile(1, twelve()));
    v.steer();
    assert(v.windowsSeq==1 && v.canvas->stagedName=="0x5005" && v.canvas->landed=="0x5005" && v.canvas->state==canvas::Scene::State::Work);
    ease(v, 120);
    assert(stagedOffset(v)<1);
    v.finishCanvas();
}
// Windows grouped by category are contiguous in packing order: rows 0, +1, -1, then along the ring from the heading.
void groupsContiguous(const canvas::Scene& s, float headingX) {
    std::vector<std::tuple<int,float,std::string>> order;
    const int rank[3]={2, 0, 1};
    for (const auto& w:s.windows) {
        if (w.gone || w.pinned) continue;
        const int row=std::clamp(canvas::rowFor(w.rect.cy(), s.metrics), -1, 1);
        order.emplace_back(rank[row+1], s.ring.unwrap(w.rect.x-headingX), canvas::categoryName(canvas::category(w.record.cls, w.record.title)));
    }
    std::sort(order.begin(), order.end());
    std::set<std::string> closed; std::string last;
    for (const auto& [row, x, group]:order) {
        if (group==last) continue;
        assert(!closed.count(group));
        if (!last.empty()) closed.insert(last);
        last=group;
    }
}
// Arrange on a canvas that fits: no overlap, categories contiguous, inside the band, pinned windows stay.
void arrangeFits() {
    canvas::Scene s(canvas::Ring{}, canvas::Settings{}, "", true);
    // Two terminal classes on either side of code in MRU order (foot 1, code 2, alacritty 4): Terminal stays one run.
    const char* classes[]={"firefox", "foot", "code", "slack", "alacritty", "firefox", "code", "slack", "foot", "firefox"};
    std::vector<windows::Record> records;
    for (int i=0;i<10;++i) { auto r=record(0xd000u+unsigned(i), "w"+std::to_string(i), 1100, 650, i, 200+i); r.cls=classes[i]; records.push_back(r); }
    s.adopt(listOf(records), 1);
    s.findMutable("0xd003")->pinned=true; const auto pinnedAt=s.find("0xd003")->rect;
    s.overview({});
    const float headingX=s.headingPoint().first;
    assert(s.arrange({}) && s.history.undo.size()==1);
    for (const auto& a:s.windows) {
        assert(canvas::inBand(a.rect, s.metrics));
        for (const auto& b:s.windows) if (&a!=&b && !a.pinned && !b.pinned) assert(!canvas::overlaps(a.rect, b.rect, 0, s.ring.period()));
    }
    assert(sameRect(s.find("0xd003")->rect, pinnedAt));
    groupsContiguous(s, headingX);
}
// The palette never covers the selection: its bottom edge clears the palette top by 1.5°, its top stays in
// view; a selection too tall for that band lowers the Search zoom, one that fits keeps it.
void paletteClear() {
    canvas::Scene s(canvas::Ring{}, canvas::Settings{}, "", true);
    s.adopt(listOf({record(0xe001, "tall editor", 1200, 1000, 1, 300), record(0xe002, "small note", 700, 400, 0, 301)}), 1);
    const auto edges=[&](const std::string& name) {
        const auto* w=s.find(name); const float perTan=900*s.ring.radius, h=w->rect.h*s.camera.targetZoom, c=s.aimY-s.camera.targetFocusY;
        assert(std::abs(s.camera.targetFocusY-w->rect.cy())<1e-3f);
        return std::pair{std::atan((c-h/2)/perTan)*180/float(pi), std::atan((c+h/2)/perTan)*180/float(pi)};
    };
    s.overview({}); s.camera.targetZoom=1;   // one window at zoom 1 is about 26° tall
    s.searchType("tall editor", {});
    assert(s.state==canvas::Scene::State::Search && s.selectedResult()=="0xe001" && s.camera.targetZoom<.6f);
    auto [bottom, top]=edges("0xe001");
    assert(bottom>=s.paletteTopDeg()+1.5f-1e-2f && top<=s.fov.vertical/2-.5f+1e-2f);
    s.searchType("small note", {});
    const float zoom=s.camera.targetZoom;
    assert(s.selectedResult()=="0xe002");
    std::tie(bottom, top)=edges("0xe002");
    assert(s.camera.targetZoom==zoom && bottom>=s.paletteTopDeg()+1.5f-1e-2f && top<=s.fov.vertical/2);
}
// The smoke rule: the focused window needs 10 frames, near windows one; gone, closed and dead ones pass.
void smokeRule() {
    canvas::Scene s(canvas::Ring{}, canvas::Settings{}, "", true);
    s.adopt(listOf({record(0xc001, "a", 800, 600, 0, 1), record(0xc002, "b", 800, 600, 1, 2), record(0xc003, "c", 800, 600, 2, 3)}), 1);
    auto &focused=s.windows[0], &nearby=s.windows[1], &far=s.windows[2];
    focused.decision.tier=governor::Tier::Focused; nearby.decision.tier=governor::Tier::Near; far.decision.tier=governor::Tier::Far;
    focused.frames=9; nearby.frames=1; assert(!s.smokeDone());
    focused.frames=10; assert(s.smokeDone());
    nearby.frames=0; assert(!s.smokeDone());
    nearby.closed=true; assert(s.smokeDone());
    nearby.closed=false; nearby.gone=true; assert(s.smokeDone());
    nearby.gone=false; auto dead=std::make_unique<FakeSource>(); dead->living=false; nearby.source=std::move(dead); assert(s.smokeDone());
    nearby.source.reset(); focused.frames=0; auto fake=std::make_unique<FakeSource>(); fake->living=false; focused.source=std::move(fake);
    nearby.frames=1; assert(s.smokeDone());
}
// A closed or failed source takes the frame texture it owned; a CPU-uploaded image stays.
void staleTexture(View& v) {
    auto& w=v.canvas->windows[0];
    auto dead=std::make_unique<FakeSource>(); dead->living=false;
    w.source=std::move(dead); w.frame.texture=4242; w.width=640; w.height=360;
    v.updateWindowCaptures();
    assert(!w.source && w.closed && w.frame.texture==0 && w.width==0 && w.captureStatus=="closed");
    w.source=std::make_unique<FakeSource>(); w.frame.texture=0; w.width=640;
    canvas::Scene::dropSource(w);
    assert(!w.source && w.width==640);
    w.closed=false; w.captureStatus.clear();
}
// canvas.tsv hot reload: a new radius rebuilds the ring with the eye inside it, an exclusion drops
// live windows at once, the fps caps the governor, and an invalid file keeps the settings.
void reloadSettings(View& v) {
    const std::string excluded="exclude "+std::to_string(1000+0x5003)+"\n";
    write(v.layoutPath, "# canvas v1 30 2.0 60\n"+excluded);
    v.nextCanvasCheck=0; v.reloadLayout();
    assert(v.canvas->ring.radius==2.f && v.canvas->settings.fps==30 && v.canvas->governor.maxHz==30 && !v.canvasRejected);
    assert(!v.canvas->find("0x5003") && v.canvas->find("0x5004"));
    panInsideRing(v); ease(v, 120); panInsideRing(v);
    v.navigate({View::Verb::Fit}); ease(v, 120); panInsideRing(v);
    v.canvas->schedule(false, monotonicSeconds());
    for (const auto& w:v.canvas->windows) assert(w.decision.rateHz<=30);
    write(v.layoutPath, "# canvas v1 60 nonsense\n");
    v.nextCanvasCheck=0; v.reloadLayout();
    assert(v.canvasRejected && v.canvas->ring.radius==2.f && v.canvas->settings.excludes.size()==1);
    write(v.layoutPath, "# canvas v1 60 2.4\n");
    v.nextCanvasCheck=0; v.reloadLayout();
    assert(!v.canvasRejected && v.canvas->ring.radius==2.4f && v.canvas->find("0x5003"));
    panInsideRing(v);
}
// M4 navigation. Helpers: a mailbox's whitespace fields, landing on a window with the View's follow-up,
// the prompt's .search line (answering the current .prompt) and a pose socket datagram.
std::vector<std::string> fieldsOf(const std::string& path) {
    AsyncFile::instance().flush();
    std::istringstream in(readFile(path)); std::vector<std::string> out; std::string f;
    while (in>>f) out.push_back(f);
    return out;
}
std::vector<std::string> promptFields(const View& v) {
    const auto f=fieldsOf(mailbox(v)+".prompt");
    assert(f.size()==6 && f[0]=="v1" && f[1]==std::to_string(getpid()));
    return f;
}
void landOn(View& v, const std::string& name) {
    v.applyAim(v.canvas->land(name, v.baseView())); v.afterCanvasVerb();
    ease(v, 120); v.interactionUntil=0;
    assert(v.canvas->landed==name);
}
void typeSearch(View& v, unsigned long long edit, const std::string& text, const std::string& key, bool open=true) {
    std::ostringstream line;
    line << "v1 " << getpid() << ' ' << promptFields(v)[2] << ' ' << edit << ' ' << windows::encodeHex(text) << ' ' << (open ? 1 : 0) << ' ' << key << ' ' << bootNow() << '\n';
    write(mailbox(v)+".search", line.str()); v.steer();
}
using State=canvas::Scene::State;
// (n) The prompt's query moves the camera to the best match above the palette; Enter lands on it
// (explicit focus) whatever the gaze rests on, and the prompt closes.
void searchLandingBeatsGaze(View& v) {
    work(v); landOn(v, "0x5005");
    v.navigate({View::Verb::Search});
    assert(v.canvas->state==State::Search && v.canvas->search.open && v.promptShown && !v.promptSdl);
    const auto opened=promptFields(v);
    assert(opened[3]=="1" && opened[4]=="OMXRTEST-canvas");
    typeSearch(v, 1, "term 20487", "-");   // the title of 0x5007
    const auto& s=v.canvas->search; const auto* w=v.canvas->find("0x5007");
    assert(s.query=="term 20487" && !s.results.empty() && s.results.front().name=="0x5007" && v.canvas->selectedResult()=="0x5007");
    assert(std::abs(v.canvas->ring.wrap(v.canvas->camera.targetFocusX-w->rect.cx()))<1 && std::abs(v.canvas->camera.targetFocusY-w->rect.cy())<1);
    assert(v.canvas->aimY>w->rect.cy()+.2f*v.canvas->metrics.viewH);
    assert(v.canvas->haloTarget("0x5007")==1 && v.canvas->haloTarget("0x5003")==0 && v.canvas->brightness(*v.canvas->find("0x5003"), 0)<50);
    const auto hit=hitOn(v, "0x5003"); const double start=monotonicSeconds()+1;
    for (double t=start;t<start+3;t+=.05) v.dwellOn(hit, t);
    assert(v.canvas->selectedResult()=="0x5007");
    const auto serial=v.pointerSerial;
    v.gaze.current=hitOn(v, "0x5003");
    typeSearch(v, 2, "term 20487", "enter");
    assert(v.canvas->landed=="0x5007" && v.canvas->state==State::Work && v.hoverOutput=="0x5007" && v.pointerSerial==serial+1);
    assert(!v.canvas->search.open && !v.promptShown);
    const auto closed=promptFields(v);
    assert(closed[3]=="0" && std::stoull(closed[2])==std::stoull(opened[2])+1);
    v.gaze.current.reset(); ease(v, 120);
}
// (o) Esc clears the query first, then closes and restores state, landing and camera exactly.
void escReverts(View& v) {
    work(v); landOn(v, "0x5005");
    const auto camera=v.canvas->camera; const auto rotation=v.targetRotation;
    v.navigate({View::Verb::Search}); v.searchInput("term", "-");
    assert(v.canvas->state==State::Search && v.canvas->search.query=="term");
    v.searchInput("term", "esc");
    assert(v.canvas->search.open && v.canvas->search.query.empty() && v.canvas->state==State::Search);
    v.searchInput("", "esc");
    assert(!v.canvas->search.open && v.canvas->state==State::Work && v.canvas->landed=="0x5005");
    const auto& c=v.canvas->camera;
    assert(c.targetFocusX==camera.targetFocusX && c.targetFocusY==camera.targetFocusY && c.targetZoom==camera.targetZoom);
    assert(sameRotation(rotation, v.targetRotation));
    ease(v, 60);
}
// (o2) The Quickshell prompt's Esc lines: the first clears its text (a plain edit), the second
// reports open 0 with key esc, which closes help first and otherwise reverts; open 0 alone keeps the landing.
void promptEscLines(View& v) {
    work(v); landOn(v, "0x5005");
    const auto camera=v.canvas->camera;
    v.navigate({View::Verb::Search});
    typeSearch(v, 1, "term", "-");
    typeSearch(v, 2, "", "-");
    assert(v.canvas->search.open && v.canvas->search.query.empty());
    typeSearch(v, 3, "", "f1");
    assert(v.canvas->helpOpen);
    typeSearch(v, 4, "", "esc", false);
    assert(!v.canvas->helpOpen && v.canvas->search.open);
    typeSearch(v, 5, "", "esc", false);
    assert(!v.canvas->search.open && v.canvas->state==State::Work && v.canvas->landed=="0x5005");
    assert(v.canvas->camera.targetFocusX==camera.targetFocusX && v.canvas->camera.targetZoom==camera.targetZoom);
    v.navigate({View::Verb::Search});
    typeSearch(v, 1, "term", "-");
    typeSearch(v, 2, "term", "-", false);
    assert(!v.canvas->search.open && v.canvas->landed=="0x5005");
    ease(v, 60);
}
// (o3) Two prompt keys between two polls: the key log replays Down before Enter, and keys already read
// (by editSeq) are not replayed.
void lostKeys(View& v) {
    work(v); landOn(v, "0x5005");
    v.navigate({View::Verb::Search});
    typeSearch(v, 1, "term", "-");
    assert(v.canvas->search.results.size()>3 && v.canvas->search.selected==0);
    const auto second=v.canvas->search.results[1].name;
    typeSearch(v, 3, "term", "down,enter");   // line 2 (down) was replaced before the renderer polled
    assert(v.canvas->landed==second && !v.canvas->search.open);
    ease(v, 60);
    v.navigate({View::Verb::Search});
    typeSearch(v, 1, "term", "-");
    const auto order=v.canvas->search.results;
    typeSearch(v, 2, "term", "down");
    assert(v.canvas->selectedResult()==order[1].name);
    typeSearch(v, 4, "term", "down,down,enter");   // the first down (edit 2) was read already
    assert(v.canvas->landed==order[2].name);
    ease(v, 60);
}
std::vector<std::string> fillFields(const View& v) {
    const auto f=fieldsOf(mailbox(v)+".fill");
    assert(f.size()==7 && f[0]=="v1" && f[1]==std::to_string(getpid()));
    return f;
}
std::string logical(const View& v, float px) { return std::to_string(std::lround(px/v.canvas->settings.outputScale)); }
// (p) Fill: fill size and .fill; untouched toggles restore, moved restores the old size at the new
// centre, resized fills again; flick in fills from Work and flick out restores.
void fillThreeCase(View& v) {
    work(v); landOn(v, "0x5005");
    auto& w=*v.canvas->findMutable("0x5005"); assert(canvas::Scene::onStage(w));
    const auto before=w.rect; const auto f=canvas::fillSize(v.canvas->metrics, v.canvas->settings.outputScale);
    v.navigate({View::Verb::Fill});
    assert(v.canvas->state==State::Fill && v.canvas->filled=="0x5005" && w.rect.w==f.w && w.rect.h==f.h);
    assert(std::abs(v.canvas->ring.wrap(w.rect.cx()-before.cx()))<1 && std::abs(w.rect.cy()-before.cy())<1);
    auto line=fillFields(v); const auto seq=std::stoull(line[2]);
    assert(line[3]=="0x5005" && line[4]==logical(v, f.w) && line[5]==logical(v, f.h));
    ease(v, 120); panInsideRing(v);
    v.navigate({View::Verb::Fill});
    assert(sameRect(w.rect, before) && v.canvas->state==State::Work && !w.beforeFill);
    line=fillFields(v);
    assert(std::stoull(line[2])==seq+1 && line[4]==logical(v, before.w) && line[5]==logical(v, before.h));
    v.navigate({View::Verb::Fill}); v.navigate({.verb=View::Verb::Nudge, .output="right"});
    const auto moved=w.rect; v.navigate({View::Verb::Fill});
    assert(v.canvas->state==State::Work && w.rect.w==before.w && w.rect.h==before.h);
    assert(std::abs(v.canvas->ring.wrap(w.rect.cx()-moved.cx()))<1 && std::abs(w.rect.cy()-moved.cy())<1);
    w.rect=before; v.canvas->refresh(monotonicSeconds());
    // Resized since (a user resize, or a client or Lua clamp that missed the fill size): fill again, and the
    // restore point stays the rect before the first Fill.
    v.navigate({View::Verb::Fill}); w.sourceWidth=1200; w.sourceHeight=700; v.canvas->sizeFromBuffer(w);
    v.navigate({View::Verb::Fill});
    assert(v.canvas->state==State::Fill && w.rect.w==f.w && w.beforeFill && sameRect(*w.beforeFill, before));
    w.sourceWidth=unsigned(f.w)-4; w.sourceHeight=unsigned(f.h); v.canvas->sizeFromBuffer(w);
    v.navigate({View::Verb::Fill});
    assert(v.canvas->state==State::Fill && w.beforeFill && sameRect(*w.beforeFill, before));
    w.sourceWidth=unsigned(f.w); v.canvas->sizeFromBuffer(w);
    v.navigate({View::Verb::Fill});
    assert(v.canvas->state==State::Work && w.rect.w==before.w && w.rect.h==before.h);
    w.rect=before; w.pixelW=unsigned(before.w); w.pixelH=unsigned(before.h); w.sourceWidth=w.sourceHeight=0; w.sized=false;
    v.canvas->refresh(monotonicSeconds());
    v.navigate({View::Verb::FlickIn}); assert(v.canvas->state==State::Fill);
    ease(v, 120); panInsideRing(v);
    v.navigate({View::Verb::FlickOut});
    assert(v.canvas->state==State::Work && sameRect(w.rect, before));
    ease(v, 120); assert(v.monitorMathCalls==0);
}
// (q) Alt-Tab: held steps reveal the list after 0.2 s and never move the camera; the finish lands with
// explicit focus; a tap never reveals; without a release the switcher lands 1.5 s after the last step.
void switcherHold(View& v) {
    work(v); landOn(v, "0x5005");
    auto& s=v.canvas->switcher; const auto rotation=v.targetRotation;
    v.navigate({View::Verb::Switch, 1});
    const size_t n=s.order.size();
    assert(s.active && !s.revealed && s.selected==1 && n>3 && s.order.front()==v.canvas->stagedName);
    s.openedAt-=.1; v.navigate({View::Verb::Switch, 1});
    s.openedAt-=.15; v.navigate({View::Verb::Switch, 1});
    assert(s.revealed && s.selected==3%n && sameRotation(rotation, v.targetRotation) && v.canvas->overlayOpen());
    const auto target=s.order[3%n]; const auto serial=v.pointerSerial;
    v.navigate({.verb=View::Verb::Switch, .begin=true});
    assert(!s.active && !s.revealed && v.canvas->landed==target && v.hoverOutput==target && v.pointerSerial==serial+1);
    ease(v, 60);
    v.navigate({View::Verb::Switch, 1}); assert(!s.revealed);
    const auto tap=s.order[1];
    v.navigate({.verb=View::Verb::Switch, .begin=true});
    assert(v.canvas->landed==tap && !s.revealed);
    ease(v, 60);
    v.navigate({View::Verb::Switch, 1});
    const auto held=s.order[1];
    s.lastStep-=1.6; v.steer();
    assert(!s.active && v.canvas->landed==held);
    ease(v, 60);
}
bool sameLayout(const canvas::Snapshot& a, const canvas::Snapshot& b) {
    return a.rects.size()==b.rects.size() && std::equal(a.rects.begin(), a.rects.end(), b.rects.begin(),
        [](const auto& x, const auto& y) { return x.first==y.first && sameRect(x.second, y.second); });
}
// (r) Arrange in Overview keeps windows in the band; undo and redo are exact.
void arrangeUndo(View& v) {
    work(v); v.navigate({View::Verb::FlickOut}); ease(v, 60);
    assert(v.canvas->zoomedOut());
    const std::pair<const char*, const char*> classes[]={{"0x5001", "firefox"}, {"0x5004", "code"}, {"0x5008", "firefox"}, {"0x5009", "code"}};
    for (const auto& [name, cls]:classes) v.canvas->findMutable(name)->record.cls=cls;
    const auto before=v.canvas->snapshot(); const float headingX=v.canvas->ring.unwrap(v.canvas->aimX);
    (void)headingX;
    v.navigate({View::Verb::Arrange});
    const auto arranged=v.canvas->snapshot();
    assert(!sameLayout(before, arranged) && v.canvas->zoomedOut());
    // This set overflows the ring (900 px tall windows fit row 0 only), so the rest keep their places;
    // arrangeFits checks overlap and grouping on a set that fits.
    ease(v, 120); panInsideRing(v);
    for (const auto& w:v.canvas->windows) assert(w.gone || canvas::inBand(w.rect, v.canvas->metrics));
    v.navigate({View::Verb::Undo}); assert(sameLayout(before, v.canvas->snapshot()));
    ease(v, 60); panInsideRing(v);
    v.navigate({View::Verb::Redo}); assert(sameLayout(arranged, v.canvas->snapshot()));
    ease(v, 60); panInsideRing(v);
    v.navigate({View::Verb::Undo}); assert(sameLayout(before, v.canvas->snapshot()));
    for (const auto& [name, cls]:classes) v.canvas->findMutable(name)->record.cls="foot";
    ease(v, 60); assert(v.monitorMathCalls==0);
}
// (r2) Arrange from Work (Studio, the pose verb, SDL Ctrl+A) packs from the view heading, ends in Overview
// over the new layout and undoes exactly.
void arrangeFromWork(View& v) {
    work(v); landOn(v, "0x5005");
    const auto before=v.canvas->snapshot(); const auto undos=v.canvas->history.undo.size();
    const float headingX=v.canvas->headingPoint().first;
    v.navigate({View::Verb::Arrange});
    assert(v.canvas->state==State::Overview && v.canvas->history.undo.size()==undos+1 && !sameLayout(before, v.canvas->snapshot()));
    float nearest=INFINITY;
    for (const auto& w:v.canvas->windows) if (!w.gone && !w.pinned) nearest=std::min(nearest, std::abs(v.canvas->ring.wrap(w.rect.x-headingX)));
    assert(nearest<=v.canvas->settings.gapPx+1);
    ease(v, 120); panInsideRing(v);
    v.navigate({View::Verb::Undo}); assert(sameLayout(before, v.canvas->snapshot()));
    ease(v, 60); assert(v.monitorMathCalls==0);
}
// A pinned window: out of the cull and hit testing (zero-size layout), Near for the governor at native demand; unpin restores.
void pinned(View& v, const std::string& name) {
    const auto index=size_t(v.canvas->find(name)-v.canvas->windows.data());
    const auto& w=v.canvas->windows[index]; const auto& c=v.canvas->candidates;
    assert(std::find(c.begin(), c.end(), index)!=c.end() && !w.staged);
    v.navigate({.verb=View::Verb::Pin, .output=name});
    assert(w.pinned && v.canvas->pinnedCount()==1 && v.canvas->projected[index].width==0);
    assert(std::find(c.begin(), c.end(), index)==c.end());
    const double t=monotonicSeconds();
    v.canvas->schedule(false, t); v.canvas->schedule(false, t+.6);
    assert(w.decision.tier==governor::Tier::Near);
    // Never projected on the ring, it asks for its native buffer; zoomed out it stays Near.
    auto& demanded=v.canvas->windows[index]; demanded.demandW=demanded.demandH=1;
    v.canvas->schedule(true, t+.7);
    assert(w.decision.tier==governor::Tier::Near && w.demandW==std::max(1u, w.pixelW) && w.demandH==std::max(1u, w.pixelH));
    v.navigate({.verb=View::Verb::Pin, .output=name});
    assert(!w.pinned && v.canvas->projected[index].width>0 && std::find(c.begin(), c.end(), index)!=c.end());
}
// (s) Neighbour is the 45° rule and focuses; nudge moves one snapped step; summon comes to the heading;
// a pinned window leaves targeting and the cull and counts as near.
void neighbourNudgeSummonPin(View& v) {
    work(v); landOn(v, "0x5005");
    const auto expected=canvas::neighbour("0x5005", canvas::Direction::Right, v.canvas->arrangeable(), v.canvas->ring);
    assert(expected);
    auto serial=v.pointerSerial;
    v.navigate({.verb=View::Verb::Neighbour, .output="right"});
    assert(v.canvas->landed==*expected && v.hoverOutput==*expected && v.pointerSerial==serial+1);
    ease(v, 60);
    auto& w=*v.canvas->findMutable(*expected); const auto r0=w.rect;
    canvas::Rect step=canvas::snap(canvas::nudge(r0, canvas::Direction::Right, canvas::nudgeStep)); step.x=v.canvas->ring.unwrap(step.x);
    v.navigate({.verb=View::Verb::Nudge, .output="right"});
    assert(sameRect(w.rect, step) && std::abs(v.canvas->ring.wrap(w.rect.x-r0.x)-canvas::nudgeStep)<=10);
    v.navigate({View::Verb::Undo}); assert(sameRect(w.rect, r0));
    v.navigate({View::Verb::FlickOut}); ease(v, 60);
    const auto* mover=v.canvas->find("0x5003"); auto others=v.canvas->taken();
    std::erase_if(others, [](const auto& p) { return p.name=="0x5003"; });
    float free=-1;
    for (float x=0;x<v.canvas->ring.period() && free<0;x+=100) {
        const auto at=canvas::snap({x-mover->rect.w/2, -mover->rect.h/2, mover->rect.w, mover->rect.h});
        if (canvas::freeAt(at, others, v.canvas->settings.gapPx, v.canvas->ring.period())) free=x;
    }
    assert(free>=0);
    v.canvas->camera.targetFocusX=v.canvas->aimX=free; v.canvas->camera.targetFocusY=v.canvas->aimY=0;
    v.navigate({.verb=View::Verb::Summon, .output="0x5003"});
    const auto& ring=v.canvas->ring;
    const auto& summoned=v.canvas->find("0x5003")->rect;
    assert(v.canvas->landed=="0x5003" && angle(ring.heading(summoned.cx())-ring.heading(free))<1 && canvas::freeAt(summoned, others, 0, ring.period()));
    ease(v, 120); panInsideRing(v);
    pinned(v, "0x5003");
    assert(v.monitorMathCalls==0);
}
// The twelve windows plus a Chrome window, off the canvas or (member) on it.
std::string listWithChrome(unsigned long long seq, bool member) {
    auto text=mailboxFile(seq, twelve());
    text+="0x9100 "+windows::encodeHex("google-chrome")+' '+windows::encodeHex("Docs - Google Chrome")+" 1280 800 20000 0 60 "
        +(member ? "park" : "off")+" 0 4242 0 "+(member ? "1" : "0")+"\n";
    return text;
}
// (t) A window off the canvas is found at x0.7; Enter asks for it (hover v4) and lands once it arrives.
void bringToCanvas(View& v) {
    write(mailbox(v)+".windows", listWithChrome(v.windowsSeq+1, false)); v.steer();
    assert(!v.canvas->find("0x9100"));
    work(v); v.navigate({View::Verb::Search});
    typeSearch(v, 1, "chrome", "-");
    const auto& results=v.canvas->search.results;
    assert(results.size()==1 && results[0].name=="0x9100" && !results[0].canvas);
    const auto serial=v.pointerSerial;
    typeSearch(v, 2, "chrome", "enter");
    assert(v.canvas->bringRequested=="0x9100" && v.hoverOutput=="0x9100" && v.pointerSerial==serial+1 && !v.canvas->search.open);
    assert(v.pointerX==640 && v.pointerY==400);
    assert(v.tick());
    const auto f=hoverFields(v);
    assert(f[3]=="1" && f[4]=="0x9100" && f[7]==std::to_string(serial+1));
    write(mailbox(v)+".windows", listWithChrome(v.windowsSeq+1, true)); v.steer();
    assert(v.canvas->find("0x9100") && v.canvas->landed=="0x9100" && v.canvas->state==State::Work && v.canvas->bringRequested.empty());
    ease(v, 120); panInsideRing(v);
}
void sendPose(View& v, const std::string& packet) {
    const int fd=socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC, 0); assert(fd>=0);
    sockaddr_un address{}; address.sun_family=AF_UNIX; std::memcpy(address.sun_path, v.posePath.c_str(), v.posePath.size()+1);
    assert(sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&address), sizeof(address))==ssize_t(packet.size()));
    close(fd);
    v.tracking.update(); v.steer();
}
// (u) Pose socket verbs route to the scene.
void poseVerbs(View& v) {
    work(v);
    sendPose(v, "search"); assert(v.canvas->state==State::Search && v.promptShown);
    sendPose(v, "arrange"); assert(!v.canvas->history.undo.empty() && v.canvas->state==State::Search);
    sendPose(v, "undo"); assert(!v.canvas->history.redo.empty());
    sendPose(v, "redo"); sendPose(v, "undo");
    sendPose(v, "focus:0x5003");
    assert(v.canvas->landed=="0x5003" && v.hoverOutput=="0x5003" && v.canvas->state==State::Work && !v.promptShown);
    sendPose(v, "fill"); assert(v.canvas->state==State::Fill && v.canvas->landed==v.canvas->stagedName);
    sendPose(v, "fill"); assert(v.canvas->state==State::Work);
    sendPose(v, "pin"); assert(v.canvas->pinnedCount()==1);
    sendPose(v, "pin"); assert(v.canvas->pinnedCount()==0);
    sendPose(v, "help"); assert(v.canvas->helpOpen && v.canvas->overlayOpen());
    sendPose(v, "help"); assert(!v.canvas->helpOpen);
    sendPose(v, "overview"); assert(v.canvas->zoomedOut());
    ease(v, 120); panInsideRing(v); assert(v.monitorMathCalls==0);
}
// (v) canvas.tsv field 9 switches the optional takeovers through the .mode heartbeat.
void takeoverFlag(View& v) {
    const auto heartbeat=[&](const char* flag) {
        write(v.layoutPath, std::string("# canvas v1 60 2.4 60 0.35 0.8 1 300 all ")+flag+"\n");
        v.nextCanvasCheck=0; v.reloadLayout(); assert(!v.canvasRejected);
        v.steer();
        const auto f=fieldsOf(mailbox(v)+".mode");
        assert(f.size()==5 && f[0]=="v1" && f[1]==std::to_string(getpid()) && f[2]=="canvas" && f[3]==flag);
    };
    heartbeat("0"); assert(!v.canvas->settings.takeoverKeys);
    heartbeat("1"); assert(v.canvas->settings.takeoverKeys);
}
// M5: the ladder in the renderer. A fresh View (its own mailboxes), times passed in, every window visible.
struct Ladder {
    View& v;
    void step(double t) { for (auto& w:v.canvas->windows) w.visible=!w.gone; v.scheduleCanvas(t); AsyncFile::instance().flush(); }
    canvas::CanvasWindow& at(const std::string& name) { auto* w=v.canvas->findMutable(name); assert(w); return *w; }
    std::string tiersPath() const { return mailbox(v)+".tiers"; }
    std::optional<windows::Tiers> tiers() const { return windows::parseTiers(readFile(tiersPath())); }
    std::filesystem::file_time_type written() const { return std::filesystem::last_write_time(tiersPath()); }
    void adopt(const std::vector<Row>& rows, double t) { v.canvas->adopt(*windows::parse(mailboxFile(++seq, rows)), t); }
    // Ages the `.tiers` line by 5 s, then runs the next heartbeat (a mode announcement resets its second).
    windows::Tiers heartbeat() {
        const auto before=*tiers();
        write(tiersPath(), windows::tiersLine(getpid(), before.seq, before.slivers, bootNow()-5));
        v.controls->setCanvasMode(true); v.controls->beat(); AsyncFile::instance().flush();
        return *tiers();
    }
    unsigned long long seq=1;
};
std::vector<Row> ladderRows(unsigned count, unsigned w, unsigned h) {
    std::vector<Row> rows{{0xa001, 1920, 1080, 0, "stage"}};
    for (unsigned i=1;i<count;++i) rows.push_back({0xa001u+i, w, h, int(i), "park"});
    return rows;
}
double statsNumber(const std::string& stats, const std::string& key) {
    const auto at=stats.find("\""+key+"\":"); assert(at!=std::string::npos);
    return std::stod(stats.substr(at+key.size()+3));
}
size_t occurrences(const std::string& text, const std::string& token) {
    size_t n=0;
    for (auto at=text.find(token); at!=std::string::npos; at=text.find(token, at+1)) ++n;
    return n;
}
// (a) A window rated above 30 Hz is published as a sliver (seq 1, this pid, a fresh stamp); an unchanged
// set or a change within 500 ms writes nothing; six windows lower its rate at once, the sliver goes after 2 s.
void tiersMailbox(Ladder& l, double t) {
    l.step(t);
    assert(l.at("0xa002").decision.rateHz==10 && !std::filesystem::exists(l.tiersPath()));
    l.step(t+.6);
    assert(l.at("0xa002").decision.rateHz==40 && l.at("0xa002").decision.place==windows::Place::Sliver && l.v.canvas->sliverCount()==1);
    auto tiers=l.tiers();
    assert(tiers && tiers->owner==std::to_string(getpid()) && tiers->seq==1 && std::abs(tiers->stamp-bootNow())<=1);
    assert(tiers->slivers==std::vector<std::uint64_t>{0xa002});
    const auto first=l.written();
    l.step(t+.8); l.v.controls->publishTiers({}, t+.9); AsyncFile::instance().flush();
    assert(l.written()==first && l.tiers()->seq==1);
    l.adopt(ladderRows(6, 1920, 1080), t+1); l.step(t+1);
    l.step(t+1.6);
    assert(l.at("0xa002").decision.tier==governor::Tier::Near && l.at("0xa002").decision.rateHz==15 && l.at("0xa006").decision.rateHz==10);
    assert(l.at("0xa002").decision.place==windows::Place::Sliver && l.tiers()->slivers.size()==1 && l.tiers()->seq==1);
    // The heartbeat keeps a non-empty set fresh for Lua (stale after 2 s): same seq and set, a new stamp.
    const auto beat=l.heartbeat();
    assert(beat.seq==1 && beat.slivers==std::vector<std::uint64_t>{0xa002} && std::abs(beat.stamp-bootNow())<=1);
}
// (b) The stats carry the budget block, and every capture row its place and lanes in flight. The new Near
// windows still wait out the raise delay at 10 Hz (60 + 15 + 3 x 10 + 10 = 238.5 Mpix/s).
void budgetStats(Ladder& l) {
    l.v.workTimes={1}; l.v.frameTimes={1};
    l.v.report(monotonicSeconds()); AsyncFile::instance().flush();
    const auto stats=readFile(l.v.posePath+".stats");
    assert(stats.find("\"budget\":")!=std::string::npos && stats.find("\"setMpix\":300,\"effectiveMpix\":300,")!=std::string::npos);
    const double used=statsNumber(stats, "usedMpix");
    assert(used>238 && used<=300 && statsNumber(stats, "slivers")==1 && statsNumber(stats, "calibration")==1 && statsNumber(stats, "vramMB")>0);
    assert(occurrences(stats, "\"output\":")==6 && occurrences(stats, "\"place\":")==6 && occurrences(stats, "\"inFlight\":")==6);
    assert(stats.find("\"place\":\"stage\",\"inFlight\":2")!=std::string::npos && occurrences(stats, "\"place\":\"sliver\"")==1);
}
void sliverLeaves(Ladder& l, double t) {
    l.step(t+2.5);
    assert(l.at("0xa002").decision.place==windows::Place::Sliver);
    l.step(t+2.7);
    assert(l.at("0xa002").decision.place==windows::Place::Park && l.tiers()->seq==2 && l.tiers()->slivers.empty());
    // An empty set is left to go stale: the heartbeat does not rewrite it.
    const auto beat=l.heartbeat();
    assert(beat.seq==2 && beat.slivers.empty() && bootNow()-beat.stamp>=4);
}
// (c) Request→ready samples of an export over two output frames shrink the budget by 10 % and a tier
// drops a step at once; the staged window's samples do not count; calm samples grow it back after 5 s.
void readyCalibration(Ladder& l, double t) {
    l.adopt(ladderRows(11, 1280, 720), t); l.step(t); l.step(t+.6); l.step(t+2.1);
    auto& far=l.at("0xa00b"); auto& staged=l.at("0xa001");
    assert(l.at("0xa002").decision.rateHz==30 && far.decision.rateHz==10 && std::abs(l.v.canvas->governor.usedMpix-290.3)<.1);
    auto slow=std::make_unique<FakeSource>(), stage=std::make_unique<FakeSource>();
    slow->ready=50; stage->ready=500;
    far.source=std::move(slow); staged.source=std::move(stage);
    auto& budget=l.v.canvas->governor.budget;
    l.v.canvas->noteReady(staged, t+2.1); assert(budget.ready.empty());
    // A damage-driven sliver waits on its client's commits, so it is not sampled either.
    far.decision.ignoreDamage=false; l.v.canvas->noteReady(far, t+2.1); assert(budget.ready.empty());
    far.decision.ignoreDamage=true;
    for (int i=0;i<=21;++i) l.v.canvas->noteReady(far, t+2.2+i*.1);   // two slow seconds in a row
    assert(budget.readyP50Ms==50 && std::abs(budget.effective()-270)<1e-9);
    l.step(t+3.4);
    assert(far.decision.rateHz==6 && l.at("0xa002").decision.rateHz==30 && l.v.canvas->governor.usedMpix<=270);
    static_cast<FakeSource*>(far.source.get())->ready=10;
    // Evaluations every ~1 s from t+4.3; the first growth comes 5 s after the calm began (≈ t+8.3).
    double now=t+3.5;
    for (;now<t+7.75;now+=.1) l.v.canvas->noteReady(far, now);
    assert(budget.calibration==.9 && budget.readyP50Ms==10);
    for (;now<t+9.75;now+=.1) l.v.canvas->noteReady(far, now);
    assert(budget.calibration>.9);
    for (;now<t+16;now+=.1) l.v.canvas->noteReady(far, now);
    assert(budget.calibration==1 && budget.effective()==300);
    far.source.reset(); staged.source.reset();
}
// GPU p80 over 60 % of the period takes 25 % steps, over 85 % panics every non-focused window to 6 Hz;
// a calm GPU steps back one per 2 s.
void gpuFeedback(Ladder& l, double t) {
    const double period=1000.0/60;
    auto& budget=l.v.canvas->governor.budget;
    double now=t;
    for (int i=0;i<45;++i, now+=1./60) l.v.canvas->noteGpu(12, period, now);
    assert(budget.gpuSteps==2 && !budget.panic && std::abs(budget.effective()-300*.75*.75)<1e-9);
    for (int i=0;i<30;++i, now+=1./60) l.v.canvas->noteGpu(15, period, now);
    assert(budget.panic);
    l.step(now);
    for (const auto& w:l.v.canvas->windows) assert(w.staged ? w.decision.rateHz==60 : w.decision.rateHz<=6);
    for (int i=0;i<60*15 && (budget.gpuSteps || budget.panic);++i, now+=1./60) l.v.canvas->noteGpu(2, period, now);
    assert(!budget.gpuSteps && !budget.panic && budget.effective()==300);
}
// (d) A closed window leaves the ladder's maps with its release: nothing stale is raised if it returns.
void closedForgotten(Ladder& l, double t) {
    auto& g=l.v.canvas->governor;
    l.step(t-.1);
    assert(g.granted.count("0xa002") && g.movedAt.count("0xa002"));
    auto rows=ladderRows(11, 1280, 720); rows.erase(rows.begin()+1);
    l.adopt(rows, t); l.v.canvas->tick(t+.25, 0);
    assert(!l.v.canvas->find("0xa002") && !g.granted.count("0xa002") && !g.movedAt.count("0xa002") && !g.near.count("0xa002"));
    l.step(t+.3);
    assert(!g.granted.count("0xa002"));
}
void ladder(SDL_Window* window, const std::string& temp) {
    const std::string dir=temp+"/ladder", pose=dir+"/pose.sock", canvasPath=dir+"/canvas.tsv", empty;
    std::filesystem::create_directories(dir);
    {
    std::vector<Panel> none;
    View v(none,false,spatial::Workspace{80},24,empty,pose,false,false,64,28,canvasPath,60,false);
    v.window=window; v.mode=View::SceneMode::Canvas;
    v.canvas=std::make_unique<canvas::Scene>(canvas::Ring{}, canvas::Settings{}, "", true);
    v.environment=std::make_unique<SkyEnvironment>("");
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v.maxTexture);
    v.primeCamera(); v.startCanvas();
    write(mailbox(v)+".windows", mailboxFile(1, ladderRows(2, 1280, 720)));
    v.steer();
    assert(v.canvas->stagedName=="0xa001" && !v.canvas->zoomedOut());
    Ladder l{v};
    const double t=monotonicSeconds();
    tiersMailbox(l, t); budgetStats(l); sliverLeaves(l, t);
    readyCalibration(l, t+10); gpuFeedback(l, t+30); closedForgotten(l, t+50);
    v.finishCanvas();
    }
    AsyncFile::instance().flush();
    assert(!std::filesystem::exists(pose+".controls.tiers"));   // unlinked with the controls
}
}

int main() {
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    SDL_Window* window=SDL_CreateWindow("XR canvas test",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    assert(window);
    auto context=SDL_GL_CreateContext(window); assert(context);
    char temp[]="/tmp/xr-canvas-focus-XXXXXX"; assert(mkdtemp(temp));
    const std::string pose=std::string(temp)+"/pose.sock", canvasPath=std::string(temp)+"/canvas.tsv", empty;
    {
        std::vector<Panel> none;
        View v(none,false,spatial::Workspace{80},24,empty,pose,false,false,64,28,canvasPath,60,false);
        v.window=window; v.mode=View::SceneMode::Canvas;
        v.canvas=std::make_unique<canvas::Scene>(canvas::Ring{}, canvas::Settings{}, "", true);
        v.windowsPath=std::string(temp)+"/windows.tsv"; write(v.windowsPath, windowsFile(1, twelve()));
        v.environment=std::make_unique<SkyEnvironment>("");
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v.maxTexture);
        v.primeCamera(); v.startCanvas();
        placedAndLanded(v); verbs(v); fadeAndPlace(v); reloadFile(v);
        navigationInvariants(v); workAndOverview(v); zoomAnchor(v); focusFollow(v); dwellInWork(v);
        staleTexture(v); reloadSettings(v);
        mailboxList(v); hoverV4(v); virtualCursor(v); clickPath(v); stageSourceFallback(v); stageRegion(v);
        searchLandingBeatsGaze(v); escReverts(v); promptEscLines(v); lostKeys(v); fillThreeCase(v); switcherHold(v); arrangeUndo(v); arrangeFromWork(v); neighbourNudgeSummonPin(v);
        bringToCanvas(v); poseVerbs(v); takeoverFlag(v);
        assert(v.monitorMathCalls==0);
        v.finishCanvas();
    }
    firstMailboxLands(window, temp); ladder(window, temp);
    sceneMemory(temp); sizeAndParent(); noFreePlace(); smokeRule(); versionGate(temp); arrangeFits(); paletteClear();
    AsyncFile::instance().flush(); std::filesystem::remove_all(temp);
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    std::cout << "Canvas focus: placement without overlap on 3 rows, landing on the staged window, Fit toggle, routed verbs without monitor math, eye inside the ring, fade-out, the window file reload, navigation invariants, Work/Overview, the zoom anchor, focus follow, dwell in Work, stale textures, canvas.tsv reload, the .windows mailbox, hover v4, the virtual cursor, the click path, the region source fallback and rectangle, XR staging without a camera move, the first mailbox landing, memory, buffer size, dialogs, the full-ring fallback, the smoke rule, the controls version gate, search landing over gaze, Esc revert, the prompt's Esc lines, the prompt key log, the Fill three-case restore, the Alt-Tab switcher, arrange with undo/redo, from Work and on a canvas that fits, the palette clearing the selection, neighbour/nudge/summon/pin, bring to canvas, pose verbs, the takeover flag, the .tiers mailbox with its 500 ms limit and 2 s place hold, the budget stats, request->ready calibration, GPU feedback and closed windows leaving the ladder passed\n";
}
