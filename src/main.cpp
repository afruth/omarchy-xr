#include "spectator.hpp"
#include "environment.hpp"
#include "graphics_limits.hpp"
#include "capture.hpp"
#include "frame_source.hpp"
#include "surface.hpp"
#include "layout.hpp"
#include "curvature.hpp"
#include "spacing.hpp"
#include "pose_socket.hpp"
#include "direct_output.hpp"
#include "camera_controls.hpp"
#include "live_controls.hpp"
#include "targeting.hpp"
#include "hover.hpp"
#include "capture_plan.hpp"
#include "gpu_timers.hpp"
#include "vblank.hpp"
#include "load_governor.hpp"
#include "halo_shader.hpp"
#include "sky_cull.hpp"
#include "dwell.hpp"
#include "theme.hpp"
#include "notification_hud.hpp"
#include "canvas_scene.hpp"
#include <ctime>
#include <csignal>
#include <filesystem>
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t interrupted=0;
void stopSignal(int) {interrupted=1;}
constexpr float pi = 3.14159265358979323846f;
struct Panel {
    PanelLayout layout;
    std::unique_ptr<FrameSource> capture;
    CapturedFrame frame;
    GLuint texture = 0;
    unsigned width = 0, height = 0, frames = 0;
    bool failed = false;
    std::string captureStatus;
    double retryAt = 0;
    int retryMs = 500;
    float halo=0;
    unsigned sourceWidth=0,sourceHeight=0,cpuWidth=0,cpuHeight=0;
    bool visible=true;
    adaptive::Quality quality;
};
// Tessellate in horizontal strips so each monitor can have its own curvature.
void surface(const spatial::Pose& pose, float x, float y, float w, float h, float offset) {
    const int segments = spatial::surfaceSegments(w,pose.surfaceBend);
    const int rows=spatial::verticalSegments(h,pose);
    for(int row=0;row<rows;++row){
        const float v0=float(row)/rows,v1=float(row+1)/rows;
        glBegin(GL_QUAD_STRIP);
        for(int i=0;i<=segments;++i){
            const float u=float(i)/segments;
            auto bottom=spatial::vertex(pose,x+u*w,y+v0*h,offset);
            auto top=spatial::vertex(pose,x+u*w,y+v1*h,offset);
            glTexCoord2f(u,1-v0);glVertex3f(bottom.x,bottom.y,bottom.z);
            glTexCoord2f(u,1-v1);glVertex3f(top.x,top.y,top.z);
        }
        glEnd();
    }
}
// Keeps its own *unit arithmetic (Cylinder::pose divides by 900); the two differ in the last ulp.
void drawPanel(const SurfaceView& s, const Cylinder& c) {
    constexpr float unit=1.f/900.f;
    const auto& p=*s.layout;
    const float w=p.width*unit,h=p.height*unit;
    const auto pose=spatial::pose((p.x+p.width/2-c.cx)*unit,-(p.y+p.height/2-c.cy)*unit,w,c.span,c.distance,c.workspace,p.curvature);
    glColor3f(.025f,.028f,.035f);
    surface(pose,-w/2,-h/2,w,h,0);
    if (s.width) {
        const auto content=interaction::content(w,h,s.sourceWidth,s.sourceHeight);
        const float tw=content.width,th=content.height;
        glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,s.texture);
        const float dim=s.status->empty()?1.f:.45f;
        glColor3f(p.brightness/100*dim,p.brightness/100*dim,p.brightness/100*dim);
        surface(pose,-tw/2,-th/2,tw,th,.01f);
        glDisable(GL_TEXTURE_2D);
    } else {
        for (int row=0;row<8;++row) {
            glColor3f(.2f,.25f,.32f);
            surface(pose,-w*.44f,h*.32f-row*h*.09f,w*.7f,h*.025f,.01f);
        }
    }
}
// Soft geometry halos behind the screen, with no permanent frame over content.
void drawHaloRings(const spatial::Pose& pose,float w,float h,float extent,float shadow,float haloLevel) {
    for(int i=11;i>=0;--i){
        const float inner=shadow*i/12,outer=shadow*(i+1)/12,thickness=outer-inner;
        const float fade=1-float(i)/12,drop=shadow*.3f;
        glColor4f(0,0,0,.18f*fade*fade);
        surface(pose,-w/2-outer,h/2+inner-drop,w+2*outer,thickness,-.035f);
        surface(pose,-w/2-outer,-h/2-outer-drop,w+2*outer,thickness,-.035f);
        surface(pose,-w/2-outer,-h/2-inner-drop,thickness,h+2*inner,-.035f);
        surface(pose,w/2+inner,-h/2-inner-drop,thickness,h+2*inner,-.035f);
    }
    for(int i=11;i>=0;--i){
        const float inner=extent*i/12,outer=extent*(i+1)/12,thickness=outer-inner;
        const float fade=1-float(i)/12;
        glColor4f(.35f,.65f,1.f,(.025f+.18f*haloLevel)*fade*fade);
        surface(pose,-w/2-outer,h/2+inner,w+2*outer,thickness,-.025f);
        surface(pose,-w/2-outer,-h/2-outer,w+2*outer,thickness,-.025f);
        surface(pose,-w/2-outer,-h/2-inner,thickness,h+2*inner,-.025f);
        surface(pose,w/2+inner,-h/2-inner,thickness,h+2*inner,-.025f);
    }
}
// Four bands around the rectangle; the top and bottom ones include the corners.
void drawHaloBands(HaloShader& halo,const spatial::Pose& pose,float x0,float y0,float w,float h,float margin,float offset) {
    halo.patch(x0-margin,y0+h,w+2*margin,margin);surface(pose,x0-margin,y0+h,w+2*margin,margin,offset);
    halo.patch(x0-margin,y0-margin,w+2*margin,margin);surface(pose,x0-margin,y0-margin,w+2*margin,margin,offset);
    halo.patch(x0-margin,y0,margin,h);surface(pose,x0-margin,y0,margin,h,offset);
    halo.patch(x0+w,y0,margin,h);surface(pose,x0+w,y0,margin,h,offset);
}
void drawHalo(HaloShader& halo,const theme::Rgb& accent,const SurfaceView& s,const Cylinder& c) {
    const auto& p=*s.layout;const float w=p.width/900,h=p.height/900;
    const auto pose=c.pose(p);
    const float extent=std::min(w,h)*(.012f+.018f*s.halo);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
    // A neutral, feathered drop shadow adds depth without outlining the image.
    const float shadow=std::min(w,h)*.045f,drop=shadow*.3f;
    if(halo.ready()){
        halo.use(0,0,0,.18f,w/2,h/2,shadow,0,-drop);drawHaloBands(halo,pose,-w/2,-h/2-drop,w,h,shadow,-.035f);
        // Unselected: a faint feathered glow. Selected: the theme accent, a solid ten-pixel rim
        // that fades out over the halo extent, so the chosen monitor reads at a glance.
        const float rim=10.f/900*s.halo;
        halo.use(accent[0],accent[1],accent[2],.03f+.85f*s.halo,w/2,h/2,extent,0,0,rim);
        drawHaloBands(halo,pose,-w/2,-h/2,w,h,extent+rim,-.025f);
        halo.stop();
    } else drawHaloRings(pose,w,h,extent,shadow,s.halo);
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);
}
struct View {
    std::vector<Panel>& panels;
    const bool smoke;
    spatial::Workspace workspace;
    float spacing;
    const std::string& display;
    const std::string& posePath;
    const bool direct;
    const bool stereo;
    const float ipd;
    const float fov;
    const std::string& layoutPath;
    int fps;
    bool spectatorEnabled;
    PoseSocket tracking;
    std::unique_ptr<DirectOutput> output;
    std::unique_ptr<Spectator> spectator;
    std::unique_ptr<SkyEnvironment> environment;
    std::unique_ptr<notifications::Hud> notificationHud;
    std::optional<LiveControls> controls;
    std::string spectatorError;
    std::string ladderLogged;   // the last "Canvas: ladder" line, logged once per change
    SDL_Window* window=nullptr;
    SDL_GLContext context=nullptr;
    GLint maxTexture=0;
    float left=0, top=0, right=0, bottom=0, cx=0, cy=0;
    float yaw=0, pitch=0, panX=0, panY=0, panZ=0, distance=5;
    std::vector<PanelLayout> geometry;
    float targetDistance=5, targetPanX=0, targetPanY=0, targetPanZ=0;
    tracking::Quaternion navigationRotation, targetRotation, selectionAnchor, focusAnchor;
    targeting::Selection selection;
    targeting::Tracker gaze;
    std::string focusOutput, panOutput;
    float focusDepth=5, focusX=0, focusY=0, smoothFocusX=0, smoothFocusY=0;
    std::optional<targeting::Hit> zoomGaze;
    bool focusFromGaze=false, panGestureActive=false, panCamera=false;
    double recenterUntil=0;
    bool running=true;
    int result=0, drawn=0, trackingStatus=-1, glStreak=0, swapStreak=0;
    Uint64 started=0, nextLayoutCheck=0;
    double lastCameraTime=0, reportTime=0, workP99=0, gpuP99=5, workMax=0, lastFrameMs=16.7, lastPredictionMs=0, lastMarginMs=0, nextTrackingCheck=0;
    unsigned reportFrames=0, missedBaseline=0, seenMisses=0;
    std::vector<double> workTimes, frameTimes, gpuCaptureTimes, gpuSpectatorTimes, gpuSceneTimes, latchWork, latchGpu;
    SpectatorGovernor governor;
    HaloShader halo;
    unsigned skyCulled=0;
    GpuTimers gpuTimers;
    MissPenalty missPenalty;
    std::filesystem::file_time_type layoutVersion{}, trackingVersion{}, gazeVersion{};
    bool trackingLoaded=false, gazeLoaded=false;
    std::string trackingPath, gazePath;
    double nextGazeCheck=0;
    gaze::Dwell dwell;
    // Zoom level reached by the last fit: flick in steps overview -> monitor -> pane, flick out steps back.
    enum class Level { Overview, Monitor, Pane };
    Level level=Level::Overview;
    // What the scene shows: the monitor panels today; free-floating windows on the canvas later.
    enum class SceneMode { Monitors, Canvas };
    SceneMode mode=SceneMode::Monitors;
    std::string levelOutput;
    double interactionUntil=0;
    theme::Accent accent;
    unsigned pointerSerial=0;
    float pointerX=0, pointerY=0;
    std::string dwellOutput;
    // Window canvas mode (--canvas): the scene, its window list file and the hot-reload state.
    std::unique_ptr<canvas::Scene> canvas;
    std::string windowsPath;
    std::filesystem::file_time_type windowsVersion{}, canvasVersion{};
    Uint64 nextWindowsCheck=0;
    double nextCanvasCheck=0;
    unsigned long long windowsSeq=0;
    unsigned loggedLive=~0u;
    bool windowsRejected=false, canvasRejected=false;
    bool mousePanning=false;
    // Canvas input (§3.4): the explicitly focused window published as .hover v4, the restage asked
    // for and not yet seen in a window list, and the compositor cursor mapped onto the staged window.
    std::string hoverOutput, restageRequested;
    struct XrCursor { bool valid=false; std::string window; float px=0, py=0; };
    XrCursor xrCursor;
    std::optional<windows::Cursor> lastCursor;
    double cursorAt=-1e9;
    GLuint cursorTexture=0;
    // The search prompt (§5.4): shown while the scene's search session is open, typed either in this SDL
    // window (when it has keyboard focus) or in the Quickshell prompt over .prompt/.search.
    bool promptShown=false, promptSdl=false;
    canvas::Scene::State canvasStateSeen=canvas::Scene::State::Overview;
    // Monitor-only math (safe distance, overview depth, bounds); canvas mode must never run it.
    mutable unsigned monitorMathCalls=0;
    View(std::vector<Panel>& panels, bool smoke, spatial::Workspace workspace, float spacing, const std::string& display, const std::string& posePath, bool direct, bool stereo, float ipd, float fov, const std::string& layoutPath, int fps, bool spectatorEnabled)
        : panels(panels), smoke(smoke), workspace(workspace), spacing(spacing), display(display), posePath(posePath), direct(direct), stereo(stereo), ipd(ipd), fov(fov), layoutPath(layoutPath), fps(fps), spectatorEnabled(spectatorEnabled), tracking(posePath) {}
    void drawable(int& w, int& h) const { if (output) { w=output->width(); h=output->height(); } else SDL_GL_GetDrawableSize(window, &w, &h); }
    float span() const { return (right-left)/900.f; }
    float safe(float d) const { ++monitorMathCalls; return spatial::safeDistance(geometry, cx, cy, span(), d, workspace, spacing); }
    tracking::Quaternion baseView() const { return targeting::viewRotation(tracking.camera.view, pitch, yaw); }
    tracking::Quaternion currentView() const { return tracking::multiply(baseView(), navigationRotation); }
    float aspect() const { int w,h; drawable(w,h); return float(std::max(w/(stereo?2:1),1))/std::max(h,1); }
    float overviewDepth() const { ++monitorMathCalls; return navigation::overviewDepth(geometry, cx, cy, span(), distance, workspace, fov, aspect()); }
    float maxZoomDepth() const { ++monitorMathCalls; return overviewDepth()*2.f; }
    Cylinder sceneCylinder() const { return canvas ? canvas->cylinder() : Cylinder{cx, cy, span(), distance, workspace}; }
    const std::vector<PanelLayout>& sceneGeometry() const { return canvas ? canvas->geometry() : geometry; }
    const PanelLayout* findLayout(const std::string& name) const {
        const auto& g=sceneGeometry();
        const auto found=std::find_if(g.begin(), g.end(), [&](const auto& p){ return p.output==name; });
        return found==g.end() ? nullptr : &*found;
    }
    spatial::Pose monitorPose(const PanelLayout& p) const { return sceneCylinder().pose(p); }
    // Every drawable surface in the current scene mode, in draw order.
    template<class F> void forEachSurface(F&& f) const {
        if (canvas) { canvas->forEachSurface(f); return; }
        for (const auto& p:panels) f(SurfaceView{&p.layout, p.frame.texture?p.frame.texture:p.texture, p.failed?0u:p.width, p.height, p.sourceWidth, p.sourceHeight, &p.captureStatus, p.halo, p.visible, 1, {}});
    }
    void bindTexture(GLuint texture) const { gltex::bind(texture); }
    // The windowed canvas keys (§5.8) for the title bar; --help lists them all.
    static constexpr const char* canvasKeys="/: search | F: fill | O: overview | Tab: switch | F1: help";
    bool openWindow() {
        if (SDL_Init(direct ? SDL_INIT_EVENTS : SDL_INIT_VIDEO) != 0) { std::cerr << SDL_GetError() << '\n'; return false; }
        if (direct) return true;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        int displayIndex=0;
        if (!display.empty() && !findDisplay(displayIndex)) return false;
        const std::string title=mode==SceneMode::Canvas ? std::string("Omarchy XR canvas | Right-drag: look | Middle-drag: pan | Wheel: zoom | ")+canvasKeys
                                                        : "Omarchy XR | Right-drag: look | Middle-drag: pan | Wheel: zoom | F: fit | R: recenter";
        window = SDL_CreateWindow(title.c_str(),
            SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), 1280, 720,
            SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) { std::cerr << SDL_GetError() << '\n'; SDL_Quit(); return false; }
        if (!display.empty() && SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP)!=0) {
            std::cerr << SDL_GetError() << '\n'; SDL_DestroyWindow(window); SDL_Quit(); return false;
        }
        context = SDL_GL_CreateContext(window);
        if (!context) { std::cerr << SDL_GetError() << '\n'; SDL_DestroyWindow(window); SDL_Quit(); return false; }
        SDL_GL_SetSwapInterval(1);
        SDL_ShowCursor(SDL_DISABLE);
        return true;
    }
    bool findDisplay(int& displayIndex) const {
        displayIndex=-1;
        for (int i=0;i<SDL_GetNumVideoDisplays();++i) {
            const std::string name=SDL_GetDisplayName(i) ? SDL_GetDisplayName(i) : "";
            if (name==display || name.ends_with("("+display+")")) { displayIndex=i; break; }
        }
        if (displayIndex<0) { std::cerr << "Display unavailable: " << display << '\n'; SDL_Quit(); return false; }
        std::cout << "Presentation output: " << display << std::endl;
        return true;
    }
    void fit() {
        level=Level::Overview; levelOutput.clear(); interactionUntil=monotonicSeconds()+.4;
        recenterUntil=0; panCamera=false; zoomGaze.reset(); focusFromGaze=false;
        focusOutput.clear(); focusX=focusY=0; targetRotation={};
        targetDistance=distance;
        targetPanX=targetPanY=yaw=pitch=0;
        targetPanZ=distance-overviewDepth();
    }
    void sampleTarget() {
        const bool fresh=posePath.empty() || tracking.camera.fresh(monotonicSeconds());
        const auto view=currentView();
        const auto c=sceneCylinder();
        gaze.update(fresh ? targeting::query(targeting::viewRay(view, {panX, panY, panZ}), sceneGeometry(), c.cx, c.cy, c.span, c.distance, c.workspace, canvas ? &canvas->candidates : nullptr) : std::nullopt, fresh);
        selection.validate(sceneGeometry());
        // A glance never selects: the look point has to rest on one area with the head settled.
        dwellOn(gaze.current, monotonicSeconds());
    }
    // Scrolling, zooming, fitting and the camera's own easing all pause selection: the look
    // point sweeps the scene then, and nothing rests.
    void dwellOn(const std::optional<targeting::Hit>& hit, double now) {
        const bool interacting=panGestureActive || now<interactionUntil || cameraMoving();
        const auto settled=dwell.update(interacting ? std::nullopt : hit, tracking.camera.headSpeed(), now);
        if (settled && canvas && !panGestureActive && now>=recenterUntil) {
            // Canvas: a dwell only selects (halo) and records the landing candidate; it never moves anything.
            selection.output=settled->output; canvas->noteDwell(settled->output, now);
            std::cout << "Gaze: settled on " << settled->output << std::endl;
        } else if (settled && !panGestureActive && now>=recenterUntil) {
            const auto previous=selection.output;
            selection.observe(gaze.current);
            if (selection.output!=previous) selectionAnchor=baseView();
            if (dwell.settings.pointer) { ++pointerSerial; pointerX=settled->pixelX; pointerY=settled->pixelY; }
            std::cout << "Gaze: settled on " << settled->output << " at " << int(settled->pixelX) << "," << int(settled->pixelY) << std::endl;
        }
    }
    bool cameraMoving() const {
        const auto& a=navigationRotation; const auto& b=targetRotation;
        const double dot=std::abs(a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z);
        return std::abs(targetPanX-panX)>.005f || std::abs(targetPanY-panY)>.005f || std::abs(targetPanZ-panZ)>.005f
            || std::abs(std::log(distance/targetDistance))>1e-3f || dot<.99995 || (canvas && canvas->camera.moving());
    }
    bool focusSelected(bool fitHeight, float zoom) {
        recenterUntil=0; panCamera=false;
        if (fitHeight) { zoomGaze.reset(); focusFromGaze=false; }
        const bool captured=!zoomGaze;
        const bool recapture=zoomGaze && gaze.current && gaze.current->output!=zoomGaze->output;
        if (!fitHeight && zoom!=0 && navigation::lockZoomGaze(zoomGaze, gaze.current)) lockGaze(captured, recapture);
        const auto* found=findLayout(selection.output);
        if (!found) return false;
        aimAt(*found, fitHeight, zoom, captured, recapture);
        return true;
    }
    void lockGaze(bool captured, bool recapture) {
        if (!captured && !recapture) return;
        const auto previous=selection.output;
        selection.observe(zoomGaze);
        if (selection.output!=previous) selectionAnchor=baseView();
    }
    void aimAt(const PanelLayout& p, bool fitHeight, float zoom, bool captured, bool recapture) {
        // Keep workspace geometry fixed; move the camera along the look point's local normal.
        // Zoom must not change its workspace bend or yaw.
        targetDistance=distance;
        const auto pose=monitorPose(p);
        if (focusOutput!=p.output) {
            focusOutput=p.output; focusX=focusY=0; focusAnchor=selectionAnchor; focusFromGaze=false;
            focusDepth=navigation::viewingDistance(pose, {panX, panY, panZ});
        }
        if (fitHeight) { focusX=focusY=0; focusAnchor=selectionAnchor; focusDepth=navigation::frontHeightDistance(p, pose, fov); }
        else followZoom(p, zoom, captured, recapture);
        // Stay in front of a curved monitor's nearest edge, even at high zoom.
        const float sag=spatial::bendZ(p.width/1800, pose.surfaceBend);
        focusDepth=std::max(focusDepth, sag+.15f);
        int viewportW, viewportH; drawable(viewportW, viewportH);
        const auto limited=navigation::applyPanLimits(p, pose, focusDepth, fov, float(viewportW/(stereo?2:1))/std::max(viewportH, 1), {focusX, focusY, 0}, focusFromGaze);
        focusX=limited.x; focusY=limited.y;
        const auto target=navigation::panFocus(pose, focusAnchor, focusDepth, focusX, focusY);
        targetRotation=target.rotation; targetPanX=target.pan.x; targetPanY=target.pan.y; targetPanZ=target.pan.z;
    }
    void followZoom(const PanelLayout& p, float zoom, bool captured, bool recapture) {
        if (zoom!=0 && zoomGaze && zoomGaze->output==p.output) {
            const auto origin=navigation::gazeFocus(p, *zoomGaze);
            focusX=origin.x; focusY=origin.y;
            if (captured || recapture) focusAnchor=baseView();
            focusFromGaze=true;
        }
        focusDepth=navigation::zoomDepth(focusDepth, zoom, maxZoomDepth());
    }
    void recenterSelected() {
        const auto previousView=currentView();
        recenterUntil=monotonicSeconds()+.5;
        const bool wasPanning=panCamera; panCamera=false;
        const auto* found=findLayout(selection.output);
        if (!found) { headingOnly(previousView); return; }
        recenterOn(*found, wasPanning, previousView);
    }
    // No selection yet: animate heading only, preserving zoom and pan.
    void headingOnly(const tracking::Quaternion& previousView) {
        tracking.camera.recenter(monotonicSeconds()); yaw=pitch=0;
        navigationRotation=navigation::preserveView(previousView, baseView());
        targetRotation={};
    }
    void recenterOn(const PanelLayout& p, bool wasPanning, const tracking::Quaternion& previousView) {
        const auto pose=monitorPose(p);
        // Preserve current viewing distance, including an in-flight zoom.
        // Keep the current zoom even if gaze selected a different panel.
        auto zoomPose=zoomedPose(pose, wasPanning);
        focusDepth=navigation::viewingDistance(zoomPose, {panX, panY, panZ});
        tracking.camera.recenter(monotonicSeconds()); yaw=pitch=0;
        selectionAnchor=focusAnchor={}; focusOutput=p.output; focusX=focusY=0; zoomGaze.reset(); focusFromGaze=false; targetDistance=distance;
        const auto target=navigation::frontFocus(pose, {}, focusDepth);
        navigationRotation=navigation::preserveView(previousView, baseView());
        targetRotation=target.rotation;
        targetPanX=target.pan.x; targetPanY=target.pan.y; targetPanZ=target.pan.z;
        std::cout << "Camera: smooth recenter " << selection.output << " depth " << focusDepth << std::endl;
    }
    spatial::Pose zoomedPose(spatial::Pose pose, bool wasPanning) const {
        const auto* zoomPanel=findLayout(focusOutput);
        if (!zoomPanel) return pose;
        pose=monitorPose(*zoomPanel);
        const float x=wasPanning?smoothFocusX:focusX, y=wasPanning?smoothFocusY:focusY;
        pose.center=spatial::vertex(pose, x, y); pose.yaw-=x*pose.surfaceBend;
        if (pose.spherical) pose.latitude+=y*pose.surfaceBend;
        return pose;
    }
    void panSelected(float dx, float dy, bool begin) {
        recenterUntil=0;
        if (begin) panOutput=selection.output;
        const auto* found=findLayout(panOutput);
        if (!found) return;
        const auto pose=monitorPose(*found);
        // Freeze the zoom visible at gesture start, including animations.
        if (begin && !beginPan(*found, pose)) return;
        int w,h; drawable(w, h);
        const auto limits=navigation::gazePanLimits(*found, pose, focusDepth, fov, float(w/(stereo?2:1))/std::max(h, 1), {focusX, focusY, 0}, focusFromGaze);
        const float speed=2*focusDepth*std::tan(fov*pi/360)/400;
        focusX=std::clamp(focusX-dx*speed, -limits.x, limits.x);
        focusY=std::clamp(focusY+dy*speed, -limits.y, limits.y);
        const auto target=navigation::panFocus(pose, focusAnchor, focusDepth, focusX, focusY);
        targetRotation=target.rotation; targetPanX=target.pan.x; targetPanY=target.pan.y; targetPanZ=target.pan.z;
    }
    bool beginPan(const PanelLayout& p, const spatial::Pose& pose) {
        auto reference=pose;
        if (focusOutput==p.output) {
            const float x=panCamera?smoothFocusX:focusX, y=panCamera?smoothFocusY:focusY;
            reference.center=spatial::vertex(pose, x, y); reference.yaw-=x*pose.surfaceBend;
            if (pose.spherical) reference.latitude+=y*pose.surfaceBend;
        }
        const float depth=navigation::viewingDistance(reference, {panX, panY, panZ});
        int w,h; drawable(w, h);
        const auto limits=navigation::panLimits(p, pose, depth, fov, float(w/(stereo?2:1))/std::max(h, 1));
        if (limits.x==0 && limits.y==0) { panOutput.clear(); return false; }
        zoomGaze.reset();
        if (focusOutput!=p.output) focusX=focusY=0;
        if (!panCamera) { smoothFocusX=focusX; smoothFocusY=focusY; }
        panCamera=true;
        focusOutput=p.output; focusDepth=depth; focusAnchor=baseView(); targetDistance=distance;
        return true;
    }
    void zoomBy(float amount) {
        interactionUntil=monotonicSeconds()+.4;
        if (!focusSelected(false, amount)) {
            targetDistance=distance;
            targetPanZ=distance-navigation::zoomDepth(distance-targetPanZ, amount, maxZoomDepth());
        }
    }
    void fitTarget() {
        // An explicit command targets what is looked at now; it does not wait for a dwell.
        if (gaze.current) { const auto previous=selection.output; selection.observe(gaze.current); if (selection.output!=previous) selectionAnchor=baseView(); }
        fitSelection();
    }
    void fitOutput(const std::string& name) {
        if (!findLayout(name)) return;
        selection.output=name; selectionAnchor=baseView();
        fitSelection();
    }
    void fitSelection() {
        interactionUntil=monotonicSeconds()+.4;
        if (focusSelected(true, 0)) { level=Level::Monitor; levelOutput=selection.output; std::cout << "Camera: fit selected monitor face-on " << selection.output << std::endl; }
        else std::cout << "Camera: no selected monitor; fit ignored" << std::endl;
    }
    // The flick gestures step through the levels. In: workspace -> monitor -> the active window on
    // that monitor. Out: pane -> monitor -> workspace. A monitor fit on a different monitor than the
    // current level's restarts at the monitor level.
    void flickIn() {
        if (gaze.current) { const auto previous=selection.output; selection.observe(gaze.current); if (selection.output!=previous) selectionAnchor=baseView(); }
        if (level==Level::Monitor && levelOutput==selection.output && fitPane()) return;
        fitTarget();
    }
    void flickOut() {
        if (level==Level::Pane) { fitTarget(); return; }
        fit(); tracking.camera.recenter(monotonicSeconds());
    }
    // Search and later are canvas verbs (M4): Switch steps by x (+-1) or finishes (begin; output
    // "cancel" cancels); Neighbour/Nudge take a direction token, Summon/Focus/Pin a window in output.
    enum class Verb { Fit, FitTarget, FitOutput, Recenter, ZoomBy, FlickIn, FlickOut, Pan,
                      Search, Fill, Switch, Arrange, Undo, Redo, Neighbour, Nudge, Pin, Help, Summon, Focus };
    struct Move { Verb verb; float x=0, y=0; bool begin=false; std::string output={}; };
    // Every external navigation entry point goes through here; monitor mode ignores the canvas verbs.
    void navigate(const Move& m) {
        if (canvas) { navigateCanvas(m); return; }
        switch (m.verb) {
        case Verb::Fit: fit(); tracking.camera.recenter(monotonicSeconds()); break;
        case Verb::FitTarget: fitTarget(); break;
        case Verb::FitOutput: fitOutput(m.output); break;
        case Verb::Recenter: recenterSelected(); break;
        case Verb::ZoomBy: zoomBy(m.x); break;
        case Verb::FlickIn: flickIn(); break;
        case Verb::FlickOut: flickOut(); break;
        case Verb::Pan: panSelected(m.x, m.y, m.begin); break;
        default: break;
        }
    }
    // Canvas mode (§4.2 input table): every verb goes to the scene; none falls through to monitor math.
    void navigateCanvas(const Move& m) {
        const double now=monotonicSeconds();
        const auto anchor=baseView();
        switch (m.verb) {
        case Verb::Fit: explicitMove(now); applyAim(canvas->toggleOverview(anchor, now)); break;
        case Verb::FitTarget: canvasFitTarget(now); break;
        case Verb::FitOutput: if (windows::parseAddress(m.output)) canvasFollow(m.output, now); break;
        case Verb::Recenter: canvasRecenter(); break;
        case Verb::ZoomBy: interactionUntil=now+.4; applyAim(canvas->zoomBy(m.x, gazePoint(), anchor, now)); break;
        case Verb::FlickIn: explicitMove(now); applyAim(canvas->flickIn(anchor, now)); break;
        case Verb::FlickOut: explicitMove(now); applyAim(canvas->flickOut(anchor)); break;
        case Verb::Pan: recenterUntil=0; applyAim(canvas->pan(m.x, m.y, m.begin, anchor)); break;
        default: canvasCommand(m, now, anchor); break;
        }
        afterCanvasVerb();
    }
    // The M4 verbs (§5.2-§5.5); explicit ones hold dwell and focus follow off for 0.4 s.
    void canvasCommand(const Move& m, double now, const tracking::Quaternion& anchor) {
        const auto direction=canvas::Scene::direction(m.output);
        if (m.verb!=Verb::Switch && m.verb!=Verb::Pin && m.verb!=Verb::Help) explicitMove(now);
        switch (m.verb) {
        case Verb::Search: applyAim(canvas->searchOpen(anchor)); break;
        case Verb::Fill: applyAim(canvas->fillToggle(anchor)); break;
        case Verb::Switch:
            if (m.begin) applyAim(canvas->switcherFinish(anchor, m.output=="cancel"));
            else canvas->switcherStep(m.x<0 ? -1 : 1, now);
            break;
        case Verb::Arrange: applyAim(canvas->arrange(anchor)); break;
        case Verb::Undo: applyAim(canvas->undo(anchor)); break;
        case Verb::Redo: applyAim(canvas->redo(anchor)); break;
        case Verb::Neighbour: if (direction) applyAim(canvas->neighbourOf(*direction, anchor)); break;
        case Verb::Nudge: if (direction) applyAim(canvas->nudgeBy(*direction, anchor)); break;
        case Verb::Pin: canvas->pinToggle(m.output.empty() ? canvas->pinTarget() : m.output); break;
        case Verb::Help: canvas->helpOpen=!canvas->helpOpen; break;
        case Verb::Summon: applyAim(canvas->summon(m.output, anchor)); break;
        case Verb::Focus:
            if (canvas->find(m.output)) { canvas->focusRequest=m.output; applyAim(canvas->land(m.output, anchor)); }
            break;
        default: break;
        }
    }
    // What a scene verb asked of the View: the explicit focus (§5.6), the .fill request, the prompt.
    void afterCanvasVerb() {
        level=canvas->zoomedOut() ? Level::Overview : Level::Monitor;
        levelOutput=canvas->zoomedOut() ? std::string() : canvas->landed;
        if (!canvas->focusRequest.empty()) focusWindow(std::exchange(canvas->focusRequest, {}));
        if (canvas->fillRequest && controls) controls->publishFill(canvas->fillRequest->address, canvas->fillRequest->w, canvas->fillRequest->h);
        canvas->fillRequest.reset();
        syncPrompt();
    }
    // At the window's buffer centre; a window off the canvas by its list size (Lua adopts it on staging).
    void focusWindow(const std::string& name) {
        if (const auto* w=canvas->find(name)) { explicitFocus(name, w->pixelW/2.f, w->pixelH/2.f); return; }
        for (const auto& r:canvas->lastList.records)
            if (r.name()==name) { explicitFocus(name, canvas->scaled(r.w)/2.f, canvas->scaled(r.h)/2.f); return; }
    }
    bool promptViaSdl() const { return window && SDL_GetKeyboardFocus()==window; }
    // §5.4: the prompt opens with Overview (entered from Work or Fill) and with an explicit search, and
    // closes with the session. The SDL window types itself when focused; its Overview keeps the
    // single-key commands, so only an explicit search starts SDL text input.
    void syncPrompt() {
        if (!canvas || !controls) return;
        using S=canvas::Scene::State;
        const bool entered=canvas->state==S::Overview && (canvasStateSeen==S::Work || canvasStateSeen==S::Fill);
        if (entered && !promptViaSdl()) canvas->searchAttach();
        canvasStateSeen=canvas->state;
        const bool open=canvas->search.open;
        if (open==promptShown) return;
        promptShown=open;
        if (open) promptSdl=promptViaSdl();
        if (promptSdl) { if (open) SDL_StartTextInput(); else SDL_StopTextInput(); }
        else controls->publishPrompt(open, canvas->outputName);
    }
    bool promptHoldsKeys() const { return canvas && promptShown && !promptSdl; }
    static float headingDeg(const tracking::Quaternion& view) {
        const auto f=targeting::rotate(tracking::conjugate(view), {0, 0, -1});
        return std::atan2(f.x, -f.z)*180/pi;
    }
    canvas::Fov canvasFov() const { return {fov, aspect()}; }
    // The eye eases to the scene's aim like every monitor-mode aim; no aim leaves the view where it is.
    void applyAim(const canvas::Scene::Aim& aim) {
        if (!aim) return;
        targetRotation=aim->rotation; targetPanX=aim->pan.x; targetPanY=aim->pan.y; targetPanZ=aim->pan.z;
        targetDistance=distance; panCamera=false; zoomGaze.reset(); focusFromGaze=false;
    }
    void explicitMove(double now) { interactionUntil=now+.4; recenterUntil=0; }
    // The look point in projected canvas px, for the zoom anchor.
    std::optional<std::pair<float,float>> gazePoint() const {
        const auto* l=gaze.current ? findLayout(gaze.current->output) : nullptr;
        if (!l) return std::nullopt;
        return std::pair{l->x+gaze.current->pixelX, l->y+gaze.current->pixelY};
    }
    // An explicit command lands on what is looked at now, else on the selection; it does not wait for a dwell.
    void canvasFitTarget(double now) {
        const std::string name=gaze.current ? gaze.current->output : selection.output;
        if (name.empty()) { std::cout << "Canvas: nothing gazed; fit ignored" << std::endl; return; }
        explicitMove(now);
        if (gaze.current) focusHit(*gaze.current);
        else if (const auto* l=findLayout(name)) { targeting::Hit centre; centre.output=name; centre.pixelX=l->width/2; centre.pixelY=l->height/2; focusHit(centre); }
        selection.output=name;
        applyAim(canvas->land(name, baseView()));
        std::cout << "Canvas: land on " << name << std::endl;
    }
    // Hyprland focus follows only when the window is mostly out of view; an explicit verb within 0.4 s wins.
    void canvasFollow(const std::string& name, double now) {
        if (now<interactionUntil) { std::cout << "Canvas: focus " << name << " not followed during an explicit move" << std::endl; return; }
        const auto aim=canvas->follow(name, baseView());
        if (aim) std::cout << "Canvas: follow focus to " << name << std::endl;
        applyAim(aim);
    }
    void canvasRecenter() {
        const auto previousView=currentView();
        recenterUntil=monotonicSeconds()+.5;
        tracking.camera.recenter(monotonicSeconds()); yaw=pitch=0;
        navigationRotation=navigation::preserveView(previousView, baseView());
        applyAim(canvas->recenter());
        std::cout << "Camera: canvas recenter" << std::endl;
    }
    bool fitPane() {
        if (!controls->paneValid || controls->paneOutput!=selection.output) { std::cout << "Camera: no active window known on " << selection.output << "; pane fit ignored" << std::endl; return false; }
        const auto* found=findLayout(selection.output);
        if (!found) return false;
        const auto& p=*found;
        recenterUntil=0; panCamera=false; zoomGaze.reset(); focusFromGaze=false; targetDistance=distance;
        interactionUntil=monotonicSeconds()+.4;
        const auto pose=monitorPose(p);
        int viewportW, viewportH; drawable(viewportW, viewportH);
        const float aspect=float(viewportW/(stereo?2:1))/std::max(viewportH, 1);
        // Surface coordinates of the window's centre: x along the arc, y up, from the monitor centre.
        focusOutput=p.output; focusAnchor=selectionAnchor;
        focusX=(controls->paneX+controls->paneW/2-p.width/2)/900;
        focusY=-(controls->paneY+controls->paneH/2-p.height/2)/900;
        focusDepth=navigation::rectDistance(controls->paneW, controls->paneH, fov, aspect);
        const float sag=spatial::bendZ(p.width/1800, pose.surfaceBend);
        focusDepth=std::max(focusDepth, sag+.15f);
        const auto limited=navigation::applyPanLimits(p, pose, focusDepth, fov, aspect, {focusX, focusY, 0}, false);
        focusX=limited.x; focusY=limited.y;
        const auto target=navigation::panFocus(pose, focusAnchor, focusDepth, focusX, focusY);
        targetRotation=target.rotation; targetPanX=target.pan.x; targetPanY=target.pan.y; targetPanZ=target.pan.z;
        level=Level::Pane; levelOutput=p.output;
        std::cout << "Camera: fit active window " << int(controls->paneW) << "x" << int(controls->paneH) << " on " << p.output << std::endl;
        return true;
    }
    bool ensureLease() {
        if (!output || output->pump()) return true;
        std::cerr << "Display lease ended; reacquiring for up to 10 seconds\n";
        const double deadline=monotonicSeconds()+10;
        while (monotonicSeconds()<deadline && !interrupted) {
            try {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                for (auto& p:panels) {
                    if (p.texture) glDeleteTextures(1, &p.texture);
                    p.texture=0; p.frame.texture=0; p.cpuWidth=p.cpuHeight=0; p.capture.reset();
                }
                if (canvas) canvas->releaseGpu();
                if (cursorTexture) { glDeleteTextures(1, &cursorTexture); cursorTexture=0; }
                spectator.reset();
                environment->release();
                if(notificationHud) notificationHud->release();
                halo.release();
                gpuTimers.reset();
                output=std::make_unique<DirectOutput>(display, stereo);
                if (!output->pump()) throw std::runtime_error("Replacement lease is not active");
                for (auto& p:panels) {
                    p.retryAt=monotonicSeconds(); p.retryMs=500; p.captureStatus="reconnecting after lease";
                    glGenTextures(1, &p.texture); bindTexture(p.texture);
                }
                if (canvas) canvas->regenerate(monotonicSeconds());
                gpuTimers.probe();
                seenMisses=output->missedVblanks();
                missedBaseline=seenMisses;
                return true;
            } catch (const std::exception& error) {
                std::cerr << error.what() << '\n';
                SDL_Delay(200);
            }
        }
        result=3; return false;
    }
    void serviceCaptures() {
        for (auto& p:panels) if (p.capture && !p.failed) p.capture->service();
        if (canvas) canvas->service();
    }
    void reconnectCaptures() {
        const double now=monotonicSeconds();
        if (canvas) { canvas->recoverHub(now); canvas->openSources(now); for (auto& w:canvas->windows) canvas->openStage(w, now); return; }
        for (auto& p:panels) {
            if (p.capture || p.retryAt<=0 || now<p.retryAt) continue;
            auto next=std::make_unique<DesktopCapture>();
            next->setFrameRate(static_cast<unsigned>(fps));
            if (next->connect() && next->select(p.layout.output)) { p.capture=std::move(next); p.captureStatus.clear(); p.retryAt=0; p.retryMs=500; }
            else { p.captureStatus=next->error().empty()?"reconnect failed":next->error(); p.retryAt=now+p.retryMs/1000.0; p.retryMs=DesktopCapture::nextRetryMs(p.retryMs); }
        }
    }
    void updateCaptures() {
        if (canvas) { updateWindowCaptures(); return; }
        for (auto& p:panels) {
            if (!p.capture || p.failed) continue;
            if (p.capture->update(p.frame)) upload(p);
            if (!p.capture->error().empty()) dropCapture(p);
        }
    }
    void upload(Panel& p) {
        if (p.frame.width>unsigned(maxTexture) || p.frame.height>unsigned(maxTexture)) {
            std::cerr << p.layout.output << ": exceeds GPU texture size\n"; p.failed=true; result=1; return;
        }
        copyFrame(p);
    }
    // Shared by monitor panels and canvas windows.
    template<class S> void copyFrame(S& p) {
        p.sourceWidth=p.frame.sourceWidth; p.sourceHeight=p.frame.sourceHeight;
        glBindTexture(GL_TEXTURE_2D, p.texture);
        if (!p.frame.texture) uploadCpu(p);
        p.width=p.frame.width; p.height=p.frame.height;
        ++p.frames;
    }
    template<class S> void uploadCpu(S& p) {
        if (p.cpuWidth!=p.frame.width || p.cpuHeight!=p.frame.height) {
            p.width=p.frame.width; p.height=p.frame.height; p.cpuWidth=p.width; p.cpuHeight=p.height;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p.width, p.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.frame.rgba.data());
        } else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, p.width, p.height, GL_RGBA, GL_UNSIGNED_BYTE, p.frame.rgba.data());
    }
    void dropCapture(Panel& p) {
        std::cerr << p.layout.output << ": " << p.capture->error() << " (retrying)\n";
        p.captureStatus=p.capture->error();
        p.capture.reset();
        p.retryAt=monotonicSeconds()+p.retryMs/1000.0;
        p.retryMs=DesktopCapture::nextRetryMs(p.retryMs);
    }
    // Hub dispatch once per tick, then every window; a closed window stays closed, no backoff.
    void updateWindowCaptures() {
        canvas->pump();
        const double now=monotonicSeconds();
        for (auto& w:canvas->windows) {
            if (w.stageSource) updateStage(w);
            if (!w.source) continue;
            // While region frames are shown, the idling export's thumbnail must not replace them.
            if (w.regionShown) { CapturedFrame parked; w.source->update(parked); }
            else if (w.source->update(w.frame)) { w.stageFrame=false; canvas->noteReady(w, now); uploadWindow(w); }
            if (!w.source) continue;
            if (!w.source->alive()) { std::cout << w.name << ": window closed" << std::endl; w.captureStatus="closed"; w.closed=true; canvas::Scene::dropSource(w); }
            else if (!w.source->error().empty()) dropWindow(w);
        }
        // Windowed, the swap blocks a whole frame without dispatch: let new requests reach their copy now.
        if (!output) canvas->settle(.002);
    }
    // A region frame uploads into the same texture; its size equals the window's, so sizeFromBuffer holds.
    void updateStage(canvas::CanvasWindow& w) {
        const double now=monotonicSeconds();
        if (w.stageSource->update(w.frame)) { w.stageFrame=true; ++w.stageFrames; ++w.stageReportFrames; w.stageLastFrame=now; uploadWindow(w); }
        if (w.stageSource && !w.stageSource->error().empty()) canvas->failStage(w, w.stageSource->error(), now);
    }
    void uploadWindow(canvas::CanvasWindow& w) {
        if (w.frame.width>unsigned(maxTexture) || w.frame.height>unsigned(maxTexture)) {
            std::cerr << w.name << ": exceeds GPU texture size\n"; w.captureStatus="exceeds GPU texture size"; w.closed=true; canvas::Scene::dropSource(w); return;
        }
        copyFrame(w);
        ++w.reportFrames; w.lastFrame=monotonicSeconds();
        canvas->sizeFromBuffer(w);
    }
    void dropWindow(canvas::CanvasWindow& w) {
        std::cerr << w.name << ": " << w.source->error() << " (retrying)\n";
        w.captureStatus=w.source->error(); canvas::Scene::dropSource(w);
        w.retryAt=monotonicSeconds()+w.retryMs/1000.0; w.retryMs=FrameSource::nextRetryMs(w.retryMs);
    }
    void describePrediction(const char* source) const {
        const auto& p=tracking.camera.prediction;
        std::cout << "Tracking: prediction up to " << p.horizonMs << " ms, fading in from " << p.restSpeed << " to " << p.fullSpeed << " deg/s, " << p.samples << " samples; filter "
                  << (p.minCutoff>0 ? "min cutoff " : "off, min cutoff ") << p.minCutoff << " Hz, beta " << p.beta << " (" << source << ")" << std::endl;
    }
    void reloadTracking(double now) {
        if (trackingPath.empty() || now<nextTrackingCheck) return;
        nextTrackingCheck=now+.25;
        std::error_code missing;
        const auto version=std::filesystem::last_write_time(trackingPath, missing);
        if (missing) { if (trackingLoaded) { tracking.camera.prediction={}; trackingLoaded=false; describePrediction("defaults"); } return; }
        if (trackingLoaded && version==trackingVersion) return;
        trackingLoaded=true; trackingVersion=version;
        std::ifstream file(trackingPath); std::string line; std::getline(file, line);
        if (const auto parsed=tracking::parsePrediction(line)) { tracking.camera.prediction=*parsed; describePrediction("tracking.tsv"); }
        else std::cerr << "Ignoring invalid tracking.tsv; expected: tracking-v2 <horizonMs 0..30> <restSpeed> <fullSpeed> <samples 2..8> <minCutoffHz> <beta>" << std::endl;
    }
    void reloadGaze(double now) {
        if (gazePath.empty() || now<nextGazeCheck) return;
        nextGazeCheck=now+.25;
        std::error_code missing;
        const auto version=std::filesystem::last_write_time(gazePath, missing);
        if (missing) { if (gazeLoaded) { dwell.settings={}; gazeLoaded=false; describeGaze("defaults"); } return; }
        if (gazeLoaded && version==gazeVersion) return;
        gazeLoaded=true; gazeVersion=version;
        std::ifstream file(gazePath); std::string line; std::getline(file, line);
        if (const auto parsed=gaze::parseSettings(line)) { dwell.settings=*parsed; describeGaze("gaze.tsv"); }
        else std::cerr << "Ignoring invalid gaze.tsv; expected: gaze-v1 <dwellMs 100..10000> <settleSpeed deg/s> <radiusPx> <pointer 0|1>" << std::endl;
    }
    void describeGaze(const char* source) const {
        const auto& g=dwell.settings;
        std::cout << "Gaze: dwell " << g.dwellMs << " ms below " << g.settleSpeed << " deg/s within " << g.radiusPx << " px, pointer " << (g.pointer ? "follows" : "off") << " (" << source << ")" << std::endl;
    }
    void predictPose() {
        timespec now{}; clock_gettime(CLOCK_MONOTONIC, &now);
        const double nowSeconds=now.tv_sec+now.tv_nsec/1e9;
        const double target=predictionTargetSeconds(nowSeconds, output && output->hasVblank(), output?output->lastVblankUs():0, output?output->refreshHz():0, lastFrameMs);
        tracking.camera.predict(target, nowSeconds);
        lastPredictionMs=tracking.camera.predictionMs;
    }
    void waitForPose() {
        // Direct mode samples the pose at the latch deadline. With no direct output or no vblank timestamp yet, sample at the start of the frame.
        const bool earlyPose=!output || !output->hasVblank();
        if (earlyPose) return;
        lastMarginMs=latchMarginMs(workP99, gpuP99)+missPenalty.value(monotonicSeconds());
        const auto hz=output->refreshHz();
        const auto periodUs=hz?1000000ull/hz:0;
        const auto marginUs=static_cast<std::uint64_t>(lastMarginMs*1000.0);
        const auto next=output->lastVblankUs()+periodUs;
        const auto deadline=next>marginUs?next-marginUs:next;
        while (latchWaiting(static_cast<std::uint64_t>(monotonicSeconds()*1e6), output->lastVblankUs(), hz, lastMarginMs) && !interrupted) {
            if (!ensureLease()) { running=false; break; }
            serviceCaptures();
            if (nearDeadline(deadline)) break;
        }
    }
    bool nearDeadline(std::uint64_t deadline) const {
        timespec now{}; clock_gettime(CLOCK_MONOTONIC, &now);
        const auto nowUs=static_cast<std::uint64_t>(now.tv_sec)*1000000ull+now.tv_nsec/1000;
        if (nowUs+200>=deadline) return true;
        const auto remain=deadline-nowUs;
        if (remain<=2000) {
            timespec abs{}; abs.tv_sec=static_cast<time_t>(deadline/1000000ull); abs.tv_nsec=static_cast<long>((deadline%1000000ull)*1000ull);
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &abs, nullptr);
            return true;
        }
        timespec step{}; step.tv_nsec=2000*1000;
        nanosleep(&step, nullptr);
        return false;
    }
    void reloadLayout() {
        if (canvas) { reloadCanvas(); return; }
        if (layoutPath.empty() || SDL_GetTicks64()<nextLayoutCheck) return;
        nextLayoutCheck=SDL_GetTicks64()+100;
        try {
            const auto version=std::filesystem::last_write_time(layoutPath);
            // The backend replaces this file atomically, including presentation settings.
            if (version!=layoutVersion) applyLiveLayout(version);
        } catch (const std::exception& e) {
            std::cerr << "Live layout update deferred: " << e.what() << std::endl;
            nextLayoutCheck=SDL_GetTicks64()+1000;
        }
    }
    // Canvas mode polls the window list every 100 ms and canvas.tsv every 250 ms, both by mtime.
    void reloadCanvas() {
        if (!windowsPath.empty() && SDL_GetTicks64()>=nextWindowsCheck) { nextWindowsCheck=SDL_GetTicks64()+100; pollWindows(); }
        const double now=monotonicSeconds();
        if (!layoutPath.empty() && now>=nextCanvasCheck) { nextCanvasCheck=now+.25; pollCanvasSettings(); }
    }
    // The .windows file (--canvas-windows-file); a rejected or older list is logged once and the last one stays.
    bool pollWindows() {
        std::error_code missing;
        const auto version=std::filesystem::last_write_time(windowsPath, missing);
        if (missing || version==windowsVersion) return false;
        windowsVersion=version;
        std::ifstream file(windowsPath); const std::string text((std::istreambuf_iterator<char>(file)), {});
        const auto list=windows::parse(text);
        if (!list || !adoptList(*list)) {
            if (!windowsRejected) std::cerr << "Ignoring invalid window list " << windowsPath << "; keeping the last one (format: docs/infinite-canvas-plan.md 3.1)" << std::endl;
            windowsRejected=true; return false;
        }
        return true;
    }
    // One window list from the file or the .windows mailbox; an older sequence number is refused.
    bool adoptList(const windows::List& list) {
        if (list.seq<windowsSeq) return false;
        windowsRejected=false; windowsSeq=list.seq;
        const auto focused=canvas->focusedName;
        const bool stagedChanged=canvas->adopt(list, monotonicSeconds());
        if (canvas->stagedName==restageRequested) restageRequested.clear();
        // The mailbox repeats unchanged lists as a heartbeat: log changes only.
        if (stagedChanged || canvas->live()!=loggedLive) std::cout << "Canvas: " << canvas->live() << " windows, staged " << (canvas->stagedName.empty() ? "none" : canvas->stagedName) << std::endl;
        loggedLive=canvas->live();
        // A window brought from search lands once it is on the canvas.
        if (!canvas->bringRequested.empty() && canvas->find(canvas->bringRequested)) {
            std::cout << "Canvas: brought " << canvas->bringRequested << " to the canvas" << std::endl;
            applyAim(canvas->land(std::exchange(canvas->bringRequested, {}), baseView()));
        }
        // --canvas-windows-file runs only: a new focus_history_id 0 window stands in for the .focus
        // mailbox, which the mailbox path follows (it leaves out XR's own staging, §4.2).
        if (!windowsPath.empty() && !focused.empty() && !canvas->focusedName.empty() && canvas->focusedName!=focused) navigate({.verb=Verb::FitOutput, .output=canvas->focusedName});
        return true;
    }
    // A missing canvas.tsv keeps the current settings; an invalid one is logged once.
    void pollCanvasSettings() {
        std::error_code missing;
        const auto version=std::filesystem::last_write_time(layoutPath, missing);
        if (missing || version==canvasVersion) return;
        canvasVersion=version;
        try {
            std::ifstream file(layoutPath);
            if (canvas->applySettings(canvas::parseSettings(file), monotonicSeconds())) {
                distance=targetDistance=canvas->ring.radius; applyAim(canvas->reaim(baseView()));
                std::cout << "Canvas: ring radius " << canvas->ring.radius << ", gap " << canvas->settings.gapPx << " px" << std::endl;
            }
            if (controls) controls->setTakeover(canvas->settings.takeoverKeys);
            canvasRejected=false;
        } catch (const std::exception& e) {
            if (!canvasRejected) std::cerr << "Ignoring invalid canvas.tsv: " << e.what() << std::endl;
            canvasRejected=true;
        }
    }
    void applyLiveLayout(std::filesystem::file_time_type version) {
        auto layouts=readLayout(layoutPath);
        int nextFps=fps; spatial::Workspace nextWorkspace=workspace; float nextSpacing=spacing;
        readLiveSettings(nextFps, nextWorkspace, nextSpacing);
        adoptLayout(std::move(layouts), nextFps, nextWorkspace, nextSpacing, version);
    }
    void readLiveSettings(int& nextFps, spatial::Workspace& nextWorkspace, float& nextSpacing) const {
        std::ifstream settings(layoutPath); std::string first; std::getline(settings, first);
        if (!first.starts_with("# settings ")) return;
        std::istringstream values(first.substr(11));
        if (!(values>>nextFps>>nextWorkspace.amount>>nextSpacing) || nextFps<1 || nextFps>120 || !std::isfinite(nextWorkspace.amount) || nextWorkspace.amount<0 || nextWorkspace.amount>100 || !std::isfinite(nextSpacing) || nextSpacing<1 || nextSpacing>8192)
            throw std::runtime_error("Invalid live presentation settings");
        nextWorkspace.degrees=-1; values>>std::ws;
        if (!values.eof() && (!(values>>nextWorkspace.degrees) || !std::isfinite(nextWorkspace.degrees) || nextWorkspace.degrees<-1 || nextWorkspace.degrees>360))
            throw std::runtime_error("Workspace wrap must be 0..360 degrees");
        nextWorkspace.follow=false; values>>std::ws;
        if (values.eof()) return;
        // Later renderer versions append fields (scene mode, canvas settings) after the wrap flag;
        // this version ignores anything after follow.
        int follow;
        if (!(values>>follow) || (follow!=0 && follow!=1)) throw std::runtime_error("Invalid workspace wrapping setting");
        nextWorkspace.follow=follow;
    }
    void adoptLayout(std::vector<PanelLayout> layouts, int nextFps, spatial::Workspace nextWorkspace, float nextSpacing, std::filesystem::file_time_type version) {
        nextWorkspace.gap=nextSpacing/900;
        float l,t,r,b; boundsOf(layouts, l, t, r, b);
        const float newCx=(l+r)/2, newCy=(t+b)/2;
        const bool wrapChanged=nextWorkspace.degrees!=workspace.degrees || nextWorkspace.follow!=workspace.follow;
        const float newDistance=spatial::safeDistance(layouts, newCx, newCy, (r-l)/900.f, wrapChanged?5.f:distance, nextWorkspace, nextSpacing);
        // Prepare only new or failed sources before altering the current scene.
        auto updated=replacePanels(layouts, nextFps);
        for (auto& p:panels) if (p.texture) glDeleteTextures(1, &p.texture);
        panels=std::move(updated); geometry=std::move(layouts); result=0;
        // Geometry safety must not change the camera's viewing depth.
        const float geometryShift=newDistance-distance;
        panZ+=geometryShift; targetPanZ+=geometryShift;
        left=l; top=t; right=r; bottom=b; cx=newCx; cy=newCy; distance=newDistance;
        workspace=nextWorkspace; spacing=nextSpacing; fps=nextFps; layoutVersion=version; targetDistance=distance;
        selection.validate(geometry);
        if (!focusOutput.empty()) { if (selection.output.empty()) fit(); else focusSelected(false, 0); }
        std::cout << "Live layout applied: " << panels.size() << " panels, " << fps << " fps" << std::endl;
    }
    static void boundsOf(const std::vector<PanelLayout>& layouts, float& l, float& t, float& r, float& b) {
        if (layouts.empty()) { l=t=r=b=0; return; }
        l=layouts[0].x; t=layouts[0].y; r=l; b=t;
        for (const auto& p:layouts) { l=std::min(l, p.x); t=std::min(t, p.y); r=std::max(r, p.x+p.width); b=std::max(b, p.y+p.height); }
    }
    std::vector<Panel> replacePanels(const std::vector<PanelLayout>& layouts, int nextFps) {
        auto additions=newPanels(layouts, nextFps);
        std::vector<Panel> updated; updated.reserve(layouts.size());
        for (const auto& layout:layouts) {
            auto old=std::find_if(panels.begin(), panels.end(), [&](const Panel& p){ return p.layout.output==layout.output && !p.failed; });
            if (old!=panels.end()) { updated.push_back(std::move(*old)); old->texture=0; }
            else updated.push_back(takeAdded(additions, layout.output));
            updated.back().layout=layout;
            updated.back().capture->setFrameRate(nextFps);
        }
        return updated;
    }
    std::vector<Panel> newPanels(const std::vector<PanelLayout>& layouts, int nextFps) {
        std::vector<Panel> additions;
        for (const auto& layout:layouts) {
            auto old=std::find_if(panels.begin(), panels.end(), [&](const Panel& p){ return p.layout.output==layout.output && !p.failed; });
            if (old!=panels.end()) continue;
            Panel p; p.layout=layout;
            auto next=std::make_unique<DesktopCapture>();
            next->setFrameRate(nextFps);
            if (!next->connect() || !next->select(layout.output)) throw std::runtime_error(next->error());
            p.capture=std::move(next);
            additions.push_back(std::move(p));
        }
        return additions;
    }
    Panel takeAdded(std::vector<Panel>& additions, const std::string& outputName) {
        auto added=std::find_if(additions.begin(), additions.end(), [&](const Panel& p){ return p.layout.output==outputName; });
        Panel taken=std::move(*added);
        glGenTextures(1, &taken.texture); bindTexture(taken.texture);
        return taken;
    }
    void steer() {
        controls->update();
        if (canvas) adoptMailbox();
        panGestureActive=controls->panActive;
        if (controls->panStarted || controls->panX || controls->panY) interactionUntil=monotonicSeconds()+.4;
        if (controls->panStarted || controls->panX || controls->panY) navigate({Verb::Pan, float(controls->panX), float(controls->panY), controls->panStarted});
        sampleTarget();
        if (canvas) updateVirtualCursor();
        if (controls->zoom) navigate({Verb::ZoomBy, float(controls->zoom)});
        if (controls->fit==1) navigate({Verb::FlickOut});
        if (controls->fit==2) navigate({Verb::FlickIn});
        if (canvas && controls->fit>=8) canvasKey(controls->fit, controls->notificationTarget);
        if (notificationHud && (controls->fit==6 || controls->fit==7)) {
            placeNotification(monotonicSeconds());
            notificationHud->flick(controls->notificationTarget,controls->fit==6);
        }
        if (controls->fit==3) tracking.recenterRequested=true;
        if (controls->fit==4) navigate({Verb::ZoomBy, -std::log(.9f)});
        if (controls->fit==5) navigate({Verb::ZoomBy, std::log(.9f)});
        if (tracking.zoom) { navigate({Verb::ZoomBy, float(tracking.zoom)*-std::log(.9f)}); tracking.zoom=0; }
        if (tracking.recenterRequested) { navigate({Verb::Recenter}); tracking.recenterRequested=false; }
        if (tracking.fitRequested) { navigate({Verb::Fit}); tracking.fitRequested=false; }
        if (tracking.fitTargetRequested) { navigate({Verb::FitTarget}); tracking.fitTargetRequested=false; }
        if (canvas) steerCanvas(); else tracking.clearCanvasVerbs();
        if (!controls->focusOutput.empty()) navigate({.verb=Verb::FitOutput, .output=controls->focusOutput});
    }
    // The canvas key set (§5.5, modes 8-17); 11/12 with the token "release" end the switcher.
    void canvasKey(int mode, const std::string& token) {
        switch (mode) {
        case 8: navigate({Verb::Fit}); break;
        case 9: navigate({Verb::Search}); break;
        case 10: navigate({Verb::Fill}); break;
        case 11: case 12: navigate(token=="release" ? Move{.verb=Verb::Switch, .begin=true} : Move{Verb::Switch, mode==11 ? 1.f : -1.f}); break;
        case 13: navigate({Verb::Arrange}); break;
        case 14: navigate({.verb=Verb::Neighbour, .output=token}); break;
        case 15: navigate({.verb=Verb::Nudge, .output=token}); break;
        case 16: navigate({Verb::Pin}); break;
        case 17: navigate({Verb::Help}); break;
        default: break;
        }
    }
    // Pose socket verbs, prompt lines, the switcher's missing-release fallback and dwell under overlays.
    void steerCanvas() {
        const double now=monotonicSeconds();
        steerPoseVerbs();
        if (controls->search) searchLine(*controls->search);
        if (canvas->switcher.active && now-canvas->switcher.lastStep>=1.5) navigate({.verb=Verb::Switch, .begin=true});
        if (canvas->overlayUnderGaze(overlayScene(currentView()))) interactionUntil=std::max(interactionUntil, now+.4);
        syncPrompt();
    }
    void steerPoseVerbs() {
        auto& t=tracking;
        const std::pair<bool PoseSocket::*, Verb> verbs[]={{&PoseSocket::overviewRequested, Verb::Fit}, {&PoseSocket::searchRequested, Verb::Search},
            {&PoseSocket::fillRequested, Verb::Fill}, {&PoseSocket::arrangeRequested, Verb::Arrange}, {&PoseSocket::undoRequested, Verb::Undo},
            {&PoseSocket::redoRequested, Verb::Redo}, {&PoseSocket::pinRequested, Verb::Pin}, {&PoseSocket::helpRequested, Verb::Help}};
        for (const auto& [flag, verb]:verbs) if (std::exchange(t.*flag, false)) navigate({verb});
        if (!t.focusRequested.empty()) navigate({.verb=Verb::Focus, .output=std::exchange(t.focusRequested, {})});
    }
    // A prompt line: its text, then its unseen keys in order (keys of lines overwritten before this poll
    // included); open 0 closes the session keeping the landing. The prompt's second Esc (open 0 with key
    // esc) goes through searchKey, so help closes first and the close reverts. A key that ends the session
    // (Enter) drops the keys after it.
    void searchLine(const windows::Search& line) {
        if (!canvas->search.open) return;
        if (!line.open && line.key!="esc") { canvas->searchClose(false, baseView()); afterCanvasVerb(); return; }
        if (line.keys.empty()) { searchInput(line.text, "-"); return; }
        for (const auto& key:line.keys) if (canvas->search.open) searchInput(line.text, key);
    }
    void searchInput(const std::string& text, const std::string& key) {
        const auto anchor=baseView();
        if (text!=canvas->search.query) applyAim(canvas->searchType(text, anchor));
        if (!key.empty() && key!="-") { explicitMove(monotonicSeconds()); applyAim(canvas->searchKey(key, anchor)); }
        afterCanvasVerb();
    }
    // The .windows mailbox (without --canvas-windows-file); the first list lands on its staged window.
    void adoptMailbox() {
        if (!windowsPath.empty() || !controls->windows) return;
        const bool first=windowsSeq==0;
        if (adoptList(*controls->windows) && first && canvas->staged()) applyAim(canvas->land(canvas->stagedName, baseView()));
    }
    // §3.4: inside the staged window the compositor cursor maps 1:1 onto its quad (the XR cursor); the
    // overflow the adapter carried beyond its edge moves on over the canvas, and a fresh line whose
    // virtual point lies in another window restages that window once.
    void updateVirtualCursor() {
        const double now=monotonicSeconds();
        if (controls->cursor) { lastCursor=controls->cursor; cursorAt=now; }
        xrCursor={};
        const auto* staged=canvas->staged();
        if (!staged || !canvas::Scene::onStage(*staged) || !lastCursor || now-cursorAt>.5) return;
        const auto& r=staged->record; const auto& c=*lastCursor;
        const double scale=canvas->settings.outputScale, lx=c.x-r.atX, ly=c.y-r.atY;
        if (lx>=0 && ly>=0 && lx<r.w && ly<r.h) xrCursor={true, staged->name, float(lx*scale), float(ly*scale)};
        // The prompt holds the keyboard: no restage (and so no pointer warp) until it closes.
        if (!controls->cursor || (!c.overflowX && !c.overflowY) || promptHoldsKeys()) return;
        const float x=canvas->ring.unwrap(staged->rect.x+float((lx+c.overflowX)*scale)), y=staged->rect.y+float((ly+c.overflowY)*scale);
        const auto contains=[&](const canvas::CanvasWindow& w) {
            const float dx=canvas->ring.unwrap(x-w.rect.x), dy=y-w.rect.y;
            return !w.gone && !w.staged && w.rect.w>0 && w.rect.h>0 && dx<w.rect.w && dy>=0 && dy<w.rect.h;
        };
        // Still inside the window already asked for: nothing new, even where windows overlap.
        if (const auto* requested=canvas->find(restageRequested); requested && contains(*requested)) return;
        const auto it=std::find_if(canvas->windows.begin(), canvas->windows.end(), contains);
        if (it==canvas->windows.end()) return;
        std::cout << "Canvas: cursor crossed into " << it->name << std::endl;
        explicitFocus(it->name, canvas->ring.unwrap(x-it->rect.x)*it->pixelW/it->rect.w, (y-it->rect.y)*it->pixelH/it->rect.h);
    }
    // An explicit focus action (§5.6): selection, .hover v4 with one new pointer serial at buffer px,
    // and a restage request until a window list shows the window staged.
    // The search closes first (keeping the camera), so the keyboard goes to the window.
    void explicitFocus(const std::string& name, float px, float py) {
        if (canvas->search.open) { canvas->searchClose(false, baseView()); syncPrompt(); }
        selection.output=name; hoverOutput=name; ++pointerSerial; pointerX=px; pointerY=py;
        restageRequested=name==canvas->stagedName ? std::string() : name;
    }
    // A hit's projected px into window-buffer px (§3.4 step 2).
    void focusHit(const targeting::Hit& hit) {
        const auto* w=canvas->find(hit.output); const auto* l=findLayout(hit.output);
        if (!w || !l || l->width<=0 || l->height<=0) return;
        explicitFocus(hit.output, hit.pixelX*w->pixelW/l->width, hit.pixelY*w->pixelH/l->height);
    }
    // 2D click (§3.4): the pixel ray at (u, v) of the view focuses the window under it; no camera move.
    void clickAt(float u, float v) {
        const float t=std::tan(fov*pi/360), r=t*aspect();
        const auto dir=targeting::rotate(tracking::conjugate(currentView()), targeting::normalize({(2*u-1)*r, (1-2*v)*t, -1}));
        const auto c=sceneCylinder();
        const auto hit=targeting::query({targeting::mul({panX, panY, panZ}, -1), dir}, sceneGeometry(), c.cx, c.cy, c.span, c.distance, c.workspace, &canvas->candidates);
        if (!hit) { std::cout << "Canvas: click hit no window" << std::endl; return; }
        focusHit(*hit);
        std::cout << "Canvas: click focus " << hit->output << " at " << int(pointerX) << "," << int(pointerY) << std::endl;
    }
    void click(const SDL_MouseButtonEvent& button) {
        int w=0, h=0; SDL_GetWindowSize(window, &w, &h);
        float u=float(button.x)/float(std::max(w, 1));
        if (stereo) u=std::fmod(u*2, 1.f);
        clickAt(u, float(button.y)/float(std::max(h, 1)));
    }
    void pollInput() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type==SDL_QUIT) running=false;
            if (event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT && canvas) click(event.button);
            if (event.type==SDL_KEYDOWN) keyDown(event.key.keysym);
            if (event.type==SDL_TEXTINPUT && canvas && promptShown && promptSdl) searchInput(canvas->search.query+event.text.text, "-");
            if (event.type==SDL_MOUSEWHEEL) navigate({Verb::ZoomBy, event.wheel.preciseY*-std::log(.9f)});
            if (event.type==SDL_MOUSEMOTION) mouseMove(event.motion);
        }
    }
    void keyDown(const SDL_Keysym& key) {
        if (canvas) { canvasKeyDown(key); return; }
        if (key.sym==SDLK_ESCAPE) running=false;
        if (key.sym==SDLK_r) navigate({Verb::Recenter});
        if (key.sym==SDLK_f) navigate({Verb::Fit});
    }
    static const char* arrowToken(SDL_Keycode key) {
        switch (key) {
        case SDLK_LEFT: return "left";
        case SDLK_RIGHT: return "right";
        case SDLK_UP: return "up";
        case SDLK_DOWN: return "down";
        default: return nullptr;
        }
    }
    // 2D canvas keys (§5.8): / search, F Fill, O Overview, P pin, R recenter, Tab switcher (Return or
    // 1.5 s end it), Alt(+Shift)+arrows neighbour (nudge), Ctrl+A/Z/Shift+Z, F1; Esc closes overlays, then quits.
    void canvasKeyDown(const SDL_Keysym& key) {
        const bool ctrl=key.mod&KMOD_CTRL, shift=key.mod&KMOD_SHIFT, alt=key.mod&KMOD_ALT;
        if (promptShown && promptSdl) { sdlSearchKey(key.sym, ctrl, shift); return; }
        if (const char* dir=arrowToken(key.sym); dir && alt) { navigate({.verb=shift ? Verb::Nudge : Verb::Neighbour, .output=dir}); return; }
        if (ctrl) {
            if (key.sym==SDLK_a) navigate({Verb::Arrange});
            if (key.sym==SDLK_z) navigate({shift ? Verb::Redo : Verb::Undo});
            return;
        }
        switch (key.sym) {
        case SDLK_ESCAPE: closeOverlayOrQuit(); break;
        case SDLK_SLASH: case SDLK_KP_DIVIDE: navigate({Verb::Search}); break;
        case SDLK_f: navigate({Verb::Fill}); break;
        case SDLK_o: navigate({Verb::Fit}); break;
        case SDLK_p: navigate({Verb::Pin}); break;
        case SDLK_r: navigate({Verb::Recenter}); break;
        case SDLK_TAB: navigate({Verb::Switch, shift ? -1.f : 1.f}); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: if (canvas->switcher.active) navigate({.verb=Verb::Switch, .begin=true}); break;
        case SDLK_F1: navigate({Verb::Help}); break;
        default: break;
        }
    }
    void closeOverlayOrQuit() {
        if (canvas->switcher.active) navigate({.verb=Verb::Switch, .begin=true, .output="cancel"});
        else if (canvas->helpOpen) canvas->helpOpen=false;
        else running=false;
    }
    // Typing goes through SDL_TEXTINPUT; these keys are the prompt's forwarded keys (searchKey).
    void sdlSearchKey(SDL_Keycode sym, bool ctrl, bool shift) {
        std::string key;
        if (sym==SDLK_BACKSPACE) {
            auto query=canvas->search.query;
            while (!query.empty()) { const unsigned char c=query.back(); query.pop_back(); if ((c&0xC0)!=0x80) break; }
            searchInput(query, "-"); return;
        }
        if (ctrl && sym>=SDLK_1 && sym<=SDLK_8) key="ctrl-"+std::string(1, char('1'+(sym-SDLK_1)));
        else if (ctrl && sym==SDLK_a) key="ctrl-a";
        else if (ctrl && sym==SDLK_z) key=shift ? "ctrl-shift-z" : "ctrl-z";
        else if (sym==SDLK_UP || sym==SDLK_DOWN) key=sym==SDLK_UP ? "up" : "down";
        else if (sym==SDLK_TAB) key=shift ? "shift-tab" : "tab";
        else if (sym==SDLK_RETURN || sym==SDLK_KP_ENTER) key=shift ? "shift-enter" : "enter";
        else if (sym==SDLK_ESCAPE) key="esc";
        else if (sym==SDLK_F1) key="f1";
        if (!key.empty()) searchInput(canvas->search.query, key);
    }
    void mouseMove(const SDL_MouseMotionEvent& motion) {
        if (motion.state&SDL_BUTTON_RMASK) { yaw+=motion.xrel*.15f; pitch=std::clamp(pitch+motion.yrel*.15f, -80.f, 80.f); }
        if (canvas) { canvasMousePan(motion); return; }
        if (motion.state&SDL_BUTTON_MMASK) { panX+=motion.xrel*distance*.0015f; panY-=motion.yrel*distance*.0015f; targetPanX=panX; targetPanY=panY; }
    }
    // Middle-drag pans the canvas focus; the first motion of a drag begins the gesture.
    void canvasMousePan(const SDL_MouseMotionEvent& motion) {
        if (!(motion.state&SDL_BUTTON_MMASK)) { mousePanning=false; return; }
        interactionUntil=monotonicSeconds()+.4;
        navigate({Verb::Pan, float(motion.xrel), float(motion.yrel), !mousePanning});
        mousePanning=true;
    }
    float easeCamera() {
        const double cameraTime=monotonicSeconds();
        const float cameraDt=float(cameraTime-lastCameraTime); lastCameraTime=cameraTime;
        if (std::abs(std::log(distance/targetDistance))>1e-5f) distance=navigation::easeDistance(distance, targetDistance, cameraDt);
        else distance=targetDistance;
        navigationRotation=navigation::easeRotation(navigationRotation, targetRotation, cameraDt);
        panX=navigation::ease(panX, targetPanX, cameraDt); panY=navigation::ease(panY, targetPanY, cameraDt);
        panZ=navigation::ease(panZ, targetPanZ, cameraDt);
        if (panCamera) easePan(cameraDt);
        return cameraDt;
    }
    void easePan(float cameraDt) {
        const auto* found=findLayout(focusOutput);
        if (!found) { panCamera=false; return; }
        const auto pose=monitorPose(*found);
        smoothFocusX=navigation::ease(smoothFocusX, focusX, cameraDt);
        smoothFocusY=navigation::ease(smoothFocusY, focusY, cameraDt);
        // Interpolate surface coordinates, then derive the camera: the entire path keeps constant zoom.
        const auto camera=navigation::panFocus(pose, focusAnchor, focusDepth, smoothFocusX, smoothFocusY);
        navigationRotation=targetRotation=camera.rotation;
        panX=targetPanX=camera.pan.x; panY=targetPanY=camera.pan.y; panZ=targetPanZ=camera.pan.z;
    }
    void showTracking() {
        if (posePath.empty()) return;
        const int state=tracking.camera.fresh(monotonicSeconds()) ? 1 : 0;
        if (state==trackingStatus) return;
        trackingStatus=state;
        std::cout << "Head tracking: " << (state ? "live" : "waiting / stale; holding view") << std::endl;
        if (window && mode==SceneMode::Canvas) SDL_SetWindowTitle(window, (std::string(state ? "Omarchy XR canvas | Head tracking live | " : "Omarchy XR canvas | Tracking unavailable | Mouse look | ")+canvasKeys+" | Esc: exit").c_str());
        else if (window) SDL_SetWindowTitle(window, state ? "Omarchy XR | Head tracking live | R: recenter | F: fit | Esc: exit" : "Omarchy XR | Tracking unavailable | Mouse look | R: recenter | Esc: exit");
    }
    void projectPanels(int viewportWidth, int viewportHeight, double cameraTime) {
        if (canvas) { projectWindows(viewportWidth, viewportHeight, cameraTime); return; }
        const auto view=currentView();
        for (auto& p:panels) {
            const auto& l=p.layout;
            const auto pose=monitorPose(l);
            const auto plan=adaptive::project(l, pose, view, {panX, panY, panZ}, std::max(1, viewportWidth/(stereo?2:1)), std::max(1, viewportHeight), fov, stereo?ipd/2000:0);
            p.visible=smoke || plan.visible;
            const float scale=p.quality.update(plan.scale, cameraTime);
            if (p.capture) p.capture->setDemand(p.visible, std::max(1u, unsigned(std::ceil(l.width*scale))), std::max(1u, unsigned(std::ceil(l.height*scale))));
        }
    }
    // Only the culled candidates are projected; the governor then sets every window's rate and demand.
    void projectWindows(int viewportWidth, int viewportHeight, double cameraTime) {
        const auto view=currentView(); const auto c=sceneCylinder();
        for (auto& w:canvas->windows) w.visible=false;
        for (auto i:canvas->candidates) {
            auto& w=canvas->windows[i]; const auto& l=canvas->projected[i];
            const auto plan=adaptive::project(l, c.pose(l), view, {panX, panY, panZ}, std::max(1, viewportWidth/(stereo?2:1)), std::max(1, viewportHeight), fov, stereo?ipd/2000:0);
            w.visible=!w.gone && (smoke || plan.visible);
            const float scale=w.quality.update(plan.scale, cameraTime);
            w.demandW=std::max(1u, unsigned(std::ceil(l.width*scale))); w.demandH=std::max(1u, unsigned(std::ceil(l.height*scale)));
        }
        scheduleCanvas(monotonicSeconds());
    }
    // The ladder's rates, its sliver set to Lua (.tiers, rate-limited there) and a log line per step change.
    void scheduleCanvas(double now) {
        canvas->schedule(canvas->zoomedOut(), now);
        if (controls) controls->publishTiers(canvas->sliverAddresses(), now);
        auto ladder=canvas->ladderSummary();
        if (ladder!=ladderLogged) { std::cout << "Canvas: ladder " << ladder << std::endl; ladderLogged=std::move(ladder); }
    }
    // One canvas step per tick: FOV-derived metrics, camera easing, projection and the cull.
    void stepCanvas(float cameraDt) {
        canvas->setFov(canvasFov());
        canvas->gazed=gaze.current ? gaze.current->output : std::string(); canvas->selected=selection.output;
        canvas->tick(monotonicSeconds(), cameraDt);
        canvas->cull(headingDeg(currentView()), canvasFov().horizontal()/2);
    }
    void renderScene(int width, int height, bool stereoView, bool flipped, int drawableW, int drawableH) {
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        int offsetX=0, offsetY=0;
        if (flipped) letterbox(width, height, drawableW, drawableH, offsetX, offsetY);
        const int eyes=stereoView?2:1, eyeWidth=width/eyes;
        const auto view=currentView();
        for (int eye=0; eye<eyes; ++eye) drawEye(eye, eyeWidth, height, offsetX, offsetY, stereoView, flipped, view);
    }
    void letterbox(int& width, int& height, int drawableW, int drawableH, int& offsetX, int& offsetY) const {
        const float ratio=float(drawableW/(stereo?2:1))/drawableH;
        const int contentWidth=std::min(width, int(height*ratio));
        const int contentHeight=std::min(height, int(width/ratio));
        offsetX=(width-contentWidth)/2; offsetY=(height-contentHeight)/2;
        width=contentWidth; height=contentHeight;
    }
    void drawEye(int eye, int eyeWidth, int height, int offsetX, int offsetY, bool stereoView, bool flipped, const tracking::Quaternion& view) {
        const float eyePosition=stereoView?(eye==0?-ipd/2000.f:ipd/2000.f):0;
        glViewport(offsetX+eye*eyeWidth, offsetY, eyeWidth, height);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        const double t=.1*std::tan(fov*pi/360), r=t*eyeWidth/height;
        glFrustum(-r, r, flipped?t:-t, flipped?-t:t, .1, 20000);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        if (!skyHidden(eyePosition, float(t/.1), float(r/.1))) environment->draw(tracking::matrix(view).data(), accent.rgb, eyePosition);
        else ++skyCulled;
        glTranslatef(-eyePosition, 0, 0);
        glMultMatrixf(tracking::matrix(view).data()); glTranslatef(panX, panY, panZ);
        if (canvas) {
            canvas->cull(headingDeg(view), canvasFov().horizontal()/2);
            drawSurfaces([&](const auto& visit){ canvas->forEachCandidate(visit); });
            drawCanvasLabels();
            drawXrCursor();
            drawCanvasOverlays(view);
        } else drawSurfaces([&](const auto& visit){ forEachSurface(visit); });
        if(stereoView && notificationHud) notificationHud->draw(lastCameraTime);
    }
    // All halos first, then all surfaces, so no halo draws over a neighbouring surface.
    // candidates(visit) yields the surfaces to draw: every panel here, the culled windows in canvas mode.
    template<class Walk> void drawSurfaces(Walk&& candidates) {
        const auto c=sceneCylinder();
        candidates([&](const SurfaceView& s){ if (s.visible) drawHalo(halo, accent.rgb, s, c); });
        candidates([&](const SurfaceView& s){ if (s.visible) drawPanel(s, c); });
    }
    // Overview labels over everything on the ring, before the HUD: depth test off, premultiplied blend.
    void drawCanvasLabels() {
        const auto quads=canvas->labelQuads();
        if (quads.empty()) return;
        const auto c=sceneCylinder();
        glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT|GL_TRANSFORM_BIT);
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_TEXTURE_2D);
        glMatrixMode(GL_TEXTURE);
        for (const auto& q:quads) {
            // A label wider than its window shows its left part: scale u instead of squashing the text.
            glLoadIdentity(); glScalef(q.u, 1, 1);
            glBindTexture(GL_TEXTURE_2D, q.texture); glColor4f(q.alpha, q.alpha, q.alpha, q.alpha);
            const float w=q.layout.width/900, h=q.layout.height/900;
            surface(c.pose(q.layout), -w/2, -h/2, w, h, .01f);
        }
        glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
        glPopAttrib();
    }
    // Body-locked overlays and pinned windows after everything on the ring, before the HUD: the same
    // quads in stereo, the 2D window and the spectator (§4.5 item 3). The eye as in placeNotification.
    notifications::space::Scene overlayScene(const tracking::Quaternion& view) const {
        notifications::space::Scene scene;
        scene.view=view; scene.eye={-panX, -panY, -panZ};
        scene.tanV=std::tan(fov*pi/360); scene.tanH=scene.tanV*aspect(); scene.ipd=ipd/1000;
        return scene;
    }
    void drawCanvasOverlays(const tracking::Quaternion& view) {
        const auto scene=overlayScene(view);
        canvas->overlays.style.accent={accent.rgb[0], accent.rgb[1], accent.rgb[2], 1};
        for (const auto& q:canvas->overlayQuads(scene, lastCameraTime)) canvas::overlay::drawQuad(q.texture, scene, q.centre, q.width, q.height, q.alpha, q.opaque);
    }
    // The staged window's layout while the XR cursor shows; a shown region frame already carries the native cursor.
    const PanelLayout* xrCursorLayout() const {
        const auto* staged=canvas->staged(); const auto* l=staged ? findLayout(staged->name) : nullptr;
        if (!xrCursor.valid || !l || !staged->visible || staged->regionShown || xrCursor.window!=staged->name || !staged->pixelW || !staged->pixelH) return nullptr;
        return l;
    }
    // The XR cursor over the staged window: a 16x24 window-buffer-px arrow, tip at the cursor, above the labels.
    void drawXrCursor() {
        const auto* l=xrCursorLayout();
        if (!l) return;
        const auto* staged=canvas->staged();
        if (!cursorTexture) cursorTexture=cursorArrow();
        const float sx=l->width/staged->pixelW/900, sy=l->height/staged->pixelH/900;
        const float w=cursorW*sx, h=cursorH*sy, x=-l->width/1800+xrCursor.px*sx, y=l->height/1800-xrCursor.py*sy;
        glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT);
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, cursorTexture); glColor4f(1, 1, 1, 1);
        surface(sceneCylinder().pose(*l), x, y-h, w, h, .02f);
        glPopAttrib();
    }
    static constexpr int cursorW=16, cursorH=24;
    // A pointer arrow (premultiplied BGRA): white inside, a dark 1 px rim.
    static GLuint cursorArrow() {
        static constexpr float poly[][2]={{0,0},{0,17},{4,13},{7,20},{9,19},{6,12},{12,12}};
        const auto inside=[](float x, float y) {
            bool in=false;
            for (size_t i=0, j=std::size(poly)-1; i<std::size(poly); j=i++)
                if ((poly[i][1]>y)!=(poly[j][1]>y) && x<(poly[j][0]-poly[i][0])*(y-poly[i][1])/(poly[j][1]-poly[i][1])+poly[i][0]) in=!in;
            return in;
        };
        std::vector<unsigned char> pixels(cursorW*cursorH*4, 0);
        for (int y=0;y<cursorH;++y) for (int x=0;x<cursorW;++x) {
            const float cx=x+.5f, cy=y+.5f;
            if (!inside(cx, cy)) continue;
            const bool rim=!inside(cx-1, cy) || !inside(cx+1, cy) || !inside(cx, cy-1) || !inside(cx, cy+1);
            const unsigned char value=rim ? 24 : 255;
            auto* p=&pixels[(y*cursorW+x)*4]; p[0]=p[1]=p[2]=value; p[3]=255;
        }
        return gltex::uploadBgra(pixels.data(), cursorW, cursorH);
    }
    // One opaque panel filling this eye makes the full-screen sky draw pointless.
    bool skyHidden(float eyePosition, float tanV, float tanH) const {
        if (!environment->visible()) return false;
        const auto view=currentView();
        const auto c=sceneCylinder();
        if (canvas) {
            const auto* staged=canvas->staged(); const auto* l=staged ? findLayout(staged->name) : nullptr;
            return l && staged->visible && occlusion::panelCoversEye(*l, c.pose(*l), view, {panX, panY, panZ}, eyePosition, tanH, tanV);
        }
        for (const auto& p:panels) {
            if (!p.visible) continue;
            const auto& l=p.layout;
            const auto pose=c.pose(l);
            if (occlusion::panelCoversEye(l, pose, view, {panX, panY, panZ}, eyePosition, tanH, tanV)) return true;
        }
        return false;
    }
    void presentSpectator(int drawableW, int drawableH) {
        if (tracking.spectator>=0) { spectatorEnabled=tracking.spectator==1; tracking.spectator=-1; spectatorError.clear(); }
        if (!spectatorEnabled) spectator.reset();
        if (!spectatorEnabled) return;
        try {
            if (!spectator) spectator=std::make_unique<Spectator>();
            if (!spectator->pump()) { spectator.reset(); spectatorEnabled=false; return; }
            if (!spectator->begin(monotonicSeconds(), governor.interval())) return;
            const bool timed=gpuTimers.begin(GpuTimers::Spectator);
            renderScene(spectator->width(), spectator->height(), false, true, drawableW, drawableH);
            if (timed) gpuTimers.end(GpuTimers::Spectator);
            spectator->present();
        } catch (const std::exception& e) {
            spectatorError=e.what(); std::cerr << spectatorError << std::endl;
            glBindFramebuffer(GL_FRAMEBUFFER, 0); spectator.reset(); spectatorEnabled=false;
            while (glGetError()!=GL_NO_ERROR) {} // optional window failure must not stop stereo
        }
    }
    // The selection rim comes in within about 120 ms; the camera easing would take half a second.
    void easeHalos(float cameraDt) {
        for (auto& p:panels) { const float target=selection.output==p.layout.output ? 1.f:0.f; p.halo+=(target-p.halo)*std::min(1.f, cameraDt/.12f); }
        if (canvas) for (auto& w:canvas->windows) { const float target=canvas->haloTarget(w.name); w.halo+=(target-w.halo)*std::min(1.f, cameraDt/.12f); }
    }
    bool draw(float cameraDt, int w, int h) {
        easeHalos(cameraDt);
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        environment->update(monotonicSeconds());
        // The spectator render queues ahead of the stereo scene, so both must finish before the flip;
        // they are timed one after the other and summed for the latch margin.
        presentSpectator(w, h);
        const bool timeScene=gpuTimers.begin(GpuTimers::Scene);
        renderScene(w, h, stereo, false, w, h);
        if (timeScene) gpuTimers.end(GpuTimers::Scene);
        if (!glOk()) return false;
        validateStereo(w/(stereo?2:1), h);
        return true;
    }
    bool glOk() {
        GLenum glError=GL_NO_ERROR; while (GLenum err=glGetError()) glError=err;
        if (glError==GL_NO_ERROR) { glStreak=0; return true; }
        std::cerr << "OpenGL rendering error " << glError << '\n';
        if (++glStreak>=30) { result=2; return false; }
        return true;
    }
    void validateStereo(int eyeWidth, int h) {
        if (!(smoke && stereo && drawn==9)) return;
        std::vector<unsigned char> leftEye(eyeWidth*h*3), rightEye(leftEye.size());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, eyeWidth, h, GL_RGB, GL_UNSIGNED_BYTE, leftEye.data());
        glReadPixels(eyeWidth, 0, eyeWidth, h, GL_RGB, GL_UNSIGNED_BYTE, rightEye.data());
        if (leftEye==rightEye) throw std::runtime_error("Stereo validation failed: eye images are identical");
        std::cout << "Stereo validation: distinct left/right images" << std::endl;
    }
    bool swapFrame(double& workMs) {
        try {
            if (output) output->swap([&]{ const double startedAt=monotonicSeconds(); updateCaptures(); workMs+=(monotonicSeconds()-startedAt)*1000; });
            else SDL_GL_SwapWindow(window);
            swapStreak=0; return true;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            if (++swapStreak>=5) { result=3; return false; }
            return true;
        }
    }
    void recordWork(double workMs, double frameStarted) {
        workTimes.push_back(workMs); if (workMs>workMax) workMax=workMs; ++drawn; ++reportFrames;
        latchWork.push_back(workMs); if (latchWork.size()>120) latchWork.erase(latchWork.begin());
        if (!latchWork.empty()) { auto ranked=latchWork; std::sort(ranked.begin(), ranked.end()); workP99=ranked[(ranked.size()-1)*99/100]; }
        const double now=monotonicSeconds(); lastFrameMs=(now-frameStarted)*1000; frameTimes.push_back(lastFrameMs);
        if (output) { const unsigned missedNow=output->missedVblanks(); if (missedNow>seenMisses) { missPenalty.miss(now, missedNow-seenMisses); seenMisses=missedNow; } }
        if (now-reportTime>=5) report(now);
        if (smoke && drawn>=10 && (canvas ? canvas->smokeDone() : std::all_of(panels.begin(), panels.end(), [](const Panel& p){ return !p.capture || p.frames>=10; }))) running=false;
    }
    void report(double now) {
        std::sort(workTimes.begin(), workTimes.end()); std::sort(frameTimes.begin(), frameTimes.end());
        const double workP95=workTimes[workTimes.size()*95/100], frameP95=frameTimes[frameTimes.size()*95/100];
        const bool gpu=!gpuCaptureTimes.empty() && gpuCaptureTimes.size()==gpuSceneTimes.size() && gpuSpectatorTimes.size()==gpuSceneTimes.size();
        if (gpu) { std::sort(gpuCaptureTimes.begin(), gpuCaptureTimes.end()); std::sort(gpuSpectatorTimes.begin(), gpuSpectatorTimes.end()); std::sort(gpuSceneTimes.begin(), gpuSceneTimes.end()); }
        const double gpuCaptureP95=gpu?gpuCaptureTimes[gpuCaptureTimes.size()*95/100]:0;
        const double gpuSpectatorP95=gpu?gpuSpectatorTimes[gpuSpectatorTimes.size()*95/100]:0;
        const double gpuSceneP95=gpu?gpuSceneTimes[gpuSceneTimes.size()*95/100]:0;
        const unsigned missed=output?output->missedVblanks():0;
        printPerformance(now, workP95, frameP95, gpu, gpuCaptureP95, gpuSpectatorP95, gpuSceneP95, missed);
        writeStats(now, workP95, frameP95, gpu, gpuCaptureP95, gpuSpectatorP95, gpuSceneP95, missed);
        if (canvas) for (auto& w:canvas->windows) w.reportFrames=w.stageReportFrames=0;
        missedBaseline=missed; reportTime=now; reportFrames=0; workMax=0; skyCulled=0; workTimes.clear(); frameTimes.clear(); gpuCaptureTimes.clear(); gpuSpectatorTimes.clear(); gpuSceneTimes.clear();
    }
    void printPerformance(double now, double workP95, double frameP95, bool gpu, double gpuCaptureP95, double gpuSpectatorP95, double gpuSceneP95, unsigned missed) const {
        std::cout << "Performance: " << reportFrames/(now-reportTime) << " present fps, work p95 " << workP95 << " ms, work max " << workMax << " ms, frame p95 " << frameP95 << " ms";
        if (gpu) std::cout << ", gpu capture p95 " << gpuCaptureP95 << " ms, gpu spectator p95 " << gpuSpectatorP95 << " ms, gpu scene p95 " << gpuSceneP95 << " ms";
        if (spectator) std::cout << ", spectator " << governor.name();
        std::cout << ", sky skipped " << skyCulled << " eye draws";
        if (output) std::cout << ", missed vblanks " << missed-missedBaseline << " (" << missed << " session)";
        std::cout << std::endl;
        if (canvas) { printBudget(); printWindowCaptures(now); }
        for (const auto& p:panels) if (p.capture) std::cout << "Capture: " << p.layout.output << " " << (p.visible?"visible":"paused") << " " << p.capture->transport() << " " << p.width << "x" << p.height << " source " << p.sourceWidth << "x" << p.sourceHeight << " requests " << p.capture->requests() << " frames " << p.frames << std::endl;
    }
    void printBudget() const {
        const auto& g=canvas->governor; const auto& b=g.budget;
        std::cout << "Budget: " << g.usedMpix << "/" << b.effective() << " Mpix/s (set " << b.setMpix << ", calibration " << b.calibration << ", gpu steps " << b.gpuSteps
                  << (b.panic ? ", panic" : "") << "), request->ready p50 " << b.readyP50Ms << " ms, VRAM ~" << std::lround(g.vramBytes/(1<<20)) << " MB, slivers " << canvas->sliverCount() << std::endl;
    }
    void printWindowCaptures(double now) const {
        for (const auto& w:canvas->windows) {
            if (w.gone) continue;
            std::cout << "Capture: " << w.name << " " << canvas::tierName(w.decision.tier) << " " << w.decision.rateHz << " Hz " << w.reportFrames/(now-reportTime) << " fps "
                      << (w.source ? w.source->transport() : "none") << " " << w.width << "x" << w.height << " source " << w.sourceWidth << "x" << w.sourceHeight
                      << " requests " << (w.source ? w.source->requests() : 0) << " frames " << w.frames;
            if (!w.captureStatus.empty()) std::cout << " (" << w.captureStatus << ")";
            if (w.stageSource) std::cout << (w.regionShown ? " (region " : " (region idle ") << w.stageSource->transport() << " " << w.stageReportFrames/(now-reportTime) << " fps, request to ready " << w.stageSource->requestToReadyMs() << " ms)";
            std::cout << std::endl;
        }
    }
    // Canvas stats never touch monitor math: depth is the eye dolly inside the ring of radius R.
    float statsZoomDepth() const { return canvas ? canvas->ring.radius-std::hypot(panX, panY, panZ) : (focusOutput.empty()?distance-panZ:focusDepth); }
    float statsMaxZoomDepth() const { return canvas ? canvas->ring.radius : maxZoomDepth(); }
    const char* zoomLevelName() const {
        if (canvas) return canvas->zoomedOut() ? "overview" : canvas->state==canvas::Scene::State::Fill ? "pane" : "window";
        return level==Level::Overview ? "workspace" : level==Level::Monitor ? "monitor" : "pane";
    }
    void writeCanvasStats(std::ostringstream& stats) const {
        using canvas::Scene; using governor::Tier;
        const char* state=canvas->state==Scene::State::Overview ? "overview" : canvas->state==Scene::State::Search ? "search" : canvas->state==Scene::State::Fill ? "fill" : "work";
        stats << ",\"canvasWindows\":" << canvas->live() << ",\"canvasState\":" << std::quoted(state)
            << ",\"searchOpen\":" << (canvas->search.open?"true":"false") << ",\"pinned\":" << canvas->pinnedCount()
            << ",\"tiers\":{\"focused\":" << canvas->tierCount(Tier::Focused) << ",\"near\":" << canvas->tierCount(Tier::Near) << ",\"far\":" << canvas->tierCount(Tier::Far)
            << ",\"overview\":" << canvas->tierCount(Tier::Overview) << ",\"idle\":" << canvas->tierCount(Tier::Idle) << "}";
        writeBudgetStats(stats);
    }
    // The ladder's budget (§4.4): the Studio setting, what calibration and GPU feedback leave of it, the use.
    void writeBudgetStats(std::ostringstream& stats) const {
        const auto& g=canvas->governor; const auto& b=g.budget;
        stats << ",\"budget\":{\"setMpix\":" << b.setMpix << ",\"effectiveMpix\":" << b.effective() << ",\"usedMpix\":" << g.usedMpix
            << ",\"calibration\":" << b.calibration << ",\"gpuSteps\":" << b.gpuSteps << ",\"panic\":" << (b.panic?"true":"false")
            << ",\"readyP50Ms\":" << b.readyP50Ms << ",\"vramMB\":" << g.vramBytes/(1<<20) << ",\"slivers\":" << canvas->sliverCount() << "}";
    }
    void writeWindowStats(std::ostringstream& stats, double now) const {
        bool first=true;
        for (const auto& w:canvas->windows) {
            if (w.gone) continue;
            if (!first) stats << ",";
            first=false;
            const double ready=w.source ? w.source->requestToReadyMs() : -1;
            stats << "{\"output\":" << std::quoted(w.name) << ",\"tier\":" << std::quoted(canvas::tierName(w.decision.tier)) << ",\"rateHz\":" << w.decision.rateHz
                << ",\"place\":" << std::quoted(canvas::placeName(w.decision.place)) << ",\"inFlight\":" << w.decision.inFlight
                << ",\"fps\":" << w.reportFrames/std::max(now-reportTime, 1e-3) << ",\"visible\":" << (w.visible?"true":"false") << ",\"transport\":" << std::quoted(w.source ? w.source->transport() : "none")
                << ",\"width\":" << w.width << ",\"height\":" << w.height << ",\"nativeWidth\":" << w.sourceWidth << ",\"nativeHeight\":" << w.sourceHeight << ",\"frames\":" << w.frames;
            if (ready>=0) stats << ",\"requestToReadyMs\":" << ready;
            if (!w.captureStatus.empty()) stats << ",\"status\":" << std::quoted(w.captureStatus);
            if (w.stageSource) writeStageStats(stats, w, now);
            stats << "}";
        }
    }
    // The staged window's region source next to the export, so a run shows which one is cheaper.
    void writeStageStats(std::ostringstream& stats, const canvas::CanvasWindow& w, double now) const {
        const double ready=w.stageSource->requestToReadyMs();
        stats << ",\"stage\":{\"transport\":" << std::quoted(w.stageSource->transport()) << ",\"shown\":" << (w.regionShown?"true":"false")
            << ",\"fps\":" << w.stageReportFrames/std::max(now-reportTime, 1e-3) << ",\"frames\":" << w.stageFrames;
        if (ready>=0) stats << ",\"requestToReadyMs\":" << ready;
        stats << "}";
    }
    void writeStats(double now, double workP95, double frameP95, bool gpu, double gpuCaptureP95, double gpuSpectatorP95, double gpuSceneP95, unsigned missed) const {
        if (posePath.empty()) return;
        std::ostringstream stats;
        stats << "{\"pid\":" << getpid() << ",\"time\":" << std::setprecision(12) << now << ",\"fps\":" << reportFrames/(now-reportTime)
            << ",\"mode\":" << std::quoted(mode==SceneMode::Monitors?"monitors":"canvas")
            << ",\"environmentLoading\":" << (environment->loadingImage()?"true":"false") << ",\"environmentError\":" << std::quoted(environment->error)
            << ",\"geometryDistance\":" << distance
            << ",\"zoomDepth\":" << statsZoomDepth() << ",\"maxZoomDepth\":" << statsMaxZoomDepth() << ",\"panX\":" << focusX << ",\"panY\":" << focusY
            << ",\"spectator\":" << (spectator?"true":"false") << ",\"spectatorFrames\":" << (spectator?spectator->frames:0) << ",\"spectatorError\":" << std::quoted(spectatorError)
            << ",\"workP95\":" << workP95 << ",\"workMax\":" << workMax << ",\"frameP95\":" << frameP95 << ",\"predictionMs\":" << lastPredictionMs << ",\"predictionCapMs\":" << tracking.camera.prediction.horizonMs
            << ",\"filterCutoffHz\":" << tracking.camera.cutoffHz << ",\"motionCoherence\":" << tracking.camera.coherence
            << ",\"headSpeed\":" << tracking.camera.headSpeed() << ",\"dwellFraction\":" << dwell.fraction(now) << ",\"pointerSerial\":" << pointerSerial
            << ",\"zoomLevel\":" << std::quoted(zoomLevelName())
            << ",\"notificationVisible\":" << (notificationHud && notificationHud->visible()?"true":"false")
            << ",\"notificationHighlighted\":" << (notificationHud && !notificationHud->highlight().empty()?"true":"false")
            << ",\"notificationCount\":" << (notificationHud?notificationHud->count():0)
            << ",\"notificationInView\":" << (notificationHud && notificationHud->placement().onscreen?"true":"false")
            << ",\"activePane\":" << (controls->paneValid ? "\""+controls->paneOutput+"\"" : std::string("null"))
            << ",\"latchMarginMs\":" << lastMarginMs << ",\"latchPenaltyMs\":" << missPenalty.ms;
        if (gpu) stats << ",\"gpuCaptureP95\":" << gpuCaptureP95 << ",\"gpuSpectatorP95\":" << gpuSpectatorP95 << ",\"gpuSceneP95\":" << gpuSceneP95;
        stats << ",\"spectatorRate\":" << std::quoted(governor.name()) << ",\"skyCulledEyeDraws\":" << skyCulled;
        if (output) stats << ",\"refreshHz\":" << output->refreshHz() << ",\"missedVblanks\":" << missed << ",\"missedVblanksWindow\":" << missed-missedBaseline;
        if (canvas) writeCanvasStats(stats);
        stats << ",\"captures\":[";
        if (canvas) writeWindowStats(stats, now); else writeCaptureStats(stats);
        stats << "]}";
        AsyncFile::instance().write(posePath+".stats", stats.str());
    }
    void writeCaptureStats(std::ostringstream& stats) const {
        bool firstCapture=true;
        for (const auto& p:panels) if (p.capture) {
            if (!firstCapture) stats << ",";
            firstCapture=false;
            const double importMs=p.capture->importLatencyMs();
            stats << "{\"output\":" << std::quoted(p.layout.output) << ",\"visible\":" << (p.visible?"true":"false") << ",\"transport\":" << std::quoted(p.capture->transport())
                << ",\"width\":" << p.width << ",\"height\":" << p.height << ",\"nativeWidth\":" << p.sourceWidth << ",\"nativeHeight\":" << p.sourceHeight;
            if (importMs>=0) stats << ",\"importMs\":" << importMs;
            if (!p.captureStatus.empty()) stats << ",\"status\":" << std::quoted(p.captureStatus);
            stats << "}";
        }
    }
    void sceneBounds() {
        ++monitorMathCalls;
        left=top=right=bottom=0;
        if (!panels.empty()) { left=panels[0].layout.x; top=panels[0].layout.y; right=left; bottom=top; }
        for (auto& p:panels) {
            left=std::min(left, p.layout.x); top=std::min(top, p.layout.y);
            right=std::max(right, p.layout.x+p.layout.width); bottom=std::max(bottom, p.layout.y+p.layout.height);
            glGenTextures(1, &p.texture); bindTexture(p.texture);
        }
        cx=(left+right)/2; cy=(top+bottom)/2;
        for (const auto& p:panels) geometry.push_back(p.layout);
        workspace.gap=spacing/900;
    }
    void primeCamera() {
        if (canvas) distance=targetDistance=canvas->ring.radius;
        else { distance=safe(distance); targetDistance=distance; fit(); panZ=targetPanZ; }
        controls.emplace(posePath);
        controls->setCanvasMode(bool(canvas));
        lastCameraTime=monotonicSeconds();
        glEnable(GL_DEPTH_TEST);
        started=SDL_GetTicks64();
        if (!layoutPath.empty() && !canvas) layoutVersion=std::filesystem::last_write_time(layoutPath);
        reportTime=monotonicSeconds();
        gpuTimers.probe();
        missedBaseline=output?output->missedVblanks():0; seenMisses=missedBaseline;
        lastMarginMs=latchMarginMs(0, gpuP99);
        // Optional, live-reloaded stabilisation settings; see tracking::Prediction.
        trackingPath=layoutPath.empty() ? "" : (std::filesystem::path(layoutPath).parent_path()/"tracking.tsv").string();
        describePrediction("defaults");
        gazePath=layoutPath.empty() ? "" : (std::filesystem::path(layoutPath).parent_path()/"gaze.tsv").string();
        describeGaze("defaults");
    }
    bool tick() {
        const double frameStarted=monotonicSeconds();
        if (!ensureLease()) return false;
        waitForPose();
        if (!running) return false;
        const double workStarted=monotonicSeconds();
        reconnectCaptures();
        reloadTracking(workStarted);
        reloadGaze(workStarted);
        accent.update(workStarted);
        tracking.update();
        if(notificationHud) {
            notificationHud->update(tracking.camera,workStarted);
            if(notificationHud->interacting(workStarted)) interactionUntil=workStarted+.25;
        }
        predictPose();
        reloadLayout();
        steer();
        pollInput();
        const float cameraDt=easeCamera();
        if (canvas) stepCanvas(cameraDt);
        sampleTarget();
        placeNotification(workStarted);
        controls->publishNotification(notificationHud?notificationHud->highlight():std::string{});
        showTracking();
        int viewportWidth, viewportHeight; drawable(viewportWidth, viewportHeight);
        int w, h; drawable(w, h);
        if (w<=0 || h<=0) { SDL_Delay(16); return true; }
        // Share exactly the halo target; the compositor consumes monitor transitions only. Canvas mode
        // publishes explicit focus actions, windowed too (§3.4).
        // While the Quickshell prompt holds the keyboard, hover is off (no pointer warp, §5.4).
        controls->publishHover(canvas ? !hoverOutput.empty() && !promptHoldsKeys() : (direct && !selection.output.empty()), canvas ? hoverOutput : selection.output, 0, 0, pointerSerial, pointerX, pointerY);
        projectPanels(viewportWidth, viewportHeight, lastCameraTime);
        collectGpu();
        const bool timeCapture=gpuTimers.begin(GpuTimers::Capture);
        updateCaptures();
        if (timeCapture) gpuTimers.end(GpuTimers::Capture);
        if (smoke && (result || SDL_GetTicks64()-started>15000)) { result=1; return false; }
        if (!draw(cameraDt, w, h)) return false;
        double workMs=(monotonicSeconds()-workStarted)*1000;
        if (!swapFrame(workMs)) return false;
        recordWork(workMs, frameStarted);
        if (!output && SDL_GL_GetSwapInterval()==0) SDL_Delay(1);
        return true;
    }
    void placeNotification(double now) {
        if(!notificationHud || !notificationHud->visible())return;
        notifications::space::Scene scene;
        scene.view=currentView();scene.eye={-panX,-panY,-panZ};
        scene.tanV=std::tan(fov*pi/360);scene.tanH=scene.tanV*aspect();scene.ipd=ipd/1000;
        if(canvas){
            // Cards berth in front of the staged window, or at 0.85 R, among the nearest windows only.
            const auto* staged=canvas->staged();const auto* panel=staged?findLayout(staged->name):nullptr;
            scene.depth=panel?notifications::space::length(targeting::sub(monitorPose(*panel).center,scene.eye)):.85f*canvas->ring.radius;
            scene.tessellate(canvas->occluders(),sceneCylinder());
        }else{
            scene.depth=std::max(.85f,distance-panZ);
            const std::string& focused=focusOutput.empty()?selection.output:focusOutput;
            if(const auto* panel=findLayout(focused))scene.depth=notifications::space::length(targeting::sub(monitorPose(*panel).center,scene.eye));
            scene.tessellate(sceneGeometry(),sceneCylinder());
        }
        notificationHud->place(std::move(scene),now);
    }
    void collectGpu() {
        const auto gpuBefore=gpuSceneTimes.size();
        gpuTimers.collect(gpuCaptureTimes, gpuSpectatorTimes, gpuSceneTimes);
        const double periodMs=output && output->refreshHz() ? 1000.0/output->refreshHz() : 1000.0/60;
        for (auto i=gpuBefore; i<gpuSceneTimes.size(); ++i) {
            const double frameGpu=gpuSpectatorTimes[i]+gpuSceneTimes[i];
            latchGpu.push_back(frameGpu);
            if (canvas) canvas->noteGpu(frameGpu, periodMs, monotonicSeconds());
            const auto before=governor.level;
            if (governor.update(frameGpu, periodMs, monotonicSeconds())!=before)
                std::cout << "Spectator: " << governor.name() << " (frame GPU time " << frameGpu << " ms of " << periodMs << ")" << std::endl;
        }
        while (latchGpu.size()>120) latchGpu.erase(latchGpu.begin());
        if (latchGpu.empty()) return;
        auto ranked=latchGpu; std::sort(ranked.begin(), ranked.end()); gpuP99=ranked[(ranked.size()-1)*99/100];
    }
    int finish() {
        for (auto& p:panels) {
            if (p.capture || p.failed) std::cout << p.layout.output << ": " << p.frames << " frames (" << p.width << 'x' << p.height << ")\n";
            if (smoke && p.capture && p.frames<10) result=1;
            glDeleteTextures(1, &p.texture);
            p.capture.reset();
        }
        if (canvas) finishCanvas();
        if (smoke && drawn<10) result=1;
        std::cout << "Head pose samples: " << tracking.camera.samples << std::endl;
        spectator.reset();
        halo.release();
        if (environment) environment->release();
        if (notificationHud) {notificationHud->release();notificationHud.reset();}
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
        output.reset(); SDL_Quit();
        return result;
    }
    void finishCanvas() {
        for (const auto& w:canvas->windows)
            std::cout << w.name << ": " << w.frames << " frames (" << w.width << 'x' << w.height << ") " << canvas::tierName(w.decision.tier) << (w.captureStatus.empty() ? "" : ", "+w.captureStatus)
                      << (w.stageFrames ? " (region "+std::to_string(w.stageFrames)+" frames)" : "") << "\n";
        if (smoke && !canvas->smokeDone()) result=1;
        if (!canvas->memoryPath.empty() && canvas->memory.dirty) canvas->memory.save(canvas->memoryPath);
        canvas->releaseGpu();
        if (cursorTexture) { glDeleteTextures(1, &cursorTexture); cursorTexture=0; }
    }
    // Settings and the initial window list, then land on the staged window (else the remembered camera,
    // else the overview) without easing.
    void startCanvas() {
        canvas->setFov(canvasFov());
        pollCanvasSettings();
        if (!windowsPath.empty()) pollWindows();
        canvas->tick(monotonicSeconds(), 0);
        const auto* staged=canvas->staged();
        const auto landing=staged ? canvas->land(staged->name, baseView()) : canvas::Scene::Aim{};
        applyAim(landing ? landing : canvas->restoreCamera(baseView()));
        canvas->snap(); canvas->refresh(monotonicSeconds()); canvas->rememberCamera=true;
        navigationRotation=targetRotation; panX=targetPanX; panY=targetPanY; panZ=targetPanZ;
        // SDL2 starts text input with the window; the canvas keys are single letters until search opens.
        if (window) SDL_StopTextInput();
        canvasStateSeen=canvas->state;
    }
    int run() {
        if (direct) output=std::make_unique<DirectOutput>(display, stereo);
        if (!openWindow()) return 1;
        environment=std::make_unique<SkyEnvironment>(layoutPath.empty() ? "" : (std::filesystem::path(layoutPath).parent_path()/"environment.tsv").string());
        if(stereo && !posePath.empty()) notificationHud=std::make_unique<notifications::Hud>(std::filesystem::path(posePath).parent_path().string());
        std::signal(SIGTERM, stopSignal); std::signal(SIGINT, stopSignal);
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
        std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\nPanels: " << panels.size() << std::endl;
        if (!canvas) sceneBounds();
        primeCamera();
        if (canvas) startCanvas();
        while (running && !interrupted) if (!tick()) break;
        return finish();
    }
};
int preview(std::vector<Panel>& panels, bool smoke, spatial::Workspace workspace, float spacing, const std::string& display, const std::string& posePath, bool direct, bool stereo, float ipd, float fov, const std::string& layoutPath, int fps, bool spectatorEnabled) {
    return View(panels, smoke, workspace, spacing, display, posePath, direct, stereo, ipd, fov, layoutPath, fps, spectatorEnabled).run();
}
// The Lua adapter writes its version next to the pose socket; canvas mode needs v6 (§3.5).
bool controlsVersionOk(const std::string& posePath, std::string& why) {
    if (posePath.empty()) { why="no --pose-socket"; return false; }
    const auto file=std::filesystem::path(posePath).parent_path()/"controls.version";
    std::ifstream in(file); int version=0;
    if (!in) { why=file.string()+" missing"; return false; }
    if (!(in>>version)) { why=file.string()+" unreadable"; return false; }
    if (version<6) { why="found version "+std::to_string(version); return false; }
    return true;
}
// Window canvas mode: no monitor panels; the capture connection must work before any window opens.
int canvasPreview(bool smoke, const std::string& display, const std::string& posePath, bool direct, bool stereo, float ipd, float fov, const std::string& canvasPath, const std::string& windowsPath, int fps, bool spectatorEnabled) {
    std::string why;
    if (windowsPath.empty() && !controlsVersionOk(posePath, why))
        throw std::runtime_error("Window canvas needs XR controls version 6 or newer: open Utilities -> Setup & integrations and reinstall the controls ("+why+")");
    const auto memory=(std::filesystem::path(canvasPath).parent_path()/"canvas-memory.tsv").string();
    auto scene=std::make_unique<canvas::Scene>(canvas::Ring{}, canvas::Settings{}, memory);
    std::string error;
    if (!scene->connect(error)) throw std::runtime_error("Window canvas unavailable: "+error);
    std::vector<Panel> none;
    View view(none, smoke, spatial::Workspace{}, 24, display, posePath, direct, stereo, ipd, fov, canvasPath, fps, spectatorEnabled);
    view.mode=View::SceneMode::Canvas; view.canvas=std::move(scene); view.windowsPath=windowsPath;
    return view.run();
}
void checkCanvasOptions(const std::string& canvasPath, const std::string& windowsPath, bool monitorOptions, bool otherScene) {
    if (!windowsPath.empty() && canvasPath.empty()) throw std::runtime_error("--canvas-windows-file requires --canvas");
    if (canvasPath.empty()) return;
    if (otherScene) throw std::runtime_error("Choose one of --canvas, --layout, --capture, or --list-outputs");
    if (monitorOptions) std::cerr << "Canvas mode ignores --workspace-*, --spacing and --surface-curvature" << std::endl;
}

}
int main(int argc,char** argv) {
    try {
        bool smoke=false,list=false,direct=false,stereo=false,listLeases=false,spectator=false;float ipd=64,fov=28; int fps=60; spatial::Workspace workspace;float surfaceCurve=0; int spacing=24;
        std::vector<PanelLayout> layouts; std::string path, display, posePath, canvasPath, windowsPath; bool monitorOptions=false;
        for (int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if (arg.starts_with("--workspace-") || arg=="--spacing" || arg=="--surface-curvature") monitorOptions=true;
            auto value=[&]() -> std::string { if (++i>=argc || std::string_view(argv[i]).starts_with("--") || !*argv[i]) throw std::runtime_error(arg+" requires a value"); return argv[i]; };
            if (arg=="--graphics-limits") { graphics_limits::report(); return 0; }
            if (arg=="--help") { std::cout << "Usage: omarchy-xr [--capture OUTPUT ... | --layout FILE | --list-outputs | --graphics-limits] [--spacing 1..8192] [--fps 1..120] [--workspace-curvature 0..100 | --workspace-degrees 0..360] [--workspace-follow] [--surface-curvature 0..100] [--display OUTPUT | --direct OUTPUT | --list-leases] [--stereo] [--spectator] [--ipd 50..80] [--fov 15..100] [--pose-socket PATH] [--smoke-test] [--canvas FILE [--canvas-windows-file FILE]]\nRight-drag: look; middle-drag: pan; wheel: zoom; F: fit (monitors); R: recenter; Esc: exit\n"
                "Window canvas: --canvas names canvas.tsv (settings; may not exist yet). Windows come from the XR controls' .windows\nmailbox beside --pose-socket, which needs controls version 6 (<pose dir>/controls.version). Developer runs may pass\n--canvas-windows-file, a window list in the mailbox format; it replaces the mailbox and skips the version check.\nFormats: docs/infinite-canvas-plan.md sections 3.1 and 4.3.\nCanvas keys (windowed): /: search; F: fill; O: overview; P: pin; R: recenter; Tab: switch (Return lands);\nAlt+arrows: neighbour; Alt+Shift+arrows: nudge; Ctrl+A: arrange; Ctrl+Z / Ctrl+Shift+Z: undo / redo; F1: help;\nEsc: close the overlay, then exit\n"; return 0; }
            else if (arg=="--version") { std::cout << "omarchy-xr 0.3.1\n"; return 0; }
            else if (arg=="--smoke-test") smoke=true;
            else if (arg=="--list-outputs") list=true;
            else if (arg=="--capture") { auto name=value(); layouts.push_back({name,float(layouts.size())*2000,0,1920,1080}); }
            else if (arg=="--layout") path=value();
            else if (arg=="--canvas") canvasPath=value();
            else if (arg=="--canvas-windows-file") windowsPath=value();
            else if (arg=="--display") display=value();
            else if (arg=="--direct") {display=value();direct=true;}
            else if (arg=="--spectator") spectator=true;
            else if (arg=="--stereo") stereo=true;
            else if (arg=="--list-leases") listLeases=true;
            else if (arg=="--ipd" || arg=="--fov") {
                auto text=value();size_t end=0;float v=std::stof(text,&end);
                if(end!=text.size() || !std::isfinite(v) || v<(arg=="--ipd"?50:15) || v>(arg=="--ipd"?80:100)) throw std::runtime_error("Invalid "+arg);
                if(arg=="--ipd")ipd=v;else fov=v;
            }
            else if (arg=="--pose-socket") posePath=value();
            else if (arg=="--workspace-follow") workspace.follow=true;
            else if (arg=="--workspace-degrees") {
                auto text=value();size_t end=0;workspace.degrees=std::stof(text,&end);
                if(end!=text.size() || !std::isfinite(workspace.degrees) || workspace.degrees<0 || workspace.degrees>360)throw std::runtime_error("Workspace wrap must be 0..360 degrees");
            }
            else if (arg=="--workspace-curvature" || arg=="--surface-curvature") {
                auto text=value();size_t end=0;float c=std::stof(text,&end);
                if(end!=text.size() || !std::isfinite(c) || c<0 || c>100) throw std::runtime_error("Curvature must be 0..100");
                if(arg=="--workspace-curvature") workspace.amount=c;else surfaceCurve=c;
            }
            else if(arg=="--spacing") {auto text=value();size_t end=0;spacing=std::stoi(text,&end);if(end!=text.size() || spacing<1 || spacing>8192) throw std::runtime_error("Spacing must be 1..8192 pixels");}
            else if (arg=="--fps") { auto text=value(); size_t end=0; fps=std::stoi(text,&end); if (end!=text.size() || fps<1 || fps>120) throw std::runtime_error("FPS must be 1..120"); }
            else throw std::runtime_error("Unknown option: "+arg);
        }
        checkCanvasOptions(canvasPath, windowsPath, monitorOptions, !path.empty() || !layouts.empty() || list);
        if (canvasPath.empty() && ((!path.empty() && !layouts.empty()) || (list && (!path.empty() || !layouts.empty() || smoke)))) throw std::runtime_error("Choose one of --layout, --capture, or --list-outputs");
        if(listLeases){for(auto& name:DirectOutput::connectors())std::cout<<name<<"\n";return 0;}
        if (!canvasPath.empty()) return canvasPreview(smoke,display,posePath,direct,stereo,ipd,fov,canvasPath,windowsPath,fps,spectator);
        if (list) { DesktopCapture c; if (!c.connect()) throw std::runtime_error(c.error()); for (auto& o:c.outputs()) std::cout << o << '\n'; return 0; }
        if (!path.empty()) layouts=readLayout(path);
        bool live=!layouts.empty();
        if (!live) for (int i=0;i<3;++i) layouts.push_back({"",float(i)*2000,0,1920,1080});
        if(path.empty()) for(size_t i=0;i<layouts.size();++i) layouts[i].x=float(i)*(1920+spacing);
        std::unordered_set<std::string> names;
        std::vector<Panel> panels;
        for (auto& layout:layouts) {
            Panel p; p.layout=layout;
            if (path.empty()) p.layout.curvature=surfaceCurve;
            if (live) {
                if (!names.insert(layout.output).second) throw std::runtime_error("Duplicate capture output: "+layout.output);
                auto next=std::make_unique<DesktopCapture>(); next->setFrameRate(fps);
                if (!next->connect() || !next->select(layout.output)) throw std::runtime_error(next->error());
                p.capture=std::move(next);
            }
            panels.push_back(std::move(p));
        }
        return preview(panels,smoke,workspace,spacing,display,posePath,direct,stereo,ipd,fov,path,fps,spectator);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
