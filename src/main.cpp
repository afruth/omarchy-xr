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
#include <csignal>
#include <filesystem>
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>

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
        glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,panel.frame.texture?panel.frame.texture:panel.texture);glColor3f(p.brightness/100,p.brightness/100,p.brightness/100);
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
int preview(std::vector<Panel>& panels, bool smoke, spatial::Workspace workspace, float spacing, const std::string& display, const std::string& posePath, bool direct, bool stereo, float ipd, float fov, const std::string& layoutPath, int fps, bool spectatorEnabled) {
    PoseSocket tracking(posePath);
    std::unique_ptr<DirectOutput> output;
    std::unique_ptr<Spectator> spectator;
    std::string spectatorError;
    SDL_Window* window=nullptr; SDL_GLContext context=nullptr;
    if(direct) output=std::make_unique<DirectOutput>(display,stereo);
    if (SDL_Init(direct ? SDL_INIT_EVENTS : SDL_INIT_VIDEO) != 0) { std::cerr << SDL_GetError() << '\n'; return 1; }
    if(!direct) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    int displayIndex=0;
    if (!display.empty()) {
        displayIndex=-1;
        for(int i=0;i<SDL_GetNumVideoDisplays();++i) {
            const std::string name=SDL_GetDisplayName(i) ? SDL_GetDisplayName(i) : "";
            if(name==display || name.ends_with("("+display+")")) {displayIndex=i;break;}
        }
        if(displayIndex<0) {std::cerr<<"Display unavailable: "<<display<<'\n';SDL_Quit();return 1;}
        std::cout<<"Presentation output: "<<display<<std::endl;
    }
    window = SDL_CreateWindow("Omarchy XR | Right-drag: look | Middle-drag: pan | Wheel: zoom | F: fit | R: recenter",
        SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),1280,720,
        SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) { std::cerr << SDL_GetError() << '\n'; SDL_Quit(); return 1; }
    if (!display.empty() && SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP)!=0) {
        std::cerr<<SDL_GetError()<<'\n';SDL_DestroyWindow(window);SDL_Quit();return 1;
    }
    context = SDL_GL_CreateContext(window);
    if (!context) { std::cerr << SDL_GetError() << '\n'; SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(1);
    SDL_ShowCursor(SDL_DISABLE);
    }
    SkyEnvironment environment(layoutPath.empty() ? "" : (std::filesystem::path(layoutPath).parent_path()/"environment.tsv").string());
    auto dimensions=[&](int& w,int& h){if(output){w=output->width();h=output->height();}else SDL_GL_GetDrawableSize(window,&w,&h);};
    std::signal(SIGTERM,stopSignal);std::signal(SIGINT,stopSignal);
    GLint maxTexture; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxTexture);
    std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\nPanels: " << panels.size() << std::endl;
    float left=panels[0].layout.x, top=panels[0].layout.y, right=left, bottom=top;
    for (auto& p : panels) {
        left=std::min(left,p.layout.x); top=std::min(top,p.layout.y);
        right=std::max(right,p.layout.x+p.layout.width); bottom=std::max(bottom,p.layout.y+p.layout.height);
        glGenTextures(1,&p.texture); glBindTexture(GL_TEXTURE_2D,p.texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    float cx=(left+right)/2, cy=(top+bottom)/2;
    float yaw=0,pitch=0,panX=0,panY=0,panZ=0,distance=5;
    std::vector<PanelLayout> geometry;
    for(const auto& p:panels) geometry.push_back(p.layout);
    workspace.gap=spacing/900;
    auto safe = [&](float d) {return spatial::safeDistance(geometry,cx,cy,(right-left)/900.f,d,workspace,spacing);};
    float targetDistance=distance,targetPanX=0,targetPanY=0,targetPanZ=0;
    tracking::Quaternion navigationRotation,targetRotation,selectionAnchor,focusAnchor;
    targeting::Selection selection;
    std::string focusOutput;
    float focusDepth=5,focusX=0,focusY=0;
    bool panGestureActive=false,panCamera=false;
    float smoothFocusX=0,smoothFocusY=0;
    std::string panOutput;
    double recenterUntil=0;
    auto baseView=[&]{return targeting::viewRotation(tracking.camera.view,pitch,yaw);};
    auto viewRotation=[&]{return tracking::multiply(baseView(),navigationRotation);};
    auto overviewDepth = [&] {
        int w,h;dimensions(w,h);
        return navigation::overviewDepth(geometry,cx,cy,(right-left)/900.f,distance,workspace,fov,
            float(std::max(w/(stereo?2:1),1))/std::max(h,1));
    };
    auto maxZoomDepth = [&] {return overviewDepth()*2.f;};
    auto fit = [&] {
        recenterUntil=0;panCamera=false;
        focusOutput.clear();focusX=focusY=0;targetRotation={};
        targetDistance=distance;
        targetPanX=targetPanY=yaw=pitch=0;
        targetPanZ=distance-overviewDepth();
    };
    targeting::Tracker gaze;
    auto sampleTarget = [&] {
        const bool fresh=posePath.empty() || tracking.camera.fresh(monotonicSeconds());
        const auto view=viewRotation();
        gaze.update(fresh ? targeting::query(targeting::viewRay(view,{panX,panY,panZ}),geometry,cx,cy,(right-left)/900,distance,workspace) : std::nullopt,fresh);
        selection.validate(geometry);
        const auto previous=selection.output;if(!panGestureActive && monotonicSeconds()>=recenterUntil)selection.observe(gaze.current);
        if(gaze.current && selection.output!=previous)selectionAnchor=baseView();
    };
    auto focusSelected = [&](bool fitHeight,float zoom) {
        recenterUntil=0;panCamera=false;
        const bool towardGaze=!fitHeight && zoom!=0 && gaze.current;
        if(towardGaze){
            const auto previous=selection.output;
            selection.observe(gaze.current);
            if(selection.output!=previous)selectionAnchor=baseView();
        }
        const auto found=std::find_if(geometry.begin(),geometry.end(),[&](const auto& p){return p.output==selection.output;});
        if(found==geometry.end())return false;
        const auto& p=*found;
        // Keep workspace geometry fixed; move the camera along the look
        // point's local normal. Zoom must not change its workspace bend or yaw.
        targetDistance=distance;
        const auto pose=spatial::pose((p.x+p.width/2-cx)/900,-(p.y+p.height/2-cy)/900,p.width/900,(right-left)/900,distance,workspace,p.curvature);
        if(focusOutput!=p.output){
            focusOutput=p.output;focusX=focusY=0;focusAnchor=selectionAnchor;
            focusDepth=navigation::viewingDistance(pose,{panX,panY,panZ});
        }
        if(fitHeight){focusX=focusY=0;focusAnchor=selectionAnchor;focusDepth=navigation::frontHeightDistance(p,pose,fov);}
        else {
            if(towardGaze && gaze.current->output==p.output){
                const auto origin=navigation::gazeFocus(p,*gaze.current);
                focusX=origin.x;focusY=origin.y;focusAnchor=baseView();
            }
            focusDepth=navigation::zoomDepth(focusDepth,zoom,maxZoomDepth());
        }
        // Stay in front of a curved monitor's nearest edge, even at high zoom.
        const float sag=spatial::bendZ(p.width/1800,pose.surfaceBend);
        focusDepth=std::max(focusDepth,sag+.15f);
        int viewportW,viewportH;dimensions(viewportW,viewportH);
        const auto limits=navigation::panLimits(p,pose,focusDepth,fov,float(viewportW/(stereo?2:1))/std::max(viewportH,1));
        focusX=std::clamp(focusX,-limits.x,limits.x);focusY=std::clamp(focusY,-limits.y,limits.y);
        const auto target=navigation::panFocus(pose,focusAnchor,focusDepth,focusX,focusY);
        targetRotation=target.rotation;targetPanX=target.pan.x;targetPanY=target.pan.y;targetPanZ=target.pan.z;
        return true;
    };
    auto recenterSelected = [&] {
        const auto previousView=viewRotation();
        recenterUntil=monotonicSeconds()+.5;
        const bool wasPanning=panCamera;panCamera=false;
        const auto found=std::find_if(geometry.begin(),geometry.end(),[&](const auto& p){return p.output==selection.output;});
        if(found!=geometry.end()) {
            const auto& p=*found;
            const auto pose=spatial::pose((p.x+p.width/2-cx)/900,-(p.y+p.height/2-cy)/900,p.width/900,(right-left)/900,distance,workspace,p.curvature);
            // Preserve current viewing distance, including an in-flight zoom.
            // Keep the current zoom even if gaze selected a different panel.
            const auto zoomPanel=std::find_if(geometry.begin(),geometry.end(),[&](const auto& item){return item.output==focusOutput;});
            auto zoomPose=zoomPanel==geometry.end() ? pose : spatial::pose(
                (zoomPanel->x+zoomPanel->width/2-cx)/900,-(zoomPanel->y+zoomPanel->height/2-cy)/900,
                zoomPanel->width/900,(right-left)/900,distance,workspace,zoomPanel->curvature);
            if(zoomPanel!=geometry.end()){
                const float x=wasPanning?smoothFocusX:focusX,y=wasPanning?smoothFocusY:focusY;
                zoomPose.center=spatial::vertex(zoomPose,x,y);zoomPose.yaw-=x*zoomPose.surfaceBend;
                if(zoomPose.spherical)zoomPose.latitude+=y*zoomPose.surfaceBend;
            }
            focusDepth=navigation::viewingDistance(zoomPose,{panX,panY,panZ});
            tracking.camera.recenter(monotonicSeconds());yaw=pitch=0;
            selectionAnchor=focusAnchor={};focusOutput=p.output;focusX=focusY=0;targetDistance=distance;
            const auto target=navigation::frontFocus(pose,{},focusDepth);
            navigationRotation=navigation::preserveView(previousView,baseView());
            targetRotation=target.rotation;
            targetPanX=target.pan.x;targetPanY=target.pan.y;targetPanZ=target.pan.z;
            std::cout<<"Camera: smooth recenter "<<selection.output<<" depth "<<focusDepth<<std::endl;
        } else {
            // No selection yet: animate heading only, preserving zoom and pan.
            tracking.camera.recenter(monotonicSeconds());yaw=pitch=0;
            navigationRotation=navigation::preserveView(previousView,baseView());
            targetRotation={};
        }
    };
    auto panSelected = [&](float dx,float dy,bool begin) {
        recenterUntil=0;
        if(begin)panOutput=selection.output;
        const auto found=std::find_if(geometry.begin(),geometry.end(),[&](const auto& p){return p.output==panOutput;});
        if(found==geometry.end())return;
        const auto& p=*found;
        const auto pose=spatial::pose((p.x+p.width/2-cx)/900,-(p.y+p.height/2-cy)/900,p.width/900,(right-left)/900,distance,workspace,p.curvature);
        if(begin){
            // Freeze the zoom visible at gesture start, including animations.
            auto reference=pose;
            if(focusOutput==p.output){const float x=panCamera?smoothFocusX:focusX,y=panCamera?smoothFocusY:focusY;reference.center=spatial::vertex(pose,x,y);reference.yaw-=x*pose.surfaceBend;if(pose.spherical)reference.latitude+=y*pose.surfaceBend;}
            const float depth=navigation::viewingDistance(reference,{panX,panY,panZ});
            int w,h;dimensions(w,h);
            auto limits=navigation::panLimits(p,pose,depth,fov,float(w/(stereo?2:1))/std::max(h,1));
            if(limits.x==0 && limits.y==0){panOutput.clear();return;}
            if(focusOutput!=p.output)focusX=focusY=0;
            if(!panCamera){smoothFocusX=focusX;smoothFocusY=focusY;}
            panCamera=true;
            focusOutput=p.output;focusDepth=depth;focusAnchor=baseView();targetDistance=distance;
        }
        int w,h;dimensions(w,h);
        const auto limits=navigation::panLimits(p,pose,focusDepth,fov,float(w/(stereo?2:1))/std::max(h,1));
        const float speed=2*focusDepth*std::tan(fov*pi/360)/400;
        focusX=std::clamp(focusX-dx*speed,-limits.x,limits.x);
        focusY=std::clamp(focusY+dy*speed,-limits.y,limits.y);
        const auto target=navigation::panFocus(pose,focusAnchor,focusDepth,focusX,focusY);
        targetRotation=target.rotation;targetPanX=target.pan.x;targetPanY=target.pan.y;targetPanZ=target.pan.z;
    };
    auto fitTarget = [&] {
        if(focusSelected(true,0))std::cout<<"Camera: fit selected monitor face-on "<<selection.output<<std::endl;
        else std::cout<<"Camera: no selected monitor; fit ignored"<<std::endl;
    };
    distance=safe(distance);targetDistance=distance;
    fit();panZ=targetPanZ;
    LiveControls controls(posePath);
    double lastCameraTime=monotonicSeconds();
    glEnable(GL_DEPTH_TEST);
    bool running=true; int result=0,drawn=0;
    const auto started=SDL_GetTicks64();
    int trackingStatus=-1;
    std::filesystem::file_time_type layoutVersion{};
    if(!layoutPath.empty()) layoutVersion=std::filesystem::last_write_time(layoutPath);
    Uint64 nextLayoutCheck=0;
    double reportTime=monotonicSeconds();unsigned reportFrames=0;
    std::vector<double> workTimes,frameTimes;
    auto updateCaptures = [&] {
        for (auto& p : panels) {
            if (!p.capture || p.failed) continue;
            if (p.capture->update(p.frame)) {
                if (p.frame.width>unsigned(maxTexture) || p.frame.height>unsigned(maxTexture)) {
                    std::cerr << p.layout.output << ": exceeds GPU texture size\n"; p.failed=true; result=1; continue;
                }
                p.sourceWidth=p.frame.sourceWidth;p.sourceHeight=p.frame.sourceHeight;
                glBindTexture(GL_TEXTURE_2D,p.texture);
                if(!p.frame.texture){
                if (p.cpuWidth!=p.frame.width || p.cpuHeight!=p.frame.height) {
                    p.width=p.frame.width; p.height=p.frame.height;p.cpuWidth=p.width;p.cpuHeight=p.height;
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,p.width,p.height,0,GL_RGBA,GL_UNSIGNED_BYTE,p.frame.rgba.data());
                } else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,p.width,p.height,GL_RGBA,GL_UNSIGNED_BYTE,p.frame.rgba.data());
                }
                p.width=p.frame.width;p.height=p.frame.height;
                ++p.frames;
            }
            if (!p.capture->error().empty()) {
                std::cerr << p.layout.output << ": " << p.capture->error() << '\n';
                p.failed=true; p.capture.reset(); result=1;
            }
        }
    };
    while (running && !interrupted) {
        const double frameStarted=monotonicSeconds();
        if(output && !output->pump()) break;
        tracking.update();
        if(!layoutPath.empty() && SDL_GetTicks64()>=nextLayoutCheck) {
            nextLayoutCheck=SDL_GetTicks64()+100;
            try {
                const auto version=std::filesystem::last_write_time(layoutPath);
                if(version!=layoutVersion) {
                    // The backend replaces this file atomically, including presentation settings.
                    auto layouts=readLayout(layoutPath);
                    int nextFps=fps;spatial::Workspace nextWorkspace=workspace;float nextSpacing=spacing;
                    std::ifstream settings(layoutPath);std::string first;
                    std::getline(settings,first);
                    if(first.starts_with("# settings ")) {
                        std::istringstream values(first.substr(11));std::string extra;
                        if(!(values>>nextFps>>nextWorkspace.amount>>nextSpacing) ||
                           nextFps<1 || nextFps>120 || !std::isfinite(nextWorkspace.amount) ||
                           nextWorkspace.amount<0 || nextWorkspace.amount>100 || !std::isfinite(nextSpacing) ||
                           nextSpacing<1 || nextSpacing>8192)
                            throw std::runtime_error("Invalid live presentation settings");
                        nextWorkspace.degrees=-1;
                        values>>std::ws;
                        if(!values.eof() && (!(values>>nextWorkspace.degrees) || !std::isfinite(nextWorkspace.degrees) || nextWorkspace.degrees < -1 || nextWorkspace.degrees>360))
                            throw std::runtime_error("Workspace wrap must be 0..360 degrees");
                        nextWorkspace.follow=false;values>>std::ws;
                        if(!values.eof()) {
                            int follow;
                            if(!(values>>follow) || (follow!=0 && follow!=1) || values>>extra)throw std::runtime_error("Invalid workspace wrapping setting");
                            nextWorkspace.follow=follow;
                        }
                    }
                    nextWorkspace.gap=nextSpacing/900;
                    float l=layouts[0].x,t=layouts[0].y,r=l,b=t;
                    for(const auto& p:layouts){l=std::min(l,p.x);t=std::min(t,p.y);r=std::max(r,p.x+p.width);b=std::max(b,p.y+p.height);}
                    const float newCx=(l+r)/2,newCy=(t+b)/2;
                    const bool wrapChanged=nextWorkspace.degrees!=workspace.degrees || nextWorkspace.follow!=workspace.follow;
                    const float newDistance=spatial::safeDistance(layouts,newCx,newCy,(r-l)/900.f,wrapChanged?5.f:distance,nextWorkspace,nextSpacing);
                    // Prepare only new/failed sources before altering the current scene.
                    std::vector<Panel> additions;
                    for(const auto& layout:layouts) {
                        auto old=std::find_if(panels.begin(),panels.end(),[&](const Panel& p){return p.layout.output==layout.output && !p.failed;});
                        if(old!=panels.end())continue;
                        Panel p;p.layout=layout;p.capture=std::make_unique<DesktopCapture>();
                        p.capture->setFrameRate(nextFps);
                        if(!p.capture->connect() || !p.capture->select(layout.output))throw std::runtime_error(p.capture->error());
                        additions.push_back(std::move(p));
                    }
                    std::vector<Panel> updated;updated.reserve(layouts.size());
                    for(const auto& layout:layouts) {
                        auto old=std::find_if(panels.begin(),panels.end(),[&](const Panel& p){return p.layout.output==layout.output && !p.failed;});
                        if(old!=panels.end()){updated.push_back(std::move(*old));old->texture=0;}
                        else {
                            auto added=std::find_if(additions.begin(),additions.end(),[&](const Panel& p){return p.layout.output==layout.output;});
                            updated.push_back(std::move(*added));
                            auto& p=updated.back();
                            glGenTextures(1,&p.texture);glBindTexture(GL_TEXTURE_2D,p.texture);
                            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
                            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                        }
                        updated.back().layout=layout;
                        updated.back().capture->setFrameRate(nextFps);
                    }
                    for(auto& p:panels)if(p.texture)glDeleteTextures(1,&p.texture);
                    panels=std::move(updated);geometry=std::move(layouts);result=0;
                    // Geometry safety must not change the camera's viewing depth.
                    const float geometryShift=newDistance-distance;
                    panZ+=geometryShift;targetPanZ+=geometryShift;
                    left=l;top=t;right=r;bottom=b;cx=newCx;cy=newCy;distance=newDistance;
                    workspace=nextWorkspace;spacing=nextSpacing;fps=nextFps;layoutVersion=version;
                    targetDistance=distance;
                    selection.validate(geometry);
                    if(!focusOutput.empty()){
                        if(selection.output.empty())fit();
                        else focusSelected(false,0);
                    }
                    std::cout<<"Live layout applied: "<<panels.size()<<" panels, "<<fps<<" fps"<<std::endl;
                }
            } catch(const std::exception& e) {
                std::cerr<<"Live layout update deferred: "<<e.what()<<std::endl;
                nextLayoutCheck=SDL_GetTicks64()+1000;
            }
        }
        controls.update();
        panGestureActive=controls.panActive;
        if(controls.panStarted || controls.panX || controls.panY)
            panSelected(float(controls.panX),float(controls.panY),controls.panStarted);
        sampleTarget();
        auto zoomBy = [&](float amount) {
            if(!focusSelected(false,amount)) {
                targetDistance=distance;
                targetPanZ=distance-navigation::zoomDepth(distance-targetPanZ,amount,maxZoomDepth());
            }
        };
        if(controls.zoom)zoomBy(float(controls.zoom));
        if(controls.fit==1)tracking.fitRequested=true;
        if(controls.fit==2)tracking.fitTargetRequested=true;
        if(controls.fit==3)tracking.recenterRequested=true;
        if(controls.fit==4)zoomBy(-std::log(.9f));
        if(controls.fit==5)zoomBy(std::log(.9f));
        if(tracking.zoom){zoomBy(float(tracking.zoom)*-std::log(.9f));tracking.zoom=0;}
        if(tracking.recenterRequested){recenterSelected();tracking.recenterRequested=false;}
        if(tracking.fitRequested){fit();tracking.camera.recenter(monotonicSeconds());tracking.fitRequested=false;}
        if(tracking.fitTargetRequested){fitTarget();tracking.fitTargetRequested=false;}
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type==SDL_QUIT) running=false;
            if (event.type==SDL_KEYDOWN) {
                if (event.key.keysym.sym==SDLK_ESCAPE) running=false;
                if (event.key.keysym.sym==SDLK_r) {recenterSelected();}
                if (event.key.keysym.sym==SDLK_f) {fit();tracking.camera.recenter(monotonicSeconds());}
            }
            if (event.type==SDL_MOUSEWHEEL) zoomBy(event.wheel.preciseY*-std::log(.9f));
            if (event.type==SDL_MOUSEMOTION) {
                if (event.motion.state&SDL_BUTTON_RMASK) { yaw+=event.motion.xrel*.15f; pitch=std::clamp(pitch+event.motion.yrel*.15f,-80.f,80.f); }
                if (event.motion.state&SDL_BUTTON_MMASK) { panX+=event.motion.xrel*distance*.0015f; panY-=event.motion.yrel*distance*.0015f; targetPanX=panX;targetPanY=panY; }
            }
        }
        const double cameraTime=monotonicSeconds();
        const float cameraDt=float(cameraTime-lastCameraTime);lastCameraTime=cameraTime;
        if(std::abs(std::log(distance/targetDistance))>1e-5f)
            distance=navigation::easeDistance(distance,targetDistance,cameraDt);
        else distance=targetDistance;
        navigationRotation=navigation::easeRotation(navigationRotation,targetRotation,cameraDt);
        panX=navigation::ease(panX,targetPanX,cameraDt);panY=navigation::ease(panY,targetPanY,cameraDt);
        panZ=navigation::ease(panZ,targetPanZ,cameraDt);
        if(panCamera){
            const auto found=std::find_if(geometry.begin(),geometry.end(),[&](const auto& p){return p.output==focusOutput;});
            if(found==geometry.end())panCamera=false;
            else {
                const auto& p=*found;
                const auto pose=spatial::pose((p.x+p.width/2-cx)/900,-(p.y+p.height/2-cy)/900,p.width/900,(right-left)/900,distance,workspace,p.curvature);
                smoothFocusX=navigation::ease(smoothFocusX,focusX,cameraDt);
                smoothFocusY=navigation::ease(smoothFocusY,focusY,cameraDt);
                // Interpolate surface coordinates, then derive the camera: the
                // entire path keeps constant zoom, even around a sphere.
                const auto camera=navigation::panFocus(pose,focusAnchor,focusDepth,smoothFocusX,smoothFocusY);
                navigationRotation=targetRotation=camera.rotation;
                panX=targetPanX=camera.pan.x;panY=targetPanY=camera.pan.y;panZ=targetPanZ=camera.pan.z;
            }
        }
        sampleTarget();
        if (!posePath.empty()) {
            const int state=tracking.camera.fresh(monotonicSeconds()) ? 1 : 0;
            if(state!=trackingStatus) {
                trackingStatus=state;
                std::cout<<"Head tracking: "<<(state ? "live" : "waiting / stale; holding view")<<std::endl;
                if(window) SDL_SetWindowTitle(window,state ? "Omarchy XR | Head tracking live | R: recenter | F: fit | Esc: exit" : "Omarchy XR | Tracking unavailable | Mouse look | R: recenter | Esc: exit");
            }
        }
        int viewportWidth,viewportHeight;dimensions(viewportWidth,viewportHeight);
        const auto view=viewRotation();
        int w,h; dimensions(w,h);
        if (w<=0 || h<=0) { SDL_Delay(16); continue; }
        // Share exactly the halo target; the compositor consumes monitor transitions only.
        controls.publishHover(direct && !selection.output.empty(),selection.output,0,0);
        for(auto& p:panels){
            const auto& l=p.layout;
            const auto pose=spatial::pose((l.x+l.width/2-cx)/900,-(l.y+l.height/2-cy)/900,l.width/900,(right-left)/900,distance,workspace,l.curvature);
            const auto plan=adaptive::project(l,pose,view,{panX,panY,panZ},std::max(1,viewportWidth/(stereo?2:1)),std::max(1,viewportHeight),fov,stereo?ipd/2000:0);
            p.visible=smoke || plan.visible;
            const float scale=p.quality.update(plan.scale,cameraTime);
            if(p.capture)p.capture->setDemand(p.visible,std::max(1u,unsigned(std::ceil(l.width*scale))),std::max(1u,unsigned(std::ceil(l.height*scale))));
        }
        updateCaptures();
        if (smoke && (result || SDL_GetTicks64()-started>15000)) { result=1; break; }
        for(auto& p:panels)p.halo=navigation::ease(p.halo,selection.output==p.layout.output ? 1.f:0.f,cameraDt);
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        environment.update(monotonicSeconds());
        auto renderScene=[&](int width,int height,bool stereoView,bool flipped){
            glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            int offsetX=0,offsetY=0;
            if(flipped){
                const float aspect=float(w/(stereo?2:1))/h;
                const int contentWidth=std::min(width,int(height*aspect));
                const int contentHeight=std::min(height,int(width/aspect));
                offsetX=(width-contentWidth)/2;offsetY=(height-contentHeight)/2;
                width=contentWidth;height=contentHeight;
            }
            const int eyes=stereoView?2:1,eyeWidth=width/eyes;
            for(int eye=0;eye<eyes;++eye){
                const float eyePosition=stereoView?(eye==0?-ipd/2000.f:ipd/2000.f):0;
                glViewport(offsetX+eye*eyeWidth,offsetY,eyeWidth,height);
                glMatrixMode(GL_PROJECTION);glLoadIdentity();
                const double t=.1*std::tan(fov*pi/360),r=t*eyeWidth/height;
                glFrustum(-r,r,flipped?t:-t,flipped?-t:t,.1,20000);
                glMatrixMode(GL_MODELVIEW);glLoadIdentity();
                environment.draw(tracking::matrix(view).data());
                glTranslatef(-eyePosition,0,0);
                glMultMatrixf(tracking::matrix(view).data());glTranslatef(panX,panY,panZ);
                for(const auto& p:panels)if(p.visible)drawHalo(p,cx,cy,(right-left)/900.f,distance,workspace);
                for(size_t i=0;i<panels.size();++i)if(panels[i].visible)drawPanel(panels[i],i,cx,cy,(right-left)/900.f,distance,workspace);
            }
        };
        if(tracking.spectator>=0){spectatorEnabled=tracking.spectator==1;tracking.spectator=-1;spectatorError.clear();}
        if(!spectatorEnabled)spectator.reset();
        if(spectatorEnabled){
            try{
                if(!spectator)spectator=std::make_unique<Spectator>();
                if(!spectator->pump()){spectator.reset();spectatorEnabled=false;}
                else if(spectator->begin(monotonicSeconds())){
                    renderScene(spectator->width(),spectator->height(),false,true);
                    spectator->present();
                }
            }catch(const std::exception& e){
                spectatorError=e.what();std::cerr<<spectatorError<<std::endl;
                glBindFramebuffer(GL_FRAMEBUFFER,0);spectator.reset();spectatorEnabled=false;
                while(glGetError()!=GL_NO_ERROR){} // optional window failure must not stop stereo
            }
        }
        const int eyeWidth=w/(stereo?2:1);
        renderScene(w,h,stereo,false);
        if (glGetError()!=GL_NO_ERROR) { std::cerr << "OpenGL rendering error\n"; result=1; break; }
        if(smoke && stereo && drawn==9) {
            std::vector<unsigned char> leftEye(eyeWidth*h*3),rightEye(leftEye.size());
            glPixelStorei(GL_PACK_ALIGNMENT,1);
            glReadPixels(0,0,eyeWidth,h,GL_RGB,GL_UNSIGNED_BYTE,leftEye.data());
            glReadPixels(eyeWidth,0,eyeWidth,h,GL_RGB,GL_UNSIGNED_BYTE,rightEye.data());
            if(leftEye==rightEye)throw std::runtime_error("Stereo validation failed: eye images are identical");
            std::cout<<"Stereo validation: distinct left/right images"<<std::endl;
        }
        double workMs=(monotonicSeconds()-frameStarted)*1000;
        if(output)output->swap([&]{
            const double started=monotonicSeconds();
            updateCaptures();
            workMs+=(monotonicSeconds()-started)*1000;
        });else SDL_GL_SwapWindow(window);
        workTimes.push_back(workMs);++drawn;++reportFrames;
        const double now=monotonicSeconds();frameTimes.push_back((now-frameStarted)*1000);
        if(now-reportTime>=5){
            std::sort(workTimes.begin(),workTimes.end());std::sort(frameTimes.begin(),frameTimes.end());
            std::cout<<"Performance: "<<reportFrames/(now-reportTime)<<" present fps, work p95 "<<workTimes[workTimes.size()*95/100]<<" ms, frame p95 "<<frameTimes[frameTimes.size()*95/100]<<" ms"<<std::endl;
            for(const auto& p:panels)if(p.capture)std::cout<<"Capture: "<<p.layout.output<<" "<<(p.visible?"visible":"paused")<<" "<<p.capture->transport()<<" "<<p.width<<"x"<<p.height<<" source "<<p.sourceWidth<<"x"<<p.sourceHeight<<" requests "<<p.capture->requests()<<" frames "<<p.frames<<std::endl;
            if(!posePath.empty()){
                const auto temp=posePath+".stats.tmp";
                {std::ofstream stats(temp);stats<<"{\"pid\":"<<getpid()<<",\"time\":"<<std::setprecision(12)<<now<<",\"fps\":"<<reportFrames/(now-reportTime)
                    <<",\"environmentLoading\":"<<(environment.loadingImage()?"true":"false")<<",\"environmentError\":"<<std::quoted(environment.error)
                    <<",\"geometryDistance\":"<<distance
                    <<",\"zoomDepth\":"<<(focusOutput.empty()?distance-panZ:focusDepth)<<",\"maxZoomDepth\":"<<maxZoomDepth()<<",\"panX\":"<<focusX<<",\"panY\":"<<focusY
                    <<",\"spectator\":"<<(spectator?"true":"false")<<",\"spectatorFrames\":"<<(spectator?spectator->frames:0)<<",\"spectatorError\":"<<std::quoted(spectatorError)
                    <<",\"workP95\":"<<workTimes[workTimes.size()*95/100]<<",\"frameP95\":"<<frameTimes[frameTimes.size()*95/100]<<",\"captures\":[";
                    bool first=true;for(const auto& p:panels)if(p.capture){
                        if(!first)stats<<",";
                        first=false;
                        stats<<"{\"output\":"<<std::quoted(p.layout.output)<<",\"visible\":"<<(p.visible?"true":"false")<<",\"transport\":"<<std::quoted(p.capture->transport())
                            <<",\"width\":"<<p.width<<",\"height\":"<<p.height<<",\"nativeWidth\":"<<p.sourceWidth<<",\"nativeHeight\":"<<p.sourceHeight<<"}";
                    }stats<<"]}";}
                std::rename(temp.c_str(),(posePath+".stats").c_str());
            }
            reportTime=now;reportFrames=0;workTimes.clear();frameTimes.clear();
        }
        if (smoke && drawn>=10 && std::all_of(panels.begin(),panels.end(),[](const Panel& p){ return !p.capture || p.frames>=10; })) running=false;
        if(!output && SDL_GL_GetSwapInterval()==0)SDL_Delay(1);
    }
    for (auto& p : panels) {
        if (p.capture || p.failed) std::cout << p.layout.output << ": " << p.frames << " frames (" << p.width << 'x' << p.height << ")\n";
        if (smoke && p.capture && p.frames<10) result=1;
        glDeleteTextures(1,&p.texture);
        p.capture.reset();
    }
    if (smoke && drawn<10) result=1;
    std::cout<<"Head pose samples: "<<tracking.camera.samples<<std::endl;
    spectator.reset();
    environment.release();
    if(context)SDL_GL_DeleteContext(context);
    if(window)SDL_DestroyWindow(window);
    output.reset();SDL_Quit();return result;
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
