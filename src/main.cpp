#include "spectator.hpp"
#include "environment.hpp"
#include "graphics_limits.hpp"
#include "capture.hpp"
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
    std::unique_ptr<DesktopCapture> capture;
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
void drawPanel(const Panel& panel, size_t index, float cx, float cy, float span, float distance, spatial::Workspace workspace) {
    constexpr float unit=1.f/900.f;
    const auto& p=panel.layout;
    const float w=p.width*unit,h=p.height*unit;
    const auto pose=spatial::pose((p.x+p.width/2-cx)*unit,-(p.y+p.height/2-cy)*unit,w,span,distance,workspace,p.curvature);
    (void)index;
    glColor3f(.025f,.028f,.035f);
    surface(pose,-w/2,-h/2,w,h,0);
    if (panel.width && !panel.failed) {
        const auto content=interaction::content(w,h,panel.sourceWidth,panel.sourceHeight);
        const float tw=content.width,th=content.height;
        glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,panel.frame.texture?panel.frame.texture:panel.texture);
        const float dim=panel.captureStatus.empty()?1.f:.45f;
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
void drawHalo(const Panel& panel,float cx,float cy,float span,float distance,spatial::Workspace workspace) {
    const auto& p=panel.layout;const float w=p.width/900,h=p.height/900;
    const auto pose=spatial::pose((p.x+p.width/2-cx)/900,-(p.y+p.height/2-cy)/900,w,span,distance,workspace,p.curvature);
    const float extent=std::min(w,h)*(.012f+.018f*panel.halo);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
    // A neutral, feathered drop shadow adds depth without outlining the image.
    const float shadow=std::min(w,h)*.045f;
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
        glColor4f(.35f,.65f,1.f,(.025f+.18f*panel.halo)*fade*fade);
        surface(pose,-w/2-outer,h/2+inner,w+2*outer,thickness,-.025f);
        surface(pose,-w/2-outer,-h/2-outer,w+2*outer,thickness,-.025f);
        surface(pose,-w/2-outer,-h/2-inner,thickness,h+2*inner,-.025f);
        surface(pose,w/2+inner,-h/2-inner,thickness,h+2*inner,-.025f);
    }
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
    std::optional<LiveControls> controls;
    std::string spectatorError;
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
    std::vector<double> workTimes, frameTimes, gpuCaptureTimes, gpuSceneTimes, latchWork, latchGpu;
    GpuTimers gpuTimers;
    MissPenalty missPenalty;
    std::filesystem::file_time_type layoutVersion{}, trackingVersion{};
    bool trackingLoaded=false;
    std::string trackingPath;
    View(std::vector<Panel>& panels, bool smoke, spatial::Workspace workspace, float spacing, const std::string& display, const std::string& posePath, bool direct, bool stereo, float ipd, float fov, const std::string& layoutPath, int fps, bool spectatorEnabled)
        : panels(panels), smoke(smoke), workspace(workspace), spacing(spacing), display(display), posePath(posePath), direct(direct), stereo(stereo), ipd(ipd), fov(fov), layoutPath(layoutPath), fps(fps), spectatorEnabled(spectatorEnabled), tracking(posePath) {}
    void drawable(int& w, int& h) const { if (output) { w=output->width(); h=output->height(); } else SDL_GL_GetDrawableSize(window, &w, &h); }
    float span() const { return (right-left)/900.f; }
    float safe(float d) const { return spatial::safeDistance(geometry, cx, cy, span(), d, workspace, spacing); }
    tracking::Quaternion baseView() const { return targeting::viewRotation(tracking.camera.view, pitch, yaw); }
    tracking::Quaternion currentView() const { return tracking::multiply(baseView(), navigationRotation); }
    float aspect() const { int w,h; drawable(w,h); return float(std::max(w/(stereo?2:1),1))/std::max(h,1); }
    float overviewDepth() const { return navigation::overviewDepth(geometry, cx, cy, span(), distance, workspace, fov, aspect()); }
    float maxZoomDepth() const { return overviewDepth()*2.f; }
    spatial::Pose monitorPose(const PanelLayout& p) const { return spatial::pose((p.x+p.width/2-cx)/900, -(p.y+p.height/2-cy)/900, p.width/900, span(), distance, workspace, p.curvature); }
    void bindTexture(GLuint texture) const {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    bool openWindow() {
        if (SDL_Init(direct ? SDL_INIT_EVENTS : SDL_INIT_VIDEO) != 0) { std::cerr << SDL_GetError() << '\n'; return false; }
        if (direct) return true;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        int displayIndex=0;
        if (!display.empty() && !findDisplay(displayIndex)) return false;
        window = SDL_CreateWindow("Omarchy XR | Right-drag: look | Middle-drag: pan | Wheel: zoom | F: fit | R: recenter",
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
        recenterUntil=0; panCamera=false; zoomGaze.reset(); focusFromGaze=false;
        focusOutput.clear(); focusX=focusY=0; targetRotation={};
        targetDistance=distance;
        targetPanX=targetPanY=yaw=pitch=0;
        targetPanZ=distance-overviewDepth();
    }
    void sampleTarget() {
        const bool fresh=posePath.empty() || tracking.camera.fresh(monotonicSeconds());
        const auto view=currentView();
        gaze.update(fresh ? targeting::query(targeting::viewRay(view, {panX, panY, panZ}), geometry, cx, cy, span(), distance, workspace) : std::nullopt, fresh);
        selection.validate(geometry);
        const auto previous=selection.output;
        if (!panGestureActive && monotonicSeconds()>=recenterUntil) selection.observe(gaze.current);
        if (gaze.current && selection.output!=previous) selectionAnchor=baseView();
    }
    bool focusSelected(bool fitHeight, float zoom) {
        recenterUntil=0; panCamera=false;
        if (fitHeight) { zoomGaze.reset(); focusFromGaze=false; }
        const bool captured=!zoomGaze;
        const bool recapture=zoomGaze && gaze.current && gaze.current->output!=zoomGaze->output;
        if (!fitHeight && zoom!=0 && navigation::lockZoomGaze(zoomGaze, gaze.current)) lockGaze(captured, recapture);
        const auto found=std::find_if(geometry.begin(), geometry.end(), [&](const auto& p){ return p.output==selection.output; });
        if (found==geometry.end()) return false;
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
        const auto found=std::find_if(geometry.begin(), geometry.end(), [&](const auto& p){ return p.output==selection.output; });
        if (found==geometry.end()) { headingOnly(previousView); return; }
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
        const auto zoomPanel=std::find_if(geometry.begin(), geometry.end(), [&](const auto& item){ return item.output==focusOutput; });
        if (zoomPanel==geometry.end()) return pose;
        pose=monitorPose(*zoomPanel);
        const float x=wasPanning?smoothFocusX:focusX, y=wasPanning?smoothFocusY:focusY;
        pose.center=spatial::vertex(pose, x, y); pose.yaw-=x*pose.surfaceBend;
        if (pose.spherical) pose.latitude+=y*pose.surfaceBend;
        return pose;
    }
    void panSelected(float dx, float dy, bool begin) {
        recenterUntil=0;
        if (begin) panOutput=selection.output;
        const auto found=std::find_if(geometry.begin(), geometry.end(), [&](const auto& p){ return p.output==panOutput; });
        if (found==geometry.end()) return;
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
        if (!focusSelected(false, amount)) {
            targetDistance=distance;
            targetPanZ=distance-navigation::zoomDepth(distance-targetPanZ, amount, maxZoomDepth());
        }
    }
    void fitTarget() {
        if (focusSelected(true, 0)) std::cout << "Camera: fit selected monitor face-on " << selection.output << std::endl;
        else std::cout << "Camera: no selected monitor; fit ignored" << std::endl;
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
                spectator.reset();
                environment->release();
                gpuTimers.reset();
                output=std::make_unique<DirectOutput>(display, stereo);
                if (!output->pump()) throw std::runtime_error("Replacement lease is not active");
                for (auto& p:panels) {
                    p.retryAt=monotonicSeconds(); p.retryMs=500; p.captureStatus="reconnecting after lease";
                    glGenTextures(1, &p.texture); bindTexture(p.texture);
                }
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
    void serviceCaptures() { for (auto& p:panels) if (p.capture && !p.failed) p.capture->service(); }
    void reconnectCaptures() {
        const double now=monotonicSeconds();
        for (auto& p:panels) {
            if (p.capture || p.retryAt<=0 || now<p.retryAt) continue;
            auto next=std::make_unique<DesktopCapture>();
            next->setFrameRate(static_cast<unsigned>(fps));
            if (next->connect() && next->select(p.layout.output)) { p.capture=std::move(next); p.captureStatus.clear(); p.retryAt=0; p.retryMs=500; }
            else { p.captureStatus=next->error().empty()?"reconnect failed":next->error(); p.retryAt=now+p.retryMs/1000.0; p.retryMs=DesktopCapture::nextRetryMs(p.retryMs); }
        }
    }
    void updateCaptures() {
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
        p.sourceWidth=p.frame.sourceWidth; p.sourceHeight=p.frame.sourceHeight;
        glBindTexture(GL_TEXTURE_2D, p.texture);
        if (!p.frame.texture) uploadCpu(p);
        p.width=p.frame.width; p.height=p.frame.height;
        ++p.frames;
    }
    void uploadCpu(Panel& p) {
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
    void describePrediction(const char* source) const {
        const auto& p=tracking.camera.prediction;
        std::cout << "Tracking: prediction up to " << p.horizonMs << " ms, fading in from " << p.restSpeed << " to " << p.fullSpeed << " deg/s, " << p.samples << " samples (" << source << ")" << std::endl;
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
        else std::cerr << "Ignoring invalid tracking.tsv; expected: tracking-v1 <horizonMs 0..30> <restSpeed> <fullSpeed> <samples 2..8>" << std::endl;
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
    void applyLiveLayout(std::filesystem::file_time_type version) {
        auto layouts=readLayout(layoutPath);
        int nextFps=fps; spatial::Workspace nextWorkspace=workspace; float nextSpacing=spacing;
        readLiveSettings(nextFps, nextWorkspace, nextSpacing);
        adoptLayout(std::move(layouts), nextFps, nextWorkspace, nextSpacing, version);
    }
    void readLiveSettings(int& nextFps, spatial::Workspace& nextWorkspace, float& nextSpacing) const {
        std::ifstream settings(layoutPath); std::string first; std::getline(settings, first);
        if (!first.starts_with("# settings ")) return;
        std::istringstream values(first.substr(11)); std::string extra;
        if (!(values>>nextFps>>nextWorkspace.amount>>nextSpacing) || nextFps<1 || nextFps>120 || !std::isfinite(nextWorkspace.amount) || nextWorkspace.amount<0 || nextWorkspace.amount>100 || !std::isfinite(nextSpacing) || nextSpacing<1 || nextSpacing>8192)
            throw std::runtime_error("Invalid live presentation settings");
        nextWorkspace.degrees=-1; values>>std::ws;
        if (!values.eof() && (!(values>>nextWorkspace.degrees) || !std::isfinite(nextWorkspace.degrees) || nextWorkspace.degrees<-1 || nextWorkspace.degrees>360))
            throw std::runtime_error("Workspace wrap must be 0..360 degrees");
        nextWorkspace.follow=false; values>>std::ws;
        if (values.eof()) return;
        int follow;
        if (!(values>>follow) || (follow!=0 && follow!=1) || values>>extra) throw std::runtime_error("Invalid workspace wrapping setting");
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
            Panel p; p.layout=layout; p.capture=std::make_unique<DesktopCapture>();
            p.capture->setFrameRate(nextFps);
            if (!p.capture->connect() || !p.capture->select(layout.output)) throw std::runtime_error(p.capture->error());
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
        panGestureActive=controls->panActive;
        if (controls->panStarted || controls->panX || controls->panY) panSelected(float(controls->panX), float(controls->panY), controls->panStarted);
        sampleTarget();
        if (controls->zoom) zoomBy(float(controls->zoom));
        if (controls->fit==1) tracking.fitRequested=true;
        if (controls->fit==2) tracking.fitTargetRequested=true;
        if (controls->fit==3) tracking.recenterRequested=true;
        if (controls->fit==4) zoomBy(-std::log(.9f));
        if (controls->fit==5) zoomBy(std::log(.9f));
        if (tracking.zoom) { zoomBy(float(tracking.zoom)*-std::log(.9f)); tracking.zoom=0; }
        if (tracking.recenterRequested) { recenterSelected(); tracking.recenterRequested=false; }
        if (tracking.fitRequested) { fit(); tracking.camera.recenter(monotonicSeconds()); tracking.fitRequested=false; }
        if (tracking.fitTargetRequested) { fitTarget(); tracking.fitTargetRequested=false; }
    }
    void pollInput() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type==SDL_QUIT) running=false;
            if (event.type==SDL_KEYDOWN) keyDown(event.key.keysym.sym);
            if (event.type==SDL_MOUSEWHEEL) zoomBy(event.wheel.preciseY*-std::log(.9f));
            if (event.type==SDL_MOUSEMOTION) mouseMove(event.motion);
        }
    }
    void keyDown(SDL_Keycode key) {
        if (key==SDLK_ESCAPE) running=false;
        if (key==SDLK_r) recenterSelected();
        if (key==SDLK_f) { fit(); tracking.camera.recenter(monotonicSeconds()); }
    }
    void mouseMove(const SDL_MouseMotionEvent& motion) {
        if (motion.state&SDL_BUTTON_RMASK) { yaw+=motion.xrel*.15f; pitch=std::clamp(pitch+motion.yrel*.15f, -80.f, 80.f); }
        if (motion.state&SDL_BUTTON_MMASK) { panX+=motion.xrel*distance*.0015f; panY-=motion.yrel*distance*.0015f; targetPanX=panX; targetPanY=panY; }
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
        const auto found=std::find_if(geometry.begin(), geometry.end(), [&](const auto& p){ return p.output==focusOutput; });
        if (found==geometry.end()) { panCamera=false; return; }
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
        if (window) SDL_SetWindowTitle(window, state ? "Omarchy XR | Head tracking live | R: recenter | F: fit | Esc: exit" : "Omarchy XR | Tracking unavailable | Mouse look | R: recenter | Esc: exit");
    }
    void projectPanels(int viewportWidth, int viewportHeight, double cameraTime) {
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
        environment->draw(tracking::matrix(view).data());
        glTranslatef(-eyePosition, 0, 0);
        glMultMatrixf(tracking::matrix(view).data()); glTranslatef(panX, panY, panZ);
        for (const auto& p:panels) if (p.visible) drawHalo(p, cx, cy, span(), distance, workspace);
        for (size_t i=0;i<panels.size();++i) if (panels[i].visible) drawPanel(panels[i], i, cx, cy, span(), distance, workspace);
    }
    void presentSpectator(int drawableW, int drawableH) {
        if (tracking.spectator>=0) { spectatorEnabled=tracking.spectator==1; tracking.spectator=-1; spectatorError.clear(); }
        if (!spectatorEnabled) spectator.reset();
        if (!spectatorEnabled) return;
        try {
            if (!spectator) spectator=std::make_unique<Spectator>();
            if (!spectator->pump()) { spectator.reset(); spectatorEnabled=false; return; }
            if (!spectator->begin(monotonicSeconds())) return;
            renderScene(spectator->width(), spectator->height(), false, true, drawableW, drawableH);
            spectator->present();
        } catch (const std::exception& e) {
            spectatorError=e.what(); std::cerr << spectatorError << std::endl;
            glBindFramebuffer(GL_FRAMEBUFFER, 0); spectator.reset(); spectatorEnabled=false;
            while (glGetError()!=GL_NO_ERROR) {} // optional window failure must not stop stereo
        }
    }
    bool draw(float cameraDt, int w, int h) {
        for (auto& p:panels) p.halo=navigation::ease(p.halo, selection.output==p.layout.output ? 1.f:0.f, cameraDt);
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        environment->update(monotonicSeconds());
        // The spectator render queues ahead of the stereo scene, so it is part of what must finish before the flip.
        const bool timeScene=gpuTimers.begin(GpuTimers::Scene);
        presentSpectator(w, h);
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
        if (smoke && drawn>=10 && std::all_of(panels.begin(), panels.end(), [](const Panel& p){ return !p.capture || p.frames>=10; })) running=false;
    }
    void report(double now) {
        std::sort(workTimes.begin(), workTimes.end()); std::sort(frameTimes.begin(), frameTimes.end());
        const double workP95=workTimes[workTimes.size()*95/100], frameP95=frameTimes[frameTimes.size()*95/100];
        const bool gpu=!gpuCaptureTimes.empty() && gpuCaptureTimes.size()==gpuSceneTimes.size();
        if (gpu) { std::sort(gpuCaptureTimes.begin(), gpuCaptureTimes.end()); std::sort(gpuSceneTimes.begin(), gpuSceneTimes.end()); }
        const double gpuCaptureP95=gpu?gpuCaptureTimes[gpuCaptureTimes.size()*95/100]:0;
        const double gpuSceneP95=gpu?gpuSceneTimes[gpuSceneTimes.size()*95/100]:0;
        const unsigned missed=output?output->missedVblanks():0;
        printPerformance(now, workP95, frameP95, gpu, gpuCaptureP95, gpuSceneP95, missed);
        writeStats(now, workP95, frameP95, gpu, gpuCaptureP95, gpuSceneP95, missed);
        missedBaseline=missed; reportTime=now; reportFrames=0; workMax=0; workTimes.clear(); frameTimes.clear(); gpuCaptureTimes.clear(); gpuSceneTimes.clear();
    }
    void printPerformance(double now, double workP95, double frameP95, bool gpu, double gpuCaptureP95, double gpuSceneP95, unsigned missed) const {
        std::cout << "Performance: " << reportFrames/(now-reportTime) << " present fps, work p95 " << workP95 << " ms, work max " << workMax << " ms, frame p95 " << frameP95 << " ms";
        if (gpu) std::cout << ", gpu capture p95 " << gpuCaptureP95 << " ms, gpu scene p95 " << gpuSceneP95 << " ms";
        if (output) std::cout << ", missed vblanks " << missed-missedBaseline << " (" << missed << " session)";
        std::cout << std::endl;
        for (const auto& p:panels) if (p.capture) std::cout << "Capture: " << p.layout.output << " " << (p.visible?"visible":"paused") << " " << p.capture->transport() << " " << p.width << "x" << p.height << " source " << p.sourceWidth << "x" << p.sourceHeight << " requests " << p.capture->requests() << " frames " << p.frames << std::endl;
    }
    void writeStats(double now, double workP95, double frameP95, bool gpu, double gpuCaptureP95, double gpuSceneP95, unsigned missed) const {
        if (posePath.empty()) return;
        std::ostringstream stats;
        stats << "{\"pid\":" << getpid() << ",\"time\":" << std::setprecision(12) << now << ",\"fps\":" << reportFrames/(now-reportTime)
            << ",\"environmentLoading\":" << (environment->loadingImage()?"true":"false") << ",\"environmentError\":" << std::quoted(environment->error)
            << ",\"geometryDistance\":" << distance
            << ",\"zoomDepth\":" << (focusOutput.empty()?distance-panZ:focusDepth) << ",\"maxZoomDepth\":" << maxZoomDepth() << ",\"panX\":" << focusX << ",\"panY\":" << focusY
            << ",\"spectator\":" << (spectator?"true":"false") << ",\"spectatorFrames\":" << (spectator?spectator->frames:0) << ",\"spectatorError\":" << std::quoted(spectatorError)
            << ",\"workP95\":" << workP95 << ",\"workMax\":" << workMax << ",\"frameP95\":" << frameP95 << ",\"predictionMs\":" << lastPredictionMs << ",\"predictionCapMs\":" << tracking.camera.prediction.horizonMs
            << ",\"latchMarginMs\":" << lastMarginMs << ",\"latchPenaltyMs\":" << missPenalty.ms;
        if (gpu) stats << ",\"gpuCaptureP95\":" << gpuCaptureP95 << ",\"gpuSceneP95\":" << gpuSceneP95;
        if (output) stats << ",\"refreshHz\":" << output->refreshHz() << ",\"missedVblanks\":" << missed << ",\"missedVblanksWindow\":" << missed-missedBaseline;
        stats << ",\"captures\":[";
        writeCaptureStats(stats);
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
        left=panels[0].layout.x; top=panels[0].layout.y; right=left; bottom=top;
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
        distance=safe(distance); targetDistance=distance;
        fit(); panZ=targetPanZ;
        controls.emplace(posePath);
        lastCameraTime=monotonicSeconds();
        glEnable(GL_DEPTH_TEST);
        started=SDL_GetTicks64();
        if (!layoutPath.empty()) layoutVersion=std::filesystem::last_write_time(layoutPath);
        reportTime=monotonicSeconds();
        gpuTimers.probe();
        missedBaseline=output?output->missedVblanks():0; seenMisses=missedBaseline;
        lastMarginMs=latchMarginMs(0, gpuP99);
        // Optional, live-reloaded stabilisation settings; see tracking::Prediction.
        trackingPath=layoutPath.empty() ? "" : (std::filesystem::path(layoutPath).parent_path()/"tracking.tsv").string();
        describePrediction("defaults");
    }
    bool tick() {
        const double frameStarted=monotonicSeconds();
        if (!ensureLease()) return false;
        waitForPose();
        if (!running) return false;
        const double workStarted=monotonicSeconds();
        reconnectCaptures();
        reloadTracking(workStarted);
        tracking.update();
        predictPose();
        reloadLayout();
        steer();
        pollInput();
        const float cameraDt=easeCamera();
        sampleTarget();
        showTracking();
        int viewportWidth, viewportHeight; drawable(viewportWidth, viewportHeight);
        int w, h; drawable(w, h);
        if (w<=0 || h<=0) { SDL_Delay(16); return true; }
        // Share exactly the halo target; the compositor consumes monitor transitions only.
        controls->publishHover(direct && !selection.output.empty(), selection.output, 0, 0);
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
    void collectGpu() {
        const auto gpuBefore=gpuSceneTimes.size();
        gpuTimers.collect(gpuCaptureTimes, gpuSceneTimes);
        for (auto i=gpuBefore; i<gpuSceneTimes.size(); ++i) latchGpu.push_back(gpuSceneTimes[i]);
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
        if (smoke && drawn<10) result=1;
        std::cout << "Head pose samples: " << tracking.camera.samples << std::endl;
        spectator.reset();
        if (environment) environment->release();
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
        output.reset(); SDL_Quit();
        return result;
    }
    int run() {
        if (direct) output=std::make_unique<DirectOutput>(display, stereo);
        if (!openWindow()) return 1;
        environment=std::make_unique<SkyEnvironment>(layoutPath.empty() ? "" : (std::filesystem::path(layoutPath).parent_path()/"environment.tsv").string());
        std::signal(SIGTERM, stopSignal); std::signal(SIGINT, stopSignal);
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
        std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\nPanels: " << panels.size() << std::endl;
        sceneBounds();
        primeCamera();
        while (running && !interrupted) if (!tick()) break;
        return finish();
    }
};
int preview(std::vector<Panel>& panels, bool smoke, spatial::Workspace workspace, float spacing, const std::string& display, const std::string& posePath, bool direct, bool stereo, float ipd, float fov, const std::string& layoutPath, int fps, bool spectatorEnabled) {
    return View(panels, smoke, workspace, spacing, display, posePath, direct, stereo, ipd, fov, layoutPath, fps, spectatorEnabled).run();
}

}
int main(int argc,char** argv) {
    try {
        bool smoke=false,list=false,direct=false,stereo=false,listLeases=false,spectator=false;float ipd=64,fov=28; int fps=60; spatial::Workspace workspace;float surfaceCurve=0; int spacing=24;
        std::vector<PanelLayout> layouts; std::string path, display, posePath;
        for (int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            auto value=[&]() -> std::string { if (++i>=argc || std::string_view(argv[i]).starts_with("--") || !*argv[i]) throw std::runtime_error(arg+" requires a value"); return argv[i]; };
            if (arg=="--graphics-limits") { graphics_limits::report(); return 0; }
            if (arg=="--help") { std::cout << "Usage: omarchy-xr [--capture OUTPUT ... | --layout FILE | --list-outputs | --graphics-limits] [--spacing 1..8192] [--fps 1..120] [--workspace-curvature 0..100 | --workspace-degrees 0..360] [--workspace-follow] [--surface-curvature 0..100] [--display OUTPUT | --direct OUTPUT | --list-leases] [--stereo] [--spectator] [--ipd 50..80] [--fov 15..100] [--pose-socket PATH] [--smoke-test]\nRight-drag: look; middle-drag: pan; wheel: zoom; F: fit; R: recenter; Esc: exit\n"; return 0; }
            else if (arg=="--version") { std::cout << "omarchy-xr 0.2.0-dev\n"; return 0; }
            else if (arg=="--smoke-test") smoke=true;
            else if (arg=="--list-outputs") list=true;
            else if (arg=="--capture") { auto name=value(); layouts.push_back({name,float(layouts.size())*2000,0,1920,1080}); }
            else if (arg=="--layout") path=value();
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
        if ((!path.empty() && !layouts.empty()) || (list && (!path.empty() || !layouts.empty() || smoke))) throw std::runtime_error("Choose one of --layout, --capture, or --list-outputs");
        if(listLeases){for(auto& name:DirectOutput::connectors())std::cout<<name<<"\n";return 0;}
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
                p.capture=std::make_unique<DesktopCapture>(); p.capture->setFrameRate(fps);
                if (!p.capture->connect() || !p.capture->select(layout.output)) throw std::runtime_error(p.capture->error());
            }
            panels.push_back(std::move(p));
        }
        return preview(panels,smoke,workspace,spacing,display,posePath,direct,stereo,ipd,fov,path,fps,spectator);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
