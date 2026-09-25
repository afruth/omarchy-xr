// The renderer's scene seam: geometry, cylinder, per-surface views and the stats mode, on the real View.
#define main rendererMain
#include "../src/main.cpp"
#undef main
#include "gpu_capture.hpp"
#include <cassert>

void samePose(const spatial::Pose& a, const spatial::Pose& b) {
    assert(a.center.x==b.center.x && a.center.y==b.center.y && a.center.z==b.center.z);
    assert(a.yaw==b.yaw && a.surfaceBend==b.surfaceBend && a.latitude==b.latitude && a.spherical==b.spherical);
}
void geometrySeam(View& view) {
    assert(&view.sceneGeometry()==&view.geometry && view.sceneGeometry().size()==2);
    const auto c=view.sceneCylinder();
    assert(c.cx==view.cx && c.cy==view.cy && c.span==view.span() && c.distance==view.distance);
    assert(c.workspace.amount==view.workspace.amount && c.workspace.degrees==view.workspace.degrees);
    assert(c.workspace.follow==view.workspace.follow && c.workspace.gap==view.workspace.gap);
    for (const auto& l:view.sceneGeometry()) samePose(c.pose(l), view.monitorPose(l));
    const auto* left=view.findLayout("OMXR-left");
    assert(left && left==&view.geometry[0] && view.findLayout("OMXR-right")==&view.geometry[1]);
    assert(view.findLayout("nope")==nullptr);
}
void surfaceViews(View& view) {
    auto& panels=view.panels;
    panels[0].width=100;panels[0].frame.texture=7;panels[1].failed=true;panels[1].width=100;
    panels[0].halo=.5f;panels[1].visible=false;
    std::vector<SurfaceView> seen;
    view.forEachSurface([&](const SurfaceView& s){ seen.push_back(s); });
    assert(seen.size()==2);
    assert(seen[0].layout==&panels[0].layout && seen[1].layout==&panels[1].layout);
    assert(seen[0].status==&panels[0].captureStatus && seen[1].status==&panels[1].captureStatus);
    assert(seen[0].texture==7 && seen[0].width==100 && seen[1].width==0 && seen[1].texture==panels[1].texture);
    assert(seen[0].halo==.5f && seen[0].visible && !seen[1].visible);
    panels[0].frame.texture=0;panels[0].halo=0;panels[1].visible=true;
}
// Window captures share one GBM device: injected captures find the display and never destroy it.
void sharedDevice() {
    int fd=-1;
    for (const auto& e:std::filesystem::directory_iterator("/dev/dri", std::filesystem::directory_options::skip_permission_denied))
        if (e.path().filename().string().starts_with("renderD") && (fd=open(e.path().c_str(),O_RDWR|O_CLOEXEC))>=0) break;
    if (fd<0 || eglGetCurrentDisplay()==EGL_NO_DISPLAY) { if (fd>=0) close(fd); std::cout<<"Shared device: skipped (no render node or EGL display)\n"; return; }
    auto* device=gbm_create_device(fd);assert(device);
    {
        GpuCapture a(device), b(device);
        assert(!a.ownsDevice && !b.ownsDevice && a.device==device && a.display==EGL_NO_DISPLAY);
        assert(a.init() && a.display==eglGetCurrentDisplay() && a.device==device && a.deviceFd<0);
        assert(b.init() && b.display!=EGL_NO_DISPLAY);
    }
    auto* bo=gbm_bo_create(device,16,16,GBM_FORMAT_ARGB8888,GBM_BO_USE_RENDERING);
    if (bo) gbm_bo_destroy(bo);
    assert(gbm_device_get_fd(device)==fd);
    gbm_device_destroy(device);close(fd);
}
void statsMode(View& view, const std::string& pose) {
    assert(view.mode==View::SceneMode::Monitors);
    view.controls.emplace(pose);
    view.environment=std::make_unique<SkyEnvironment>("");
    view.writeStats(1,0,0,false,0,0,0,0);
    AsyncFile::instance().flush();
    std::ifstream file(pose+".stats");const std::string stats((std::istreambuf_iterator<char>(file)),{});
    assert(stats.find("\"mode\":\"monitors\"")!=std::string::npos);
}
void focusSetup(View& view, const std::string& pose) {
    view.controls.emplace(pose);
    view.geometry={{"OMXR-left",0,0,1920,1080,40},{"OMXR-right",1944,0,1920,1080,40}};
    view.left=0;view.right=3864;view.cx=1932;view.cy=540;view.yaw=20;
    view.gaze.current=targeting::Hit{};view.gaze.current->output="OMXR-left";
}
void sameTarget(const View& a, const View& b) {
    assert(a.targetPanX==b.targetPanX && a.targetPanY==b.targetPanY && a.targetPanZ==b.targetPanZ);
    assert(a.targetRotation.w==b.targetRotation.w && a.targetRotation.x==b.targetRotation.x);
    assert(a.targetRotation.y==b.targetRotation.y && a.targetRotation.z==b.targetRotation.z);
    assert(a.focusDepth==b.focusDepth && a.level==b.level && a.levelOutput==b.levelOutput);
}
// navigate() must be a pure router: each verb lands exactly where the direct call does.
void navigateEquivalence(SDL_Window* window, const std::string& pose) {
    std::vector<Panel> none;const std::string empty;
    View a(none,false,spatial::Workspace{80},24,empty,pose,false,true,64,28,empty,60,false);
    View b(none,false,spatial::Workspace{80},24,empty,pose,false,true,64,28,empty,60,false);
    a.window=b.window=window;focusSetup(a,pose);focusSetup(b,pose);
    a.flickIn();b.navigate({View::Verb::FlickIn});sameTarget(a,b);
    a.fit();b.fit();a.zoomBy(.3f);b.navigate({View::Verb::ZoomBy,.3f});sameTarget(a,b);
    a.recenterSelected();b.navigate({View::Verb::Recenter});sameTarget(a,b);
    a.fit();a.tracking.camera.recenter(monotonicSeconds());b.navigate({View::Verb::Fit});sameTarget(a,b);
    assert(a.level==View::Level::Overview && b.level==View::Level::Overview);
    a.flickIn();b.flickIn();a.panSelected(10,5,true);b.navigate({View::Verb::Pan,10,5,true});sameTarget(a,b);
    a.flickOut();b.navigate({View::Verb::FlickOut});sameTarget(a,b);
    a.fitTarget();b.navigate({View::Verb::FitTarget});sameTarget(a,b);
    assert(b.level==View::Level::Monitor && b.levelOutput=="OMXR-left");
    a.fitOutput("OMXR-right");b.navigate({.verb=View::Verb::FitOutput,.output="OMXR-right"});sameTarget(a,b);
    assert(b.levelOutput=="OMXR-right");
}
// Three tests construct View over an empty panel list; the scene must stay well-defined.
void zeroPanels(SDL_Window* window, const std::string& pose) {
    std::vector<Panel> none;const std::string empty;
    View v(none,false,spatial::Workspace{40},30,empty,pose,false,true,64,28,empty,60,false);
    v.window=window;v.sceneBounds();
    assert(v.geometry.empty() && v.cx==0 && v.cy==0 && v.left==0 && v.right==0);
    v.controls.emplace(pose);v.sampleTarget();v.fit();
    float l=1,t=1,r=1,b=1;View::boundsOf({},l,t,r,b);
    assert(l==0 && t==0 && r==0 && b==0);
}
// A newer backend may append fields after the wrap flag; known fields are still validated.
void liveSettings(const std::string& temp, const std::string& pose) {
    const std::string path=temp+"/viewer.tsv", empty;
    std::ofstream(path)<<"# settings 60 40 24 -1 0 canvas extra\nOMXR-a 0 0 1920 1080\n";
    std::vector<Panel> none;
    View v(none,false,spatial::Workspace{80},30,empty,pose,false,true,64,28,path,60,false);
    int fps=1;spatial::Workspace ws;float sp=1;
    v.readLiveSettings(fps,ws,sp);
    assert(fps==60 && ws.amount==40 && sp==24 && ws.degrees==-1 && !ws.follow);
    std::ofstream(path)<<"# settings 60 40 24 -1 2\nOMXR-a 0 0 1920 1080\n";
    bool threw=false;
    try { v.readLiveSettings(fps,ws,sp); } catch (const std::runtime_error&) { threw=true; }
    assert(threw);
}
windows::Record canvasRecord(std::uint64_t address, unsigned w, unsigned h, int focus, windows::Place place=windows::Place::Park) {
    windows::Record r; r.address=address; r.cls="foot"; r.title="w"+std::to_string(address); r.w=w; r.h=h; r.focusHistoryID=focus; r.place=place; r.pid=int(address);
    return r;
}
// Overview labels: one raster per address and title, a bounded atlas, constant angular height at any zoom.
void canvasLabels(View& v) {
    canvas::Labels labels;
    const GLuint first=labels.texture("0xa1\tone","foot","one",48);
    assert(first && labels.texture("0xa1\tone","foot","one",48)==first && labels.atlas.size()==1);
    const GLuint retitled=labels.texture("0xa1\ttwo","foot","two",48);
    assert(retitled && retitled!=first && labels.atlas.size()==2);
    for (int i=0;i<600;++i) labels.texture("0x"+std::to_string(i)+"\tt","c","t",16);
    labels.trim(512);assert(labels.atlas.size()==512 && !labels.atlas.contains("0xa1\tone"));
    labels.release();assert(labels.atlas.empty());
    auto& s=*v.canvas;s.state=canvas::Scene::State::Overview;
    std::vector<float> heights;
    for (const float zoom:{.2f,1.f}) {
        s.camera.zoom=s.camera.targetZoom=zoom;s.refresh(3);s.cull(0,180);
        for (auto& w:s.windows) w.visible=true;
        const auto quads=s.labelQuads();
        assert(quads.size()==s.windows.size());
        for (const auto& q:quads) {
            const auto* l=v.findLayout(q.layout.output);
            assert(q.texture && std::abs(q.layout.y+q.layout.height+8-l->y)<1e-2f && q.layout.width<=l->width+1e-3f && q.u>0 && q.u<=1);
            heights.push_back(s.ring.heading(q.layout.height));
        }
    }
    for (const float h:heights) assert(std::abs(h-s.settings.labelDeg/.6f)<1e-4f);
    s.state=canvas::Scene::State::Work;s.camera.zoom=s.camera.targetZoom=.2f;s.refresh(3);
    assert(s.labelQuads().empty());   // Work: windows under 6° get no label
    s.releaseGpu();assert(s.labels.atlas.empty());
}
// Lease loss (ensureLease): textures, slots, labels and the hub go with the context; regenerate gives every
// window a fresh texture and reopens sources on the next tick, a closed window keeps its status.
void canvasLease(View& v) {
    auto& s=*v.canvas;
    for (auto& w:s.windows) if (!w.texture) w.texture=gltex::create();
    s.windows[2].closed=true;s.windows[2].captureStatus="closed";
    s.releaseGpu();s.releaseGpu();
    for (const auto& w:s.windows) assert(!w.texture && !w.frame.texture && !w.source && !w.cpuWidth);
    assert(!s.hub && s.labels.atlas.empty());
    s.hubRetryAt=9;s.hubRetryMs=4000;
    s.regenerate(5);
    for (const auto& w:s.windows) assert(w.texture && glIsTexture(w.texture) && w.retryAt==5 && w.retryMs==500);
    assert(s.windows[0].captureStatus=="reconnecting after lease" && s.windows[2].captureStatus=="closed");
    assert(s.hubRetryAt==0 && s.hubRetryMs==500);
    s.recoverHub(5);s.openSources(5);s.tick(5,0);
    assert(s.projected.size()==s.windows.size() && v.monitorMathCalls==0);
    std::vector<SurfaceView> seen;
    v.forEachSurface([&](const SurfaceView& view){ seen.push_back(view); });
    assert(seen.size()==s.windows.size());
    for (size_t i=0;i<seen.size();++i) assert(seen[i].status==&s.windows[i].captureStatus && seen[i].texture==s.windows[i].texture);
    s.releaseGpu();
}
// Canvas mode through the same seam: projected window geometry on the ring cylinder, no monitor math.
void canvasSeam(SDL_Window* window, const std::string& pose) {
    std::vector<Panel> none;const std::string empty;
    View v(none,false,spatial::Workspace{40},30,empty,pose,false,true,64,28,empty,60,false);
    v.window=window;v.mode=View::SceneMode::Canvas;
    v.canvas=std::make_unique<canvas::Scene>(canvas::Ring{},canvas::Settings{},"",true);
    windows::List list;
    list.records={canvasRecord(0xa1,1920,1080,1),canvasRecord(0xb2,1280,720,0,windows::Place::Stage),canvasRecord(0xc3,800,600,2)};
    v.canvas->setFov(v.canvasFov());
    assert(v.canvas->adopt(list,1));v.canvas->tick(1,0);
    const float R=v.canvas->ring.radius;
    assert(&v.sceneGeometry()==&v.canvas->geometry() && v.sceneGeometry().size()==3);
    const auto c=v.sceneCylinder();
    assert(c.distance==R && c.cx==0 && c.cy==0 && c.workspace.degrees==360 && c.workspace.follow);
    for (const auto& l:v.sceneGeometry()) { const auto p=c.pose(l); assert(std::abs(std::hypot(p.center.x,p.center.z)-R)<1e-3f); }
    std::vector<SurfaceView> seen;
    v.forEachSurface([&](const SurfaceView& s){ seen.push_back(s); });
    assert(seen.size()==3);
    for (size_t i=0;i<seen.size();++i) assert(seen[i].layout==&v.sceneGeometry()[i] && seen[i].status==&v.canvas->windows[i].captureStatus);
    assert(v.findLayout("0xb2")==&v.sceneGeometry()[1] && v.canvas->staged()==&v.canvas->windows[1] && !v.findLayout("0xdead"));
    v.controls.emplace(pose);v.environment=std::make_unique<SkyEnvironment>("");
    v.writeStats(1,0,0,false,0,0,0,0);
    AsyncFile::instance().flush();
    std::ifstream file(pose+".stats");const std::string stats((std::istreambuf_iterator<char>(file)),{});
    assert(stats.find("\"mode\":\"canvas\"")!=std::string::npos && stats.find("\"canvasWindows\":3")!=std::string::npos);
    assert(stats.find("\"output\":\"0xb2\"")!=std::string::npos && stats.find("\"tiers\":{")!=std::string::npos);
    assert(v.monitorMathCalls==0 && v.canvas->occluders().size()<=3);
    for (unsigned i=0;i<40;++i) list.records.push_back(canvasRecord(0x100+i,640,480,int(i)+3));
    v.canvas->adopt(list,2);v.canvas->tick(2,0);v.canvas->cull(0,180);
    assert(v.canvas->candidates.size()==43 && v.canvas->occluders().size()==24 && v.monitorMathCalls==0);
    canvasLabels(v);
    canvasLease(v);
}
int main() {
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    auto* window=SDL_CreateWindow("Scene seam",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(window);
    auto context=SDL_GL_CreateContext(window);assert(context);
    char temp[]="/tmp/xr-scene-seam-XXXXXX";assert(mkdtemp(temp));
    {
        std::vector<Panel> panels(2);const std::string empty, pose=std::string(temp)+"/pose.sock";
        panels[0].layout={"OMXR-left",0,0,1920,1080,40};panels[1].layout={"OMXR-right",1944,0,1920,1080,40};
        View view(panels,false,spatial::Workspace{40},30,empty,pose,false,true,64,28,empty,60,false);
        view.window=window;view.sceneBounds();
        geometrySeam(view);surfaceViews(view);statsMode(view,pose);
        for (auto& p:panels) glDeleteTextures(1,&p.texture);
    }
    sharedDevice();
    navigateEquivalence(window,std::string(temp)+"/pose.sock");
    zeroPanels(window,std::string(temp)+"/pose.sock");
    liveSettings(temp,std::string(temp)+"/pose.sock");
    canvasSeam(window,std::string(temp)+"/pose.sock");
    AsyncFile::instance().flush();std::filesystem::remove_all(temp);
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"Scene seam: geometry, cylinder poses, layout lookup, surface views, stats mode, shared device, navigate routing, zero panels, live settings, the canvas seam, canvas labels and canvas lease regeneration passed\n";
}
