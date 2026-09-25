#pragma once
#include "camera_controls.hpp"
#include "canvas_labels.hpp"
#include "canvas_memory.hpp"
#include "canvas_overlay.hpp"
#include "canvas_model.hpp"
#include "canvas_placement.hpp"
#include "canvas_search.hpp"
#include "capture_governor.hpp"
#include "capture_plan.hpp"
#include "capture.hpp"
#include "frame_source.hpp"
#include "gl_texture.hpp"
#include "window_capture.hpp"
#include "window_list.hpp"
#include <SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
    // The staged window's region source (M3): a screencopy of its rectangle on the canvas output, so
    // popups and the native cursor show. While its frames keep coming (regionShown) the export idles.
    // stageFrame: the shown frame came from it; stageX/Y/W/H: the region, output-local logical px.
    std::unique_ptr<FrameSource> stageSource;
    double stageRetryAt=0, stageLastFrame=0;
    int stageRetryMs=500;
    bool regionShown=false, stageFrame=false;
    int stageX=0, stageY=0;
    unsigned stageW=0, stageH=0, stageFrames=0, stageReportFrames=0;
    // Fill (§5.2): the rect before Fill and the rect it was filled to. Pinned windows are body-locked
    // (drawn by the overlay pass); they keep their rect as their place on the ring.
    std::optional<Rect> beforeFill;
    Rect filledRect;
    bool pinned=false;
};
// One body-locked quad for this frame (§4.5 item 3), in draw order; sizes are world units.
struct OverlayQuad {
    enum class Kind { Pinned, Radar, Palette, Switcher, Help } kind=Kind::Radar;
    GLuint texture=0;
    overlay::space::Vec centre{};
    float width=0, height=0, alpha=1;
    bool opaque=false;
    std::string name;   // the window, for a pinned quad
};
// Overlay berths, rasters and fades. The palette berth is the centre of a full-height palette (its top
// stays put as rows come and go); the radar sits below it along the lower view edge.
struct Overlays {
    overlay::Berth palette{0, -5}, switcher{0, 0}, radar{0, -12}, help{0, 0};
    overlay::Raster paletteRaster, switcherRaster, radarRaster, helpRaster;
    float paletteAlpha=0, switcherAlpha=0, radarAlpha=0, helpAlpha=0;
    std::vector<OverlayQuad> drawn;   // the last overlayQuads result, for the dwell gate
    // A pinned window: its berth at 0.85 R with the heading offset it had when pinned, and its angular width.
    struct Pinned { overlay::Berth berth; float widthDeg=0, alpha=0; };
    std::unordered_map<std::string, Pinned> pinned;
    overlay::Style style;
    double lastTime=-1;
    void release() {
        for(auto* r:{&paletteRaster, &switcherRaster, &radarRaster, &helpRaster}) r->release();
        drawn.clear();
    }
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
    enum class State { Work, Overview, Search, Fill } state=State::Overview;
    std::vector<CanvasWindow> windows;
    std::vector<PanelLayout> projected;   // windows[i] projected by the camera, output = address
    std::vector<size_t> candidates;       // angular cull of projected around the last heading
    Memory memory;
    windows::List lastList;               // the last adopted list, re-adopted when exclusions change
    std::unique_ptr<WindowCaptureHub> hub;
    governor::Fixed governor;
    std::string stagedName, gazed, memoryPath;
    // The canvas output from the window list header (global origin, name); empty for a header without it.
    std::string outputName;
    int outputX=0, outputY=0;
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
    double clock=0;              // the last tick or adopt time, for memory notes made by verbs
    // M4 navigation (§5.2-§5.5): the search session and where Esc returns, landing recency, layout
    // undo, the Alt-Tab switcher, the filled window and help. focusRequest (explicit focus, §5.6),
    // fillRequest (.fill) and bringRequested (a non-canvas window asked for) are carried out by the View.
    struct SearchSession {
        struct Return { State state=State::Overview; std::string landed; float focusX=0, focusY=0, zoom=1, aimX=0, aimY=0, depth=0; };
        bool open=false;
        std::string query, origin;
        std::vector<Result> results;
        std::unordered_set<std::string> matches;
        size_t selected=0;
        std::vector<std::string> order;   // MRU frozen at open, so recency never shifts under the list
        Return back;
    } search;
    Mru mru;
    unsigned long long landings=0;
    Undo history;
    struct Switcher { bool active=false, revealed=false; double openedAt=0, lastStep=0; std::vector<std::string> order; size_t selected=0; } switcher;
    struct FillRequest { std::string address; unsigned w=0, h=0; };
    std::optional<FillRequest> fillRequest;
    std::string filled, bringRequested, focusRequest;
    bool helpOpen=false;
    Overlays overlays;

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
        clock=now;
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
        outputName=list.outputName; outputX=list.outputX; outputY=list.outputY;
        chooseStaged(incoming);
        trackStage();
        if(search.open) rerank();
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
    // Only a window Hyprland really hosts on the stage is on the canvas output: the focus fallback of
    // chooseStaged may be parked on the hidden workspace, where the output shows no window.
    static bool onStage(const CanvasWindow& w) { return w.staged && w.record.place==windows::Place::Stage; }
    // The region follows the window on the stage: a moved or resized one closes its source so openStage
    // reopens it at the new rectangle; every other window has none.
    void trackStage() {
        for(auto& w:windows) {
            if(!onStage(w) || outputName.empty()) { if(w.stageW || w.stageSource) closeStage(w); continue; }
            const int x=w.record.atX-outputX, y=w.record.atY-outputY;
            if(w.stageW && x==w.stageX && y==w.stageY && w.record.w==w.stageW && w.record.h==w.stageH) continue;
            closeStage(w); w.stageX=x; w.stageY=y; w.stageW=w.record.w; w.stageH=w.record.h;
        }
    }
    // OMARCHY_XR_NO_REGION=1 keeps the staged window on the export (troubleshooting, measurements).
    void openStage(CanvasWindow& w, double now) {
        static const bool disabled=std::getenv("OMARCHY_XR_NO_REGION")!=nullptr;
        if(offline || disabled || !onStage(w) || w.gone || w.closed || outputName.empty() || !w.stageW || !w.stageH || w.stageSource || now<w.stageRetryAt) return;
        auto s=std::make_unique<RegionCapture>();
        if(s->open(outputName, w.stageX, w.stageY, int(w.stageW), int(w.stageH), std::min(60u, unsigned(settings.fps)))) { w.stageSource=std::move(s); return; }
        failStage(w, s->error(), now);
    }
    // Logged once per rectangle; the export keeps serving the window meanwhile.
    void failStage(CanvasWindow& w, const std::string& error, double now) {
        if(w.stageRetryMs==500) std::cerr << "Region capture of " << w.name << ": " << error << "; the window export serves it (retrying)" << std::endl;
        dropStage(w);
        w.stageRetryAt=now+w.stageRetryMs/1000.0; w.stageRetryMs=FrameSource::nextRetryMs(w.stageRetryMs);
    }
    // A shown region frame's texture belongs to the region source's capture slots: it goes with it.
    static void dropStage(CanvasWindow& w) {
        if(w.stageFrame && w.frame.texture) { w.frame.texture=0; w.width=w.height=0; }
        w.stageSource.reset(); w.stageFrame=false; w.regionShown=false; w.stageLastFrame=0;
    }
    static void closeStage(CanvasWindow& w) {
        dropStage(w);
        w.stageX=w.stageY=0; w.stageW=w.stageH=0; w.stageRetryAt=0; w.stageRetryMs=500;
    }
    // The first imported frame, and every resize after it, sets the size.
    void sizeFromBuffer(CanvasWindow& w) {
        if(!w.sourceWidth || !w.sourceHeight) return;
        w.sized=true;
        if(w.pixelW==w.sourceWidth && w.pixelH==w.sourceHeight) return;
        w.pixelW=w.sourceWidth; w.pixelH=w.sourceHeight; w.rect.w=float(w.pixelW); w.rect.h=float(w.pixelH);
    }
    void tick(double now, float dt) {
        clock=now;
        camera.tick(dt, ring.period());
        holdAnchor(now);
        revealSwitcher(now);
        const auto expired=[&](const CanvasWindow& w) { return w.gone && now-w.goneAt>=fadeSeconds; };
        for(auto& w:windows) if(expired(w)) { if(w.name==filled) filled.clear(); release(w, now); }
        std::erase_if(windows, expired);
        noteCamera(now);
        if(!memoryPath.empty()) memory.flush(memoryPath, now);
        refresh(now);
    }
    // The frame's texture belongs to the source's capture slots: it goes with the source. The window
    // keeps its own texture only when the last frame was a CPU upload; otherwise it shows its status.
    static void dropSource(CanvasWindow& w) {
        if(w.frame.texture) { w.frame.texture=0; w.width=w.height=0; }
        w.source.reset(); dropStage(w);
    }
    // A closed window leaves its textures and frees its memory slot for the next claim.
    void release(CanvasWindow& w, double now) {
        if(w.texture) glDeleteTextures(1, &w.texture);
        w.texture=0; w.frame.texture=0; w.source.reset(); closeStage(w); governor.forget(w.name);
        for(auto& e:memory.entries) if(e.cls==w.record.cls && e.title==w.record.title) { e.claimed=false; e.seen=now; e.rect=w.beforeFill.value_or(w.rect); }
        memory.touch(now);
    }
    float brightness(const CanvasWindow& w, double now) const {
        if(w.gone) return std::clamp(float(100*(1-(now-w.goneAt)/fadeSeconds)), 1.f, 100.f);
        if(state==State::Search) return search.matches.count(w.name) ? 100.f : std::max(1.f, 100*settings.dimUnmatched);
        return state==State::Overview && gazed!=w.name && selected!=w.name ? 80.f : 100.f;
    }
    // The halo: the search selection full, other matches of a query at .35; else the View's selection.
    float haloTarget(const std::string& name) const {
        if(state!=State::Search) return name==selected ? 1.f : 0.f;
        if(name==selectedResult()) return 1;
        return !search.query.empty() && search.matches.count(name) ? .35f : 0.f;
    }
    // Projected layouts and the cull are rebuilt together, so candidate indices always match windows.
    void refresh(double now) {
        projected.clear(); projected.reserve(windows.size());
        for(const auto& w:windows) {
            Rect r=project(w.rect, camera, ring);
            // A pinned window keeps a zero-size layout at its place, so targeting, the cull and occluders skip it.
            if(w.pinned) r={r.cx(), r.cy(), 0, 0};
            projected.push_back(toLayout(w.name, r, brightness(w, now)));
        }
        cull(heading, halfSpan);
    }
    const std::vector<PanelLayout>& geometry() const { return projected; }
    // A 10° margin keeps halos and edge windows from popping at the view border.
    void cull(float headingDeg, float halfSpanDeg) {
        heading=headingDeg; halfSpan=halfSpanDeg;
        candidates=visibleIndices(projected, heading, halfSpan+10, ring);
        std::erase_if(candidates, [&](size_t i) { return windows[i].pinned; });
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
            const bool pinned=w.pinned && !w.gone;
            in.push_back({w.name, w.staged && !w.gone, w.visible || pinned, projected[i].width/ring.pxPerDeg(), w.record.focusHistoryID, w.pixelW, w.pixelH, pinned});
        }
        const auto decisions=governor.plan(in, zoomedOut, now);
        for(size_t i=0;i<windows.size();++i) {
            auto& w=windows[i]; const auto& d=decisions[i];
            w.decision=d;
            // A pinned window is never projected on the ring: its body-locked quad asks for the native buffer.
            if(w.pinned) { w.demandW=std::max(1u, w.pixelW); w.demandH=std::max(1u, w.pixelH); }
            // Region frames within 0.5 s replace the export, which idles with its session kept alive.
            w.regionShown=onStage(w) && w.stageSource && w.stageSource->error().empty() && w.stageLastFrame>0 && now-w.stageLastFrame<.5;
            // The region follows the same decision as the export: its rate, capped at 60 Hz, and demand.
            if(w.stageSource) { w.stageSource->setFrameRate(std::min(d.rateHz, 60u)); w.stageSource->setDemand(w.visible && d.rateHz>0, w.demandW, w.demandH); }
            if(!w.source) continue;
            w.source->setFrameRate(d.rateHz, d.inFlight, d.phase);
            w.source->setIgnoreDamage(d.ignoreDamage);
            w.source->setDemand(!w.regionShown && (w.visible || w.pinned) && d.rateHz>0, w.demandW, w.demandH);
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
    void service() { pump(); for(auto& w:windows) if(w.stageSource) w.stageSource->service(); }
    void settle(double maxSeconds) {
        if(hub) hub->settle(maxSeconds);
        for(auto& w:windows) if(auto* region=dynamic_cast<RegionCapture*>(w.stageSource.get())) region->settle(maxSeconds);
    }
    // Lease loss: the GL context goes, so textures, capture slots and the hub's device go with it.
    void releaseGpu() {
        for(auto& w:windows) {
            if(w.texture) glDeleteTextures(1, &w.texture);
            w.texture=0; w.frame.texture=0; w.cpuWidth=w.cpuHeight=0; w.source.reset(); dropStage(w);
        }
        labels.release(); overlays.release(); hub.reset();
    }
    void regenerate(double now) {
        for(auto& w:windows) {
            w.texture=gltex::create(); w.retryAt=now; w.retryMs=500; w.stageRetryAt=now; w.stageRetryMs=500;
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
    CanvasWindow* findMutable(const std::string& name) { return const_cast<CanvasWindow*>(std::as_const(*this).find(name)); }
    const CanvasWindow* staged() const { return stagedName.empty() ? nullptr : find(stagedName); }
    bool zoomedOut() const { return state==State::Overview || state==State::Search; }
    bool working() const { return state==State::Work || state==State::Fill; }
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
        endSearch(); resetGesture(); landed=name; mru.note(name, double(++landings));
        // A filled window lands in Fill: zoom 1 at the depth where the fill size covers the view.
        state=w->beforeFill ? State::Fill : State::Work;
        if(w->beforeFill) filled=name;
        const float zoom=w->beforeFill ? 1.f : workZoom(w->rect, metrics);
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
        if(state==State::Search && find(selectedResult())) return selectedResult();
        if(find(stagedName)) return stagedName;
        const CanvasWindow* best=nullptr;
        for(const auto& w:windows) if(!w.gone && (!best || w.record.focusHistoryID<best->record.focusHistoryID)) best=&w;
        return best ? best->name : std::string();
    }
    Aim toggleOverview(const tracking::Quaternion& anchor, double now) {
        return zoomedOut() ? land(landingTarget(now), anchor) : overview(anchor);
    }
    // Flick out: Fill -> Work (restore) -> Overview. Flick in: Overview/Search -> Work (land) -> Fill.
    Aim flickOut(const tracking::Quaternion& anchor) {
        if(state==State::Fill) return fillToggle(anchor);
        return state==State::Work ? overview(anchor) : Aim{};
    }
    Aim flickIn(const tracking::Quaternion& anchor, double now) {
        if(zoomedOut()) return land(landingTarget(now), anchor);
        return state==State::Work ? fillToggle(anchor) : Aim{};
    }
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
        if(working()) if(const auto* w=find(landed)) return zoomWork(*w, amount, gazedPoint, anchor, now);
        if(!zoomedOut()) state=State::Overview;
        latch(gazedPoint, now);
        zoomAt(camera, latchX, latchY, std::exp(amount), ring);
        const auto* target=find(landed.empty() || state==State::Search ? landingTarget(now) : landed);
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
        if(working()) {
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
        const float eye=working() ? depth : ring.radius;
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
        if(working()) { camera.targetZoom=w->beforeFill ? 1.f : workZoom(w->rect, metrics); if(name!=landed) state=w->beforeFill ? State::Fill : State::Work; landed=name; }
        return aim(camera.targetFocusX, camera.targetFocusY, working() ? depth : ring.radius, anchor);
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
    // Search (§5.2, §5.4). Every canvas record is indexed, windows off the canvas at x0.7 (landing on
    // one brings it to the canvas); excluded windows are left out.
    std::vector<windows::Record> searchRecords() const {
        std::vector<windows::Record> out;
        for(auto r:lastList.records) {
            if(r.canvas && !adoptable(r)) continue;
            out.push_back(std::move(r));
        }
        return out;
    }
    std::string selectedResult() const { return search.selected<search.results.size() ? search.results[search.selected].name : std::string(); }
    // A real query jumps to its best hit; an empty one (and a new window list) keeps the selection.
    void rerank(bool best=false) {
        const auto previous=selectedResult();
        search.results=rank(search.query, searchRecords(), search.order, search.origin);
        search.selected=best ? 0 : keepSelection(search.results, previous);
        search.matches.clear();
        for(const auto& r:search.results) search.matches.insert(r.name);
    }
    // Landing recency with the staged window first (the switcher's first step goes to the one before).
    std::vector<std::string> mruOrder(bool canvasOnly) const {
        auto order=mru.order(lastList.records);
        if(canvasOnly) std::erase_if(order, [&](const std::string& name) { const auto* w=find(name); return !w || w->pinned; });
        const auto it=std::find(order.begin(), order.end(), stagedName);
        if(it!=order.end()) std::rotate(order.begin(), it, it+1);
        return order;
    }
    // The session snapshots where Esc returns to; the state stays (the prompt opening with Overview).
    void searchAttach() {
        if(search.open) return;
        search=SearchSession{}; search.open=true;
        search.back={state, landed, camera.targetFocusX, camera.targetFocusY, camera.targetZoom, aimX, aimY, depth};
        search.order=mruOrder(false); search.origin=stagedName;
        rerank();
    }
    void endSearch() { search.open=false; search.query.clear(); search.results.clear(); search.matches.clear(); search.selected=0; }
    // The chord, `/` or the pose verb: from Work or Fill the camera zooms out like Overview first.
    Aim searchOpen(const tracking::Quaternion& anchor) {
        searchAttach();
        const Aim out=working() ? overview(anchor) : Aim{};
        state=State::Search;
        return out;
    }
    Aim searchType(const std::string& text, const tracking::Quaternion& anchor) {
        searchAttach();
        search.query=text;
        const bool real=!tokens(text).empty();
        rerank(real);
        if(!real && state!=State::Search) return {};
        const Aim out=working() ? overview(anchor) : Aim{};
        state=State::Search;
        const Aim follow=followSelection(anchor);
        return follow ? follow : out;
    }
    // The top edge of a full-height palette, degrees above the view centre (its berth keeps that edge).
    float paletteTopDeg() const { return overlays.palette.pitchOffsetDeg+float(overlay::paletteMaxHeight)/45/2; }
    // The selection centred and the aim point below it (canvas y points down), so the window sits between
    // the palette top (plus 1.5°) and the view top (less 0.5°): 0.22 view heights when that fits, else as
    // little as clears the palette. A selection taller than that band lowers the Search zoom until it fits.
    // The eye is at the ring centre, so a point y px above the aim is atan(y / (900 R)) above the view centre.
    Aim followSelection(const tracking::Quaternion& anchor) {
        const auto* w=find(selectedResult());
        if(!w || w->pinned) return {};
        resetGesture();
        const float perTan=900*ring.radius, low=perTan*std::tan((paletteTopDeg()+1.5f)*overlay::degrees);
        const float high=perTan*std::tan((fov.vertical/2-.5f)*overlay::degrees), room=std::max(high-low, 1.f);
        if(w->rect.h*camera.targetZoom>room) camera.targetZoom=std::max(.01f, room/std::max(w->rect.h, 1.f));
        const float half=w->rect.h*camera.targetZoom/2, above=std::clamp(.22f*metrics.viewH, low+half, std::max(low+half, high-half));
        camera.targetFocusX=ring.unwrap(w->rect.cx()); camera.targetFocusY=w->rect.cy();
        return aim(camera.targetFocusX, camera.targetFocusY+above, ring.radius, anchor);
    }
    Aim searchMove(int step, const tracking::Quaternion& anchor) {
        const long n=long(search.results.size());
        if(!n) return {};
        search.selected=size_t(((long(search.selected)+step)%n+n)%n);
        if(state!=State::Search) { if(working()) overview(anchor); state=State::Search; }
        return followSelection(anchor);
    }
    // Prompt keys (window_list.hpp searchKey). Esc closes help, then clears the query, then closes
    // and reverts; Ctrl+1..8 land on that row.
    Aim searchKey(const std::string& key, const tracking::Quaternion& anchor) {
        if(key=="f1") { helpOpen=!helpOpen; return {}; }
        if(key=="ctrl-a") return arrange(anchor);
        if(key=="ctrl-z") return undo(anchor);
        if(key=="ctrl-shift-z") return redo(anchor);
        if(!search.open) return {};
        if(key=="esc") {
            if(helpOpen) { helpOpen=false; return {}; }
            return search.query.empty() ? searchClose(true, anchor) : searchType("", anchor);
        }
        if(key=="enter" || key=="shift-enter") return searchLand(key=="shift-enter", anchor);
        if(key.size()==6 && key.starts_with("ctrl-")) {
            const size_t row=size_t(key[5]-'1');
            if(row>=search.results.size()) return {};
            search.selected=row; return searchLand(false, anchor);
        }
        if(key=="up" || key=="shift-tab") return searchMove(-1, anchor);
        if(key=="down" || key=="tab") return searchMove(1, anchor);
        return {};
    }
    // Enter lands on the selection (explicit focus); Shift+Enter summons it first. A window off the
    // canvas is asked for instead and landed on once a window list shows it on the canvas.
    Aim searchLand(bool summonFirst, const tracking::Quaternion& anchor) {
        if(search.results.empty()) return {};
        const auto result=search.results[std::min(search.selected, search.results.size()-1)];
        focusRequest=result.name;
        if(!result.canvas) { bringRequested=result.name; return searchClose(false, anchor); }
        return summonFirst ? summon(result.name, anchor) : land(result.name, anchor);
    }
    // Leaving Search keeps the landing, or reverts state, landing and camera to the snapshot.
    Aim searchClose(bool revert, const tracking::Quaternion& anchor) {
        if(!search.open) return {};
        const auto back=search.back;
        endSearch();
        if(!revert) { if(state==State::Search) state=State::Overview; return {}; }
        const auto* w=find(back.landed);
        state=back.state==State::Fill && !(w && w->beforeFill) ? State::Work : back.state;
        landed=back.landed; resetGesture();
        camera.targetFocusX=back.focusX; camera.targetFocusY=back.focusY; camera.targetZoom=back.zoom;
        return aim(back.aimX, back.aimY, back.depth, anchor);
    }
    // The palette shows in the Search state, the switcher once revealed, help while open.
    bool overlayOpen() const { return state==State::Search || switcher.revealed || helpOpen; }
    // Dwell stays off only while the gaze ray (the view centre) meets a drawn overlay quad (§5.6), so gaze
    // still selects windows above the palette in Search.
    bool overlayUnderGaze(const overlay::space::Scene& scene) const {
        return std::any_of(overlays.drawn.begin(), overlays.drawn.end(), [&](const OverlayQuad& q) {
            return q.alpha>0 && overlay::space::gazeHit(scene, q.centre, q.width, q.height)>0;
        });
    }
    // Fill (phantomat canvasToggleFill): the staged window fills fillSize. Toggling it untouched restores
    // the rect before; moved since keeps the new centre with the old size; resized since fills again.
    Aim fillToggle(const tracking::Quaternion& anchor) {
        auto* w=findMutable(stagedName);
        if(!w || !onStage(*w)) { std::cout << "Canvas: no staged window to fill" << std::endl; return {}; }
        if(!w->beforeFill) return fill(*w, anchor);
        const Rect cur=w->rect, f=w->filledRect;
        if(std::abs(cur.w-f.w)>=2 || std::abs(cur.h-f.h)>=2) return fill(*w, anchor);
        Rect back=*w->beforeFill;
        if(std::abs(ring.wrap(cur.x-f.x))>=2 || std::abs(cur.y-f.y)>=2) { back.x=ring.unwrap(cur.cx()-back.w/2); back.y=cur.cy()-back.h/2; }
        w->beforeFill.reset(); w->rect=back; filled.clear();
        requestSize(*w);
        memory.note(w->record.cls, w->record.title, back, clock);
        refresh(clock);
        return land(w->name, anchor);
    }
    // Centre kept, the size set at once (the buffer follows when Lua has resized the window). Filling
    // again keeps the first rect before Fill: a client or Lua clamp that missed the size is not a restore point.
    Aim fill(CanvasWindow& w, const tracking::Quaternion& anchor) {
        const Rect f=fillSize(metrics, settings.outputScale);
        if(!w.beforeFill) w.beforeFill=w.rect;
        w.sized=true;
        w.filledRect={ring.unwrap(w.rect.cx()-f.w/2), w.rect.cy()-f.h/2, f.w, f.h};
        w.rect=w.filledRect;
        requestSize(w); refresh(clock);
        return land(w.name, anchor);
    }
    // .fill carries logical px of the canvas output.
    void requestSize(const CanvasWindow& w) {
        const float s=std::max(settings.outputScale, .1f);
        fillRequest=FillRequest{w.name, unsigned(std::lround(w.rect.w/s)), unsigned(std::lround(w.rect.h/s))};
    }
    // Alt-Tab (phantomat switchWindow/finishSwitcher): the first step goes to the window before the
    // staged one; the list shows after a 0.2 s hold, so a quick tap flips without a flash. The camera
    // does not move until the finish lands.
    void switcherStep(int dir, double now) {
        if(!switcher.active) {
            switcher=Switcher{}; switcher.order=mruOrder(true);
            if(switcher.order.empty()) return;
            switcher.active=true; switcher.openedAt=now;
        }
        const long n=long(switcher.order.size());
        switcher.selected=size_t(((long(switcher.selected)+dir)%n+n)%n);
        switcher.lastStep=now; revealSwitcher(now);
    }
    void revealSwitcher(double now) { if(switcher.active && now-switcher.openedAt>=.2) switcher.revealed=true; }
    Aim switcherFinish(const tracking::Quaternion& anchor, bool cancel) {
        if(!switcher.active) return {};
        const auto name=switcher.order[switcher.selected];
        switcher.active=switcher.revealed=false;
        if(cancel) return {};
        focusRequest=name;
        return land(name, anchor);
    }
    // Layout verbs (§5.1): arrange, undo/redo, neighbour, nudge, summon and pin. Pinned windows are body-
    // locked and take no part; every layout change is a checkpoint first.
    std::vector<Placed> arrangeable() const {
        auto out=taken();
        std::erase_if(out, [&](const Placed& p) { const auto* w=find(p.name); return !w || w->pinned; });
        return out;
    }
    Snapshot snapshot() const {
        Snapshot s;
        for(const auto& w:windows) if(!w.gone) s.rects.push_back({w.name, w.rect});
        return s;
    }
    void moveTo(CanvasWindow& w, const Rect& r) { w.rect=r; memory.note(w.record.cls, w.record.title, r, clock); }
    // Windows closed since the snapshot are skipped.
    void applySnapshot(const Snapshot& s) {
        for(const auto& [name, rect]:s.rects) if(auto* w=findMutable(name)) moveTo(*w, rect);
        refresh(clock);
    }
    // Overview again over the new layout; a search stays open.
    Aim refit(const tracking::Quaternion& anchor) {
        const bool searching=state==State::Search;
        const Aim out=overview(anchor);
        if(searching) state=State::Search;
        return out;
    }
    // CTRL+A in the prompt, SDL, Studio and the pose verb: grouped by category, then class, from the canvas
    // point at the view heading (headingPoint); from Work or Fill it ends in Overview over the new layout.
    Aim arrange(const tracking::Quaternion& anchor) {
        std::unordered_map<std::string,std::string> titles;
        for(const auto& w:windows) if(!w.gone) titles[w.name]=w.record.title;
        const auto groupOf=[&](const Placed& p) { return std::string(categoryName(category(p.cls, titles[p.name])))+'\t'+p.cls; };
        const auto placed=canvas::arrange(arrangeable(), headingPoint().first, ring, metrics, settings.gapPx, mruOrder(true), groupOf);
        history.checkpoint(snapshot());
        for(const auto& p:placed) if(auto* w=findMutable(p.name)) moveTo(*w, p.rect);
        refresh(clock);
        std::cout << "Canvas: arranged " << placed.size() << " windows" << std::endl;
        return refit(anchor);
    }
    Aim restoreLayout(std::optional<Snapshot> s, const tracking::Quaternion& anchor) {
        if(!s) return {};
        applySnapshot(*s);
        return zoomedOut() ? refit(anchor) : reaim(anchor);
    }
    Aim undo(const tracking::Quaternion& anchor) { return restoreLayout(history.popUndo(snapshot()), anchor); }
    Aim redo(const tracking::Quaternion& anchor) { return restoreLayout(history.popRedo(snapshot()), anchor); }
    static std::optional<Direction> direction(const std::string& token) {
        if(token=="left") return Direction::Left;
        if(token=="right") return Direction::Right;
        if(token=="up") return Direction::Up;
        if(token=="down") return Direction::Down;
        return {};
    }
    // The landed window, else the staged one.
    std::string current() const { return find(landed) ? landed : stagedName; }
    // SUPER+arrows: an explicit landing (the View focuses the window, so Lua stages it).
    Aim neighbourOf(Direction dir, const tracking::Quaternion& anchor) {
        const auto target=canvas::neighbour(current(), dir, arrangeable(), ring);
        if(!target) return {};
        focusRequest=*target;
        return land(*target, anchor);
    }
    // One grid step, overlap allowed (phantomat); the camera chases only a window leaving the view.
    Aim nudgeBy(Direction dir, const tracking::Quaternion& anchor) {
        auto* w=findMutable(current());
        if(!w || w->pinned) return {};
        Rect r=canvas::snap(canvas::nudge(w->rect, dir, nudgeStep)); r.x=ring.unwrap(r.x);
        if(!inBand(r, metrics)) return {};
        history.checkpoint(snapshot());
        moveTo(*w, r); refresh(clock);
        if(!working() || visibleShare(*w)>=.9f) return {};
        camera.targetFocusX=ring.unwrap(r.cx()); camera.targetFocusY=r.cy();
        return aim(camera.targetFocusX, camera.targetFocusY, depth, anchor);
    }
    // The canvas point at the aim: in Search where the search started from (the camera follows the
    // selection; phantomat summons to the navigation return), else the current one.
    std::pair<float,float> headingPoint() const {
        float fx=camera.targetFocusX, fy=camera.targetFocusY, z=camera.targetZoom, ax=aimX, ay=aimY;
        if(state==State::Search) { const auto& b=search.back; fx=b.focusX; fy=b.focusY; z=b.zoom; ax=b.aimX; ay=b.aimY; }
        z=std::max(z, .01f);
        return {ring.unwrap(fx+ring.wrap(ax-fx)/z), fy+(ay-fy)/z};
    }
    // To the heading without overlapping (summonRect), then land.
    Aim summon(const std::string& name, const tracking::Quaternion& anchor) {
        auto* w=findMutable(name);
        if(!w || w->pinned) return {};
        const auto [x, y]=headingPoint();
        auto others=taken();
        std::erase_if(others, [&](const Placed& p) { return p.name==name; });
        history.checkpoint(snapshot());
        moveTo(*w, summonRect(w->rect, x, y, others, ring, metrics, settings.gapPx));
        refresh(clock);
        return land(name, anchor);
    }
    // The window a pin acts on: in Overview/Search the selection, else the landed or staged window.
    std::string pinTarget() const {
        if(state==State::Search && find(selectedResult())) return selectedResult();
        if(zoomedOut() && find(selected)) return selected;
        return current();
    }
    bool pinToggle(const std::string& name) {
        auto* w=findMutable(name);
        if(!w) return false;
        w->pinned=!w->pinned; refresh(clock);
        std::cout << "Canvas: " << (w->pinned ? "pinned " : "unpinned ") << name << std::endl;
        return true;
    }
    unsigned pinnedCount() const { return unsigned(std::count_if(windows.begin(), windows.end(), [](const CanvasWindow& w) { return !w.gone && w.pinned; })); }
    struct LabelQuad { PanelLayout layout; GLuint texture=0; float u=1, alpha=1; };
    // Labels at a constant angular height (labelDeg x-height) above each drawn window: always in
    // Overview, in Work only on windows at least 6° tall. Long labels are cropped to the window width.
    std::vector<LabelQuad> labelQuads(int px=48) {
        std::vector<LabelQuad> out;
        const float height=settings.labelDeg*ring.pxPerDeg()/.6f;
        for(auto i:candidates) {
            const auto& w=windows[i]; const auto& p=projected[i];
            if(!w.visible || w.gone || (!zoomedOut() && ring.heading(p.height)<6)) continue;
            const auto& item=labels.item(w.name+"\t"+w.record.title, w.record.cls, w.record.title, px);
            const float width=height*float(item.width)/float(std::max(item.height, 1)), shown=std::min(p.width, width);
            out.push_back({{w.name, p.x, p.y-height-8, shown, height}, item.texture, shown/width, p.brightness/100});
        }
        labels.trim();
        return out;
    }
    // Body-locked overlays (§4.5 item 3, §5.4, §5.8), in draw order: pinned windows, radar, palette,
    // switcher, help. Berths follow the head lazily at 0.9 of the ring reach (pinned 0.85); sizes are
    // angular (45 raster px per degree, the radar 30), and each overlay fades over 150 ms. Called once
    // per eye with the same time, which moves nothing twice.
    std::vector<OverlayQuad> overlayQuads(const overlay::space::Scene& scene, double now) {
        using Kind=OverlayQuad::Kind;
        auto& o=overlays;
        const float dt=o.lastTime<0 ? 0.f : float(std::clamp(now-o.lastTime, 0., .1)); o.lastTime=now;
        const float reach=overlay::ringReach(scene, ring.radius);
        std::vector<OverlayQuad> out;
        pinnedQuads(scene, reach, now, dt, out);
        const auto place=[&](Kind kind, overlay::Berth& berth, const overlay::Raster& raster, float pxPerDeg, int fullHeight, float alpha) {
            out.push_back(berthQuad(kind, berth, raster, pxPerDeg, fullHeight, .9f*reach, alpha, scene, now));
        };
        // Rasters follow the content only while shown; a fading overlay keeps its last picture.
        const bool radarShown=zoomedOut(), paletteShown=state==State::Search, switcherShown=switcher.revealed;
        if(fade(o.radarAlpha, radarShown, dt, o.radar)) {
            if(radarShown) { const auto r=radarView(); o.radarRaster.update(overlay::radarKey(r, o.style), overlay::radarWidth, overlay::radarHeight, now, .2, [&](cairo_t* cr) { return overlay::paintRadar(cr, r, o.style); }); }
            place(Kind::Radar, o.radar, o.radarRaster, 30, 0, o.radarAlpha);
        }
        if(fade(o.paletteAlpha, paletteShown, dt, o.palette)) {
            if(paletteShown) { const auto l=paletteList(); o.paletteRaster.update(overlay::listKey(l, o.style), overlay::paletteWidth, overlay::paletteMaxHeight, now, 0, [&](cairo_t* cr) { return overlay::paintPalette(cr, l, o.style); }); }
            place(Kind::Palette, o.palette, o.paletteRaster, 45, overlay::paletteMaxHeight, o.paletteAlpha);
        }
        if(fade(o.switcherAlpha, switcherShown, dt, o.switcher)) {
            if(switcherShown) { const auto l=switcherList(); o.switcherRaster.update(overlay::listKey(l, o.style), overlay::switcherWidth, overlay::switcherMaxHeight, now, 0, [&](cairo_t* cr) { return overlay::paintSwitcher(cr, l, o.style); }); }
            place(Kind::Switcher, o.switcher, o.switcherRaster, 45, 0, o.switcherAlpha);
        }
        if(fade(o.helpAlpha, helpOpen, dt, o.help)) {
            const bool takeover=settings.takeoverKeys;
            o.helpRaster.update(overlay::helpKey(takeover, o.style), overlay::helpWidth, overlay::helpHeight, now, 0, [&](cairo_t* cr) { return overlay::paintHelp(cr, o.style, takeover); });
            place(Kind::Help, o.help, o.helpRaster, 45, 0, o.helpAlpha);
        }
        o.drawn=out;
        return out;
    }
    // True while the overlay shows or fades out; a hidden one starts at its wanted place next time.
    static bool fade(float& alpha, bool shown, float dt, overlay::Berth& berth) {
        alpha=std::clamp(alpha+(shown ? dt : -dt)/.15f, 0.f, 1.f);
        if(!shown && alpha<=0) berth.reset();
        return shown || alpha>0;
    }
    // A palette keeps its top edge where a full-height one would have it.
    static OverlayQuad berthQuad(OverlayQuad::Kind kind, overlay::Berth& berth, const overlay::Raster& raster, float pxPerDeg, int fullHeight,
                                 float distance, float alpha, const overlay::space::Scene& scene, double now) {
        namespace space=overlay::space;
        berth.update(scene, distance, now);
        const float d=space::length(berth.position), width=2*d*std::tan(float(raster.width)/pxPerDeg/2*overlay::degrees), perPx=width/float(std::max(raster.width, 1));
        OverlayQuad q{kind, raster.texture, berth.centre(scene), width, perPx*float(raster.height), alpha, false, {}};
        if(fullHeight>raster.height) q.centre=space::add(q.centre, space::mul(space::facing(q.centre, scene.eye).up, perPx*float(fullHeight-raster.height)/2));
        return q;
    }
    // Pinned windows (§5.1): the frame texture at 0.85 of the ring reach, the heading offset and angular
    // width taken on the first frame after the pin, clamped into the view.
    void pinnedQuads(const overlay::space::Scene& scene, float reach, double now, float dt, std::vector<OverlayQuad>& out) {
        auto& pins=overlays.pinned;
        std::erase_if(pins, [&](const auto& entry) { const auto* w=find(entry.first); return !w || !w->pinned; });
        for(const auto& w:windows) {
            if(w.gone || !w.pinned) continue;
            auto [it, fresh]=pins.try_emplace(w.name);
            auto& pin=it->second;
            if(fresh) pinAt(pin, w, scene);
            pin.alpha=std::min(1.f, pin.alpha+dt/.15f);
            pin.berth.update(scene, .85f*reach, now);
            const GLuint texture=w.frame.texture ? w.frame.texture : w.texture;
            if(!w.width || !w.height || !texture) continue;
            const float width=2*overlay::space::length(pin.berth.position)*std::tan(pin.widthDeg/2*overlay::degrees);
            out.push_back({OverlayQuad::Kind::Pinned, texture, pin.berth.centre(scene), width, width*float(w.height)/float(w.width), pin.alpha, true, w.name});
        }
    }
    void pinAt(Overlays::Pinned& pin, const CanvasWindow& w, const overlay::space::Scene& scene) const {
        const Rect shown=project(w.rect, camera, ring);
        const auto centre=cylinder().pose(toLayout(w.name, shown)).center;
        const auto p=scene.camera({centre.x, centre.y, centre.z});
        const float distance=std::max(overlay::space::length(p), .3f), across=fov.horizontal();
        pin.widthDeg=std::clamp(2*std::atan(shown.w/1800/distance)/overlay::degrees, 6.f, .55f*across);
        const float tall=pin.widthDeg*shown.h/std::max(shown.w, 1.f);
        const float yawLimit=std::max(0.f, across/2-pin.widthDeg/2-1), pitchLimit=std::max(0.f, fov.vertical/2-tall/2-1);
        pin.berth=overlay::Berth(std::clamp(std::atan2(p.x, -p.z)/overlay::degrees, -yawLimit, yawLimit),
                                 std::clamp(std::atan2(p.y, std::hypot(p.x, p.z))/overlay::degrees, -pitchLimit, pitchLimit));
    }
    // Palette and switcher rows: the category chip, class and title of each listed window.
    overlay::Row overlayRow(const std::string& name, std::vector<size_t> matches, bool onCanvas) const {
        const windows::Record* record=nullptr;
        for(const auto& r:lastList.records) if(r.name()==name) { record=&r; break; }
        if(!record) if(const auto* w=find(name)) record=&w->record;
        if(!record) return {categoryCode(Category::Other), "", name, {}, onCanvas};
        return {categoryCode(category(record->cls, record->title)), record->cls, record->title, std::move(matches), onCanvas};
    }
    // At most 8 rows, scrolled so the selection shows; "N of M" counts matches of every indexed window.
    overlay::List paletteList() const {
        overlay::List l; l.query=search.query; l.matches=search.results.size();
        l.total=size_t(std::count_if(lastList.records.begin(), lastList.records.end(), [&](const windows::Record& r) { return !r.canvas || adoptable(r); }));
        const size_t first=search.selected>=8 ? search.selected-7 : 0;
        for(size_t i=first;i<std::min(first+8, search.results.size());++i) {
            const auto& r=search.results[i];
            l.rows.push_back(overlayRow(r.name, r.titleMatches, r.canvas));
        }
        l.selected=search.selected-first;
        return l;
    }
    overlay::List switcherList() const {
        overlay::List l; l.matches=l.total=switcher.order.size();
        const size_t first=switcher.selected>=8 ? switcher.selected-7 : 0;
        for(size_t i=first;i<std::min(first+8, switcher.order.size());++i) l.rows.push_back(overlayRow(switcher.order[i], {}, true));
        l.selected=switcher.selected-first;
        return l;
    }
    // Ring positions around the canvas point at the view heading, at canvas scale; the view band is
    // what the current zoom shows.
    overlay::Radar radarView() const {
        overlay::Radar r;
        const float zoom=std::max(camera.zoom, .01f);
        const float centreX=ring.unwrap(camera.focusX+ring.wrap(heading*ring.pxPerDeg()-camera.focusX)/zoom);
        r.viewDeg=std::min(360.f, fov.horizontal()/zoom);
        for(const auto& w:windows) {
            if(w.gone || w.pinned) continue;
            r.marks.push_back({ring.heading(ring.wrap(w.rect.cx()-centreX)), ring.heading(w.rect.w), rowFor(w.rect.cy(), metrics), w.staged});
        }
        return r;
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
