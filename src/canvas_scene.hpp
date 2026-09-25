#pragma once
#include "camera_controls.hpp"
#include "canvas_labels.hpp"
#include "canvas_memory.hpp"
#include "canvas_model.hpp"
#include "canvas_placement.hpp"
#include "capture_governor.hpp"
#include "capture_plan.hpp"
#include "frame_source.hpp"
#include "gl_texture.hpp"
#include "window_capture.hpp"
#include "window_list.hpp"
#include <SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The window canvas scene (docs/infinite-canvas-plan.md §4.1-§4.4): every canvas window as its own
// quad on the ring. Placement, projection and rates come from the pure headers; the scene owns the
// windows, their captures and textures, and the Work/Overview camera. The View draws and aims it.
namespace canvas {
constexpr double fadeSeconds=.2;   // closed windows fade out, then leave
constexpr size_t maxOccluders=24;  // bounds the notification berth search
struct CanvasWindow {
    windows::Record record;
    std::string name;
    Rect rect;                     // canvas px; the buffer size once a frame arrived
    unsigned pixelW=0, pixelH=0;
    std::unique_ptr<FrameSource> source;
    CapturedFrame frame;
    GLuint texture=0;
    unsigned width=0, height=0, sourceWidth=0, sourceHeight=0, cpuWidth=0, cpuHeight=0, frames=0, reportFrames=0;
    unsigned demandW=1, demandH=1;
    std::string captureStatus;
    double retryAt=0, goneAt=0, lastFrame=0;
    int retryMs=500;
    float halo=0;
    bool visible=false, candidate=false, staged=false, gone=false, closed=false, sized=false;
    adaptive::Quality quality;
    governor::Decision decision;
};
inline const char* tierName(governor::Tier tier) {
    switch(tier) {
    case governor::Tier::Focused: return "focused";
    case governor::Tier::Near: return "near";
    case governor::Tier::Far: return "far";
    case governor::Tier::Overview: return "overview";
    default: return "idle";
    }
}
class Scene {
public:
    Ring ring;
    Settings settings;
    Metrics metrics;
    Camera camera;
    enum class State { Work, Overview } state=State::Overview;
    std::vector<CanvasWindow> windows;
    std::vector<PanelLayout> projected;   // windows[i] projected by the camera, output = address
    std::vector<size_t> candidates;       // angular cull of projected around the last heading
    Memory memory;
    windows::List lastList;               // the last adopted list, re-adopted when exclusions change
    std::unique_ptr<WindowCaptureHub> hub;
    governor::Fixed governor;
    std::string stagedName, gazed, memoryPath;
    // Navigation: the landed window, Hyprland's focused one, the View's selection and last settled dwell.
    std::string landed, focusedName, selected, lastDwell;
    Fov fov;
    Labels labels;
    tracking::Quaternion panAnchor;
    bool offline=false;
    float heading=0, halfSpan=180;
    float depth=2.4f, aimX=0, aimY=0, latchX=0, latchY=0;   // eye depth and aimed point (projected px)
    double lastDwellAt=-1e9, lastZoomAt=-1e9;
    double hubRetryAt=0;
    int hubRetryMs=500;
    bool rememberCamera=false;   // set once the first view is chosen, so the default camera never overwrites the saved one

    // offline: no compositor connection and no captures (tests).
    Scene(Ring r, Settings s, std::string statePath, bool noCapture=false)
        : ring(r), settings(std::move(s)), memoryPath(std::move(statePath)), offline(noCapture) {
        metrics=canvas::metrics(ring, Fov{}); depth=ring.radius;
        governor.budgetMpix=settings.captureBudgetMpix; governor.maxHz=unsigned(settings.fps);
        if(!memoryPath.empty()) memory.load(memoryPath);
    }
    bool connect(std::string& error) {
        if(offline) return true;
        hub=std::make_unique<WindowCaptureHub>();
        if(hub->connect(error)) return true;
        hub.reset(); return false;
    }
    void setFov(const Fov& next) { fov=next; metrics=canvas::metrics(ring, fov); }
    // Radius or gap changes rebuild the ring; windows keep their canvas positions. New exclusions
    // apply to the last list at once.
    bool applySettings(Settings next, double now=0) {
        const bool ringChanged=next.radius!=settings.radius || next.gapPx!=settings.gapPx, excludesChanged=next.excludes!=settings.excludes;
        settings=std::move(next); governor.budgetMpix=settings.captureBudgetMpix; governor.maxHz=unsigned(settings.fps);
        if(ringChanged) { ring.radius=settings.radius; depth=std::min(depth, ring.radius); for(auto& w:windows) w.rect.x=ring.unwrap(w.rect.x); refresh(now); }
        if(excludesChanged) adopt(lastList, now);
        return ringChanged;
    }
    Cylinder cylinder() const { return canvas::cylinder(ring, settings.gapPx); }
    bool adoptable(const windows::Record& r) const {
        return r.canvas && std::none_of(settings.excludes.begin(), settings.excludes.end(),
            [&](const std::string& token) { return token==r.cls || token==std::to_string(r.pid); });
    }
    // Diffed by address like replacePanels: existing windows stay, new ones are placed, missing ones fade.
    bool adopt(const windows::List& list, double now) {
        const std::string before=stagedName;
        if(&list!=&lastList) lastList=list;
        std::vector<const windows::Record*> incoming;
        for(const auto& r:list.records) if(adoptable(r)) incoming.push_back(&r);
        for(auto& w:windows) {
            const bool listed=std::any_of(incoming.begin(), incoming.end(), [&](const auto* r) { return r->address==w.record.address; });
            if(!listed && !w.gone) { w.gone=true; w.goneAt=now; }
        }
        for(const auto* r:incoming) {
            auto it=std::find_if(windows.begin(), windows.end(), [&](const CanvasWindow& w) { return w.record.address==r->address; });
            if(it==windows.end()) add(*r, now);
            else update(*it, *r);
        }
        chooseStaged(incoming);
        refresh(now);
        return stagedName!=before;
    }
    unsigned scaled(unsigned logical) const { return std::max(1u, unsigned(std::lround(logical*settings.outputScale))); }
    void update(CanvasWindow& w, const windows::Record& r) {
        w.gone=false; w.record=r;
        // The buffer is the truth during a resize; the list size only stands in until a frame arrives.
        w.pixelW=w.sized ? w.pixelW : scaled(r.w); w.pixelH=w.sized ? w.pixelH : scaled(r.h);
        if(!w.sized) { w.rect.w=float(w.pixelW); w.rect.h=float(w.pixelH); }
    }
    void add(const windows::Record& r, double now) {
        CanvasWindow w; w.record=r; w.name=r.name(); w.pixelW=scaled(r.w); w.pixelH=scaled(r.h);
        w.rect=place(r, float(w.pixelW), float(w.pixelH), now);
        memory.note(r.cls, r.title, w.rect, now);
        windows.push_back(std::move(w));
    }
    std::vector<Placed> taken() const {
        std::vector<Placed> out;
        for(const auto& w:windows) if(!w.gone) out.push_back({w.name, w.rect, w.record.pid, w.record.cls});
        return out;
    }
    // A floating window of an app that already has a tiled window starts centred on it (a dialog).
    std::optional<Rect> parentOf(const windows::Record& r) const {
        if(!r.floating) return {};
        for(const auto& w:windows) if(!w.gone && !w.record.floating && w.record.pid==r.pid && r.pid>0) return w.rect;
        return {};
    }
    // Session memory, then the parent rule, then a ring search from the camera focus.
    Rect place(const windows::Record& r, float w, float h, double now) {
        const auto others=taken();
        float startX=camera.targetFocusX, startY=camera.targetFocusY;
        if(auto claimed=memory.claim(r.cls, r.title, now)) {
            Rect at{ring.unwrap(claimed->x), claimed->y, w, h};
            if(inBand(at, metrics) && freeAt(at, others, settings.gapPx, ring.period())) return at;
            startX=at.cx(); startY=at.cy();
        }
        if(auto found=placeNew(w, h, startX, startY, others, ring, metrics, settings.gapPx, parentOf(r))) return *found;
        // The 8-direction search reaches the outer rows only one step out; walk each row's centre line.
        for(const float row:{0.f, metrics.rowHeight, -metrics.rowHeight})
            if(auto found=placeNew(w, h, startX, row, others, ring, metrics, settings.gapPx)) return *found;
        std::cerr << "Canvas: no free place for " << r.name() << "; overlapping at the focus" << std::endl;
        Rect fallback=canvas::snap({startX-w/2, startY-h/2, w, h}); fallback.x=ring.unwrap(fallback.x);
        return fallback;
    }
    // The staged window is the one Hyprland hosts on the stage, else the most recently focused.
    void chooseStaged(const std::vector<const windows::Record*>& incoming) {
        const windows::Record *pick=nullptr, *focused=nullptr;
        for(const auto* r:incoming) if(r->place==windows::Place::Stage) { pick=r; break; }
        for(const auto* r:incoming) if(r->focusHistoryID==0) { focused=r; break; }
        if(!pick) pick=focused;
        stagedName=pick ? pick->name() : std::string();
        focusedName=focused ? focused->name() : std::string();
        for(auto& w:windows) w.staged=!w.gone && w.name==stagedName;
    }
    // The first imported frame, and every resize after it, sets the size.
    void sizeFromBuffer(CanvasWindow& w) {
        if(!w.sourceWidth || !w.sourceHeight) return;
        w.sized=true;
        if(w.pixelW==w.sourceWidth && w.pixelH==w.sourceHeight) return;
        w.pixelW=w.sourceWidth; w.pixelH=w.sourceHeight; w.rect.w=float(w.pixelW); w.rect.h=float(w.pixelH);
    }
    void tick(double now, float dt) {
        camera.tick(dt, ring.period());
        holdAnchor(now);
        const auto expired=[&](const CanvasWindow& w) { return w.gone && now-w.goneAt>=fadeSeconds; };
        for(auto& w:windows) if(expired(w)) release(w, now);
        std::erase_if(windows, expired);
        noteCamera(now);
        if(!memoryPath.empty()) memory.flush(memoryPath, now);
        refresh(now);
    }
    // The frame's texture belongs to the source's capture slots: it goes with the source. The window
    // keeps its own texture only when the last frame was a CPU upload; otherwise it shows its status.
    static void dropSource(CanvasWindow& w) {
        if(w.frame.texture) { w.frame.texture=0; w.width=w.height=0; }
        w.source.reset();
    }
    // A closed window leaves its textures and frees its memory slot for the next claim.
    void release(CanvasWindow& w, double now) {
        if(w.texture) glDeleteTextures(1, &w.texture);
        w.texture=0; w.frame.texture=0; w.source.reset(); governor.forget(w.name);
        for(auto& e:memory.entries) if(e.cls==w.record.cls && e.title==w.record.title) { e.claimed=false; e.seen=now; e.rect=w.rect; }
        memory.touch(now);
    }
    float brightness(const CanvasWindow& w, double now) const {
        if(w.gone) return std::clamp(float(100*(1-(now-w.goneAt)/fadeSeconds)), 1.f, 100.f);
        return state==State::Overview && gazed!=w.name && selected!=w.name ? 80.f : 100.f;
    }
    // Projected layouts and the cull are rebuilt together, so candidate indices always match windows.
    void refresh(double now) {
        projected.clear(); projected.reserve(windows.size());
        for(const auto& w:windows) projected.push_back(toLayout(w.name, project(w.rect, camera, ring), brightness(w, now)));
        cull(heading, halfSpan);
    }
    const std::vector<PanelLayout>& geometry() const { return projected; }
    // A 10° margin keeps halos and edge windows from popping at the view border.
    void cull(float headingDeg, float halfSpanDeg) {
        heading=headingDeg; halfSpan=halfSpanDeg;
        candidates=visibleIndices(projected, heading, halfSpan+10, ring);
        for(auto& w:windows) w.candidate=false;
        for(auto i:candidates) windows[i].candidate=true;
    }
    SurfaceView surfaceView(size_t i) const {
        const auto& w=windows[i];
        return {&projected[i], w.frame.texture ? w.frame.texture : w.texture, w.gone ? 0u : w.width, w.height, w.sourceWidth, w.sourceHeight, &w.captureStatus, w.halo, w.visible, 1, {}};
    }
    template<class F> void forEachSurface(F&& f) const { for(size_t i=0;i<windows.size();++i) f(surfaceView(i)); }
    template<class F> void forEachCandidate(F&& f) const { for(auto i:candidates) f(surfaceView(i)); }
    // The fixed S1b governor over every window; visible and demand come from the View's projection.
    void schedule(bool zoomedOut, double now) {
        std::vector<governor::Input> in; in.reserve(windows.size());
        for(size_t i=0;i<windows.size();++i) {
            const auto& w=windows[i];
            in.push_back({w.name, w.staged && !w.gone, w.visible, projected[i].width/ring.pxPerDeg(), w.record.focusHistoryID, w.pixelW, w.pixelH});
        }
        const auto decisions=governor.plan(in, zoomedOut, now);
        for(size_t i=0;i<windows.size();++i) {
            auto& w=windows[i]; const auto& d=decisions[i];
            w.decision=d;
            if(!w.source) continue;
            w.source->setFrameRate(d.rateHz, d.inFlight, d.phase);
            w.source->setIgnoreDamage(d.ignoreDamage);
            w.source->setDemand(w.visible && d.rateHz>0, w.demandW, w.demandH);
        }
    }
    void openSources(double now) {
        if(offline || !hub || !hub->error().empty()) return;
        for(auto& w:windows) {
            if(w.source || w.gone || w.closed || now<w.retryAt) continue;
            if(!w.texture) w.texture=gltex::create();
            w.source=hub->open(w.record.address);
            if(w.source) { w.captureStatus.clear(); w.retryAt=0; w.retryMs=500; continue; }
            w.captureStatus=hub->error().empty() ? "capture unavailable" : hub->error();
            w.retryAt=now+w.retryMs/1000.0; w.retryMs=FrameSource::nextRetryMs(w.retryMs);
        }
    }
    // A lost compositor connection takes every window with it: a new hub, then sources reopen.
    void recoverHub(double now) {
        if(offline || (hub && hub->error().empty()) || now<hubRetryAt) return;
        if(hub) std::cerr << "Window capture: " << hub->error() << "; reconnecting" << std::endl;
        for(auto& w:windows) { dropSource(w); w.retryAt=0; w.retryMs=500; }
        std::string error;
        if(connect(error)) { hubRetryMs=500; return; }
        for(auto& w:windows) if(!w.closed) w.captureStatus=error;
        hubRetryAt=now+hubRetryMs/1000.0; hubRetryMs=FrameSource::nextRetryMs(hubRetryMs);
    }
    void pump() { if(hub) hub->pump(); }
    void settle(double maxSeconds) { if(hub) hub->settle(maxSeconds); }
    // Lease loss: the GL context goes, so textures, capture slots and the hub's device go with it.
    void releaseGpu() {
        for(auto& w:windows) {
            if(w.texture) glDeleteTextures(1, &w.texture);
            w.texture=0; w.frame.texture=0; w.cpuWidth=w.cpuHeight=0; w.source.reset();
        }
        labels.release(); hub.reset();
    }
    void regenerate(double now) {
        for(auto& w:windows) {
            w.texture=gltex::create(); w.retryAt=now; w.retryMs=500;
            if(!w.closed) w.captureStatus="reconnecting after lease";
        }
        hubRetryAt=0; hubRetryMs=500;
    }
    // The candidates nearest the heading, for the notification berth search.
    std::vector<PanelLayout> occluders() const {
        auto order=candidates;
        const auto offset=[&](size_t i) {
            const auto& p=projected[i];
            return std::abs(std::remainder(ring.heading(p.x+p.width/2)-heading, 360.f));
        };
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return offset(a)<offset(b); });
        if(order.size()>maxOccluders) order.resize(maxOccluders);
        std::vector<PanelLayout> out;
        for(auto i:order) out.push_back(projected[i]);
        return out;
    }
    const CanvasWindow* find(const std::string& name) const {
        const auto it=std::find_if(windows.begin(), windows.end(), [&](const CanvasWindow& w) { return !w.gone && w.name==name; });
        return it==windows.end() ? nullptr : &*it;
    }
    const CanvasWindow* staged() const { return stagedName.empty() ? nullptr : find(stagedName); }
    // Navigation (§5.2-§5.7). A verb sets the canvas camera targets and returns where the eye aims,
    // or nothing when the view stays put; the View eases to it like every other aim.
    using Aim=std::optional<navigation::FrontFocus>;
    // A one-pixel layout at the projected point, faced from depth. The eye stays within R - 0.3 of the
    // ring centre with the vertical offset of the upper and lower rows included.
    navigation::FrontFocus aim(float x, float y, float d, const tracking::Quaternion& anchor) {
        const float R=ring.radius, reach=R-.3f, up=y/900;
        const float least=R-std::sqrt(std::max(reach*reach-up*up, 0.f));
        aimX=x; aimY=y; depth=std::clamp(d, std::max(.3f, least), R);
        const PanelLayout dot{"", x-.5f, y-.5f, 1, 1};
        return navigation::frontFocus(cylinder().pose(dot), anchor, depth);
    }
    Aim reaim(const tracking::Quaternion& anchor) { return aim(aimX, aimY, depth, anchor); }
    Rect targetView(const Rect& r) const {
        Camera c=camera; c.focusX=c.targetFocusX; c.focusY=c.targetFocusY; c.zoom=c.targetZoom;
        return project(r, c, ring);
    }
    std::vector<Rect> liveRects() const {
        std::vector<Rect> rects;
        for(const auto& w:windows) if(!w.gone) rects.push_back(w.rect);
        return rects;
    }
    void resetGesture() { camera.anchored=false; lastZoomAt=-1e9; }
    // Work on one window: centred at its Work zoom, the eye at the depth where it fills the view.
    Aim land(const std::string& name, const tracking::Quaternion& anchor) {
        const auto* w=find(name);
        if(!w) return {};
        state=State::Work; resetGesture(); landed=name;
        const float zoom=workZoom(w->rect, metrics);
        camera.targetZoom=zoom; camera.targetFocusX=ring.unwrap(w->rect.cx()); camera.targetFocusY=w->rect.cy();
        return aim(camera.targetFocusX, camera.targetFocusY, fitDepth(w->rect.w*zoom, w->rect.h*zoom), anchor);
    }
    // rectDistance for a flat rectangle, then deep enough that the curved window's arc fits the
    // horizontal view from inside the ring (its edges bend towards the eye).
    float fitDepth(float w, float h) const {
        const float R=ring.radius, half=fov.horizontal()*spatial::pi/360, arc=1.04f*w/1800;
        float lo=std::clamp(navigation::rectDistance(w, h, fov.vertical, fov.aspect), .3f, R), hi=R;
        if(navigation::visibleArc(lo, half, 1/R)>=arc) return lo;
        for(int i=0;i<24;++i) { const float mid=(lo+hi)/2; (navigation::visibleArc(mid, half, 1/R)>=arc ? hi : lo)=mid; }
        return hi;
    }
    // fitBounds fills the view edge to edge; a 4 % margin per side keeps edges and labels inside.
    Aim overview(const tracking::Quaternion& anchor) {
        const auto fit=fitBounds(liveRects(), ring, metrics);
        state=State::Overview; resetGesture();
        camera.targetFocusX=fit.focusX; camera.targetFocusY=fit.focusY; camera.targetZoom=std::max(.08f, .92f*fit.zoom);
        return aim(fit.focusX, fit.focusY, ring.radius, anchor);
    }
    // The settled camera is remembered (the canvas-memory.tsv camera row); without a staged window
    // the next start returns to it in Overview, else to the fitted overview.
    void noteCamera(double now) {
        if(!rememberCamera || camera.moving()) return;
        const Fit settled{ring.unwrap(camera.targetFocusX), camera.targetFocusY, std::clamp(camera.targetZoom, 1e-3f, 1.f)};
        const auto& saved=memory.camera;
        if(saved && std::abs(saved->focusX-settled.focusX)<.5f && std::abs(saved->focusY-settled.focusY)<.5f && std::abs(saved->zoom-settled.zoom)<1e-4f) return;
        memory.noteCamera(settled, now);
    }
    Aim restoreCamera(const tracking::Quaternion& anchor) {
        if(!memory.camera) return overview(anchor);
        state=State::Overview; resetGesture();
        camera.targetFocusX=ring.unwrap(memory.camera->focusX); camera.targetFocusY=memory.camera->focusY; camera.targetZoom=memory.camera->zoom;
        return aim(camera.targetFocusX, camera.targetFocusY, ring.radius, anchor);
    }
    void noteDwell(const std::string& name, double now) { lastDwell=name; lastDwellAt=now; }
    // The window a dwell settled on within 2 s, else the staged one, else the most recently focused.
    std::string landingTarget(double now) const {
        if(now-lastDwellAt<=2 && find(lastDwell)) return lastDwell;
        if(find(stagedName)) return stagedName;
        const CanvasWindow* best=nullptr;
        for(const auto& w:windows) if(!w.gone && (!best || w.record.focusHistoryID<best->record.focusHistoryID)) best=&w;
        return best ? best->name : std::string();
    }
    Aim toggleOverview(const tracking::Quaternion& anchor, double now) {
        return state==State::Overview ? land(landingTarget(now), anchor) : overview(anchor);
    }
    // Flick out: Work -> Overview. Flick in: Overview -> Work; Work -> Fill arrives in M4.
    Aim flickOut(const tracking::Quaternion& anchor) { return state==State::Work ? overview(anchor) : Aim{}; }
    Aim flickIn(const tracking::Quaternion& anchor, double now) { return state==State::Overview ? land(landingTarget(now), anchor) : Aim{}; }
    // A zoom gesture latches its anchor at the first step after 0.4 s idle: the gazed point (projected
    // px, unprojected with the current camera), else the focus.
    void latch(const std::optional<std::pair<float,float>>& gazedPoint, double now) {
        if(now-lastZoomAt>.4) {
            const auto& c=camera;
            if(gazedPoint) { latchX=ring.unwrap(c.focusX+ring.wrap(gazedPoint->first-c.focusX)/c.zoom); latchY=c.focusY+(gazedPoint->second-c.focusY)/c.zoom; }
            else { latchX=c.targetFocusX; latchY=c.targetFocusY; }
            camera.anchored=false;
        }
        lastZoomAt=now;
    }
    // Overview zooms the canvas about the latched anchor and the view stays put; zooming in past
    // 0.9 of the landed window's Work zoom lands on it (§5.3).
    Aim zoomBy(float amount, const std::optional<std::pair<float,float>>& gazedPoint, const tracking::Quaternion& anchor, double now) {
        if(state==State::Work) if(const auto* w=find(landed)) return zoomWork(*w, amount, gazedPoint, anchor, now);
        state=State::Overview;
        latch(gazedPoint, now);
        zoomAt(camera, latchX, latchY, std::exp(amount), ring);
        const auto* target=find(landed.empty() ? landingTarget(now) : landed);
        if(amount>0 && target && camera.targetZoom>=.9f*workZoom(target->rect, metrics)) return land(target->name, anchor);
        return {};
    }
    // Work dollies the eye in [0.3, R]; zooming out from depth R shrinks the canvas, and below 0.9 of
    // the Work zoom the state becomes Overview.
    Aim zoomWork(const CanvasWindow& w, float amount, const std::optional<std::pair<float,float>>& gazedPoint, const tracking::Quaternion& anchor, double now) {
        const float full=workZoom(w.rect, metrics), R=ring.radius;
        if(camera.targetZoom>=full-1e-3f && (amount>0 || depth<R-1e-3f)) return aim(aimX, aimY, navigation::zoomDepth(depth, amount, R), anchor);
        latch(gazedPoint, now);
        zoomAt(camera, latchX, latchY, std::min(std::exp(amount), full/camera.targetZoom), ring);
        if(camera.targetZoom<.9f*full) state=State::Overview;
        return {};
    }
    // Grab semantics like monitor mode (canvas y points down): the focus and the aimed point move
    // together, so content moves at a constant screen speed at any zoom.
    Aim pan(float dx, float dy, bool begin, const tracking::Quaternion& anchor) {
        if(begin) panAnchor=anchor;
        resetGesture();
        const float speed=metrics.viewH/400/std::max(camera.targetZoom, .01f), limit=1.5f*metrics.rowHeight;
        const float y=std::clamp(camera.targetFocusY-dy*speed, -limit, limit), moveX=-dx*speed, moveY=y-camera.targetFocusY;
        camera.targetFocusX=ring.unwrap(camera.targetFocusX+moveX); camera.targetFocusY=y;
        return aim(aimX+moveX, aimY+moveY, depth, panAnchor);
    }
    // After the View recalibrated the heading: Work faces the landed window, Overview the used arc.
    Aim recenter(const tracking::Quaternion& anchor={}) {
        resetGesture();
        if(state==State::Work) {
            if(const auto* w=find(landed)) { camera.targetFocusX=ring.unwrap(w->rect.cx()); camera.targetFocusY=w->rect.cy(); }
            return aim(camera.targetFocusX, camera.targetFocusY, depth, anchor);
        }
        const auto fit=fitBounds(liveRects(), ring, metrics);
        camera.targetFocusX=fit.focusX; camera.targetFocusY=fit.focusY;
        return aim(fit.focusX, fit.focusY, ring.radius, anchor);
    }
    // Share of the window's angular interval inside the view, against the narrower of the two. The half
    // span is the ring arc visible from the eye's depth.
    float visibleShare(const CanvasWindow& w) const {
        const float eye=state==State::Work ? depth : ring.radius;
        const float half=navigation::visibleArc(eye, fov.horizontal()*spatial::pi/360, 1/ring.radius)/ring.radius*180/spatial::pi;
        const auto r=targetView(w.rect);
        const float centre=std::remainder(ring.heading(r.cx())-heading, 360.f), size=ring.heading(r.w);
        const float lo=std::max(centre-size/2, -half), hi=std::min(centre+size/2, half);
        return std::max(hi-lo, 0.f)/std::max(std::min(size, 2*half), 1e-3f);
    }
    // Hyprland focused a window (M2: the focus_history_id 0 row changed). The camera moves only when
    // less than 90 % of it is in view, and the state stays.
    Aim follow(const std::string& name, const tracking::Quaternion& anchor) {
        const auto* w=find(name);
        if(!w || visibleShare(*w)>=.9f) return {};
        resetGesture();
        camera.targetFocusX=ring.unwrap(w->rect.cx()); camera.targetFocusY=w->rect.cy();
        if(state==State::Work) { camera.targetZoom=workZoom(w->rect, metrics); landed=name; }
        return aim(camera.targetFocusX, camera.targetFocusY, state==State::Work ? depth : ring.radius, anchor);
    }
    // During a zoom gesture the eased focus follows the eased zoom, so the anchor's projection stays put.
    void holdAnchor(double now) {
        auto& c=camera;
        c.focusX=c.targetFocusX+ring.wrap(c.focusX-c.targetFocusX);
        if(!c.anchored) return;
        if(now-lastZoomAt>.4 && !c.moving()) { c.anchored=false; return; }
        if(c.zoom>.999f || c.targetZoom>.999f) return;
        const float k=(1-c.targetZoom)/(1-c.zoom);
        c.focusX=c.targetFocusX+ring.wrap(c.anchorX+ring.wrap(c.targetFocusX-c.targetAnchorX)*k-c.targetFocusX);
        c.focusY=c.anchorY+(c.targetFocusY-c.targetAnchorY)*k;
    }
    struct LabelQuad { PanelLayout layout; GLuint texture=0; float u=1, alpha=1; };
    // Labels at a constant angular height (labelDeg x-height) above each drawn window: always in
    // Overview, in Work only on windows at least 6° tall. Long labels are cropped to the window width.
    std::vector<LabelQuad> labelQuads(int px=48) {
        std::vector<LabelQuad> out;
        const float height=settings.labelDeg*ring.pxPerDeg()/.6f;
        for(auto i:candidates) {
            const auto& w=windows[i]; const auto& p=projected[i];
            if(!w.visible || w.gone || (state!=State::Overview && ring.heading(p.height)<6)) continue;
            const auto& item=labels.item(w.name+"\t"+w.record.title, w.record.cls, w.record.title, px);
            const float width=height*float(item.width)/float(std::max(item.height, 1)), shown=std::min(p.width, width);
            out.push_back({{w.name, p.x, p.y-height-8, shown, height}, item.texture, shown/width, p.brightness/100});
        }
        labels.trim();
        return out;
    }
    void snap() { camera.focusX=camera.targetFocusX; camera.focusY=camera.targetFocusY; camera.zoom=camera.targetZoom; }
    unsigned tierCount(governor::Tier tier) const {
        return unsigned(std::count_if(windows.begin(), windows.end(), [&](const CanvasWindow& w) { return !w.gone && w.decision.tier==tier; }));
    }
    unsigned live() const { return unsigned(std::count_if(windows.begin(), windows.end(), [](const CanvasWindow& w) { return !w.gone; })); }
    // Smoke: the focused window streams and every near window delivered; closed or dead ones do not block.
    bool smokeDone() const {
        return std::all_of(windows.begin(), windows.end(), [](const CanvasWindow& w) {
            if(w.gone || w.closed || (w.source && !w.source->alive())) return true;
            if(w.decision.tier==governor::Tier::Focused) return w.frames>=10;
            return w.decision.tier!=governor::Tier::Near || w.frames>=1;
        });
    }
};
}
