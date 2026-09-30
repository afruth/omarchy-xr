#define main rendererMain
#include "../src/main.cpp"
#undef main
#include <cassert>

namespace {
windows::List list(unsigned seq=1) {
    windows::List result; result.seq=seq; result.outputName="OMXR-test-canvas";
    for(unsigned i=1;i<=2;++i) {
        windows::Record row; row.address=i; row.cls="editor"; row.title="File \"quoted\"\nSecond line";
        row.w=1280; row.h=720; row.focusHistoryID=i==1 ? 0 : 1;
        row.place=i==1 ? windows::Place::Stage : windows::Place::Park;
        result.records.push_back(row);
    }
    return result;
}
void inputIndicators(View& v) {
    auto& scene=*v.canvas;
    scene.focusedName="0x2"; scene.selected="0x1";
    scene.cull(0,180);
    for(auto& window:scene.windows) window.visible=true;
    scene.labelQuads();
    assert(scene.labels.atlas.count("0x2\t[Keyboard input] "+scene.find("0x2")->record.title));
    scene.noteDwell("0x1",1); assert(scene.hintFor=="0x1");
    scene.noteConfirm(); scene.noteDwell("0x1",10); assert(scene.hintFor=="0x1");
    auto* window=scene.findMutable("0x2"); window->pinned=true;
    window->width=1280; window->height=720; glGenTextures(1,&window->texture);
    scene.refresh(10); scene.overlayQuads(v.overlayScene(v.currentView()),10);
    assert(scene.labels.atlas.count("pinned-input"));
    window->pinned=false;
}
void mapAndCommands(View& v) {
    std::ostringstream out; v.writeWindowStats(out,monotonicSeconds());
    auto* json=json_tokener_parse(("["+out.str()+"]").c_str()); assert(json);
    assert(json_object_array_length(json)==2);
    json_object* title=nullptr;
    assert(json_object_object_get_ex(json_object_array_get_idx(json,0),"title",&title));
    assert(std::string(json_object_get_string(title))==v.canvas->windows[0].record.title);
    json_object_put(json);
    assert(v.tracking.canvasVerb("summon:0x2"));v.steerPoseVerbs();
    assert(v.canvas->landed=="0x2");
    assert(v.tracking.canvasVerb("pin:0x2"));v.steerPoseVerbs();assert(v.canvas->find("0x2")->pinned);
    assert(v.tracking.canvasVerb("pin:0x2"));v.steerPoseVerbs();assert(!v.canvas->find("0x2")->pinned);
    assert(!v.tracking.canvasVerb("focus:0x2;stop"));
}
void savedView(View& v) {
    v.canvas->camera.targetFocusX=600;v.canvas->camera.targetFocusY=300;
    v.canvas->camera.targetZoom=.55;v.canvas->camera.targetScrollY=300;
    v.canvas->depth=1.8;v.canvas->aimX=600;v.canvas->aimY=0;
    v.saveComfort();AsyncFile::instance().flush();assert(std::filesystem::exists(v.comfortPath()));
    v.canvas->camera.targetZoom=1;v.panCamera=true;
    assert(v.restoreComfort());assert(!v.panCamera);
    assert(std::abs(v.canvas->camera.targetZoom-.55)<1e-5 && v.canvas->depth==1.8f);
    v.canvas->ring.radius=1.2;assert(v.restoreComfort());
    assert(v.canvas->depth<=1.2f && std::hypot(v.targetPanX,v.targetPanY,v.targetPanZ)<1.2f);
    v.canvas->ring.radius=2.4;
}
void arrivalsAndDeferredRestore(View& v) {
    v.canvas=std::make_unique<canvas::Scene>(canvas::Ring{},canvas::Settings{},"",true);
    assert(!v.restoreComfort() && v.comfortRestorePending);
    v.windowsSeq=0;v.adoptList(list());
    assert(!v.comfortRestorePending && std::abs(v.canvas->camera.targetZoom-.55)<1e-5);
    v.canvas->settings.followNewWindows=false;
    auto next=list(2); next.records[0].focusHistoryID=1;
    windows::Record added=next.records[1];added.address=3;added.focusHistoryID=0;next.records.push_back(added);
    const auto before=v.targetPanX, zoom=v.canvas->camera.targetZoom;
    v.adoptList(next);assert(v.unfollowedArrival=="0x3");
    v.interactionUntil=0;v.canvasFollow("0x3",monotonicSeconds());
    assert(v.targetPanX==before && v.canvas->camera.targetZoom==zoom);
}
void monitorSavedView(SDL_Window* window,const std::string& temp) {
    std::vector<Panel> panels(1);panels[0].layout={"OMXR-test-1",0,0,1920,1080};
    const std::string empty,settings=temp+"/viewer.tsv",pose=temp+"/monitor-pose.sock",another=temp+"/another-pose.sock";
    std::ofstream(settings)<<"OMXR-test-1 0 0 1920 1080\n";
    View first(panels,false,{},24,empty,pose,false,false,64,28,settings,60,false);
    first.window=window;first.sceneBounds();first.primeCamera();
    first.targetPanX=.15;first.targetPanY=.2;first.targetPanZ=.4;first.focusDepth=2;
    first.targetRotation=tracking::orientation(0,0,7);
    first.saveComfort();AsyncFile::instance().flush();
    first.panCamera=true;first.targetPanX=0;first.targetPanZ=0;
    assert(first.restoreComfort() && !first.panCamera);
    assert(std::abs(first.targetPanX-.15f)<1e-6 && std::abs(first.targetPanZ-.4f)<1e-6);
    first.lastCameraTime=monotonicSeconds()-.1;first.easeCamera();
    assert(std::abs(first.targetPanX-.15f)<1e-6);
    View next(panels,false,{},24,empty,another,false,false,64,28,settings,60,false);
    next.window=window;next.primeCamera();
    assert(next.restoreComfort() && std::abs(next.targetPanX-.15f)<1e-6);
    assert(first.comfortPath()==next.comfortPath());
    for(auto& panel:panels) glDeleteTextures(1,&panel.texture);
}
}
int main() {
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    auto* window=SDL_CreateWindow("UX rendering",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(window);
    auto context=SDL_GL_CreateContext(window);assert(context);
    char temp[]="/tmp/xr-ux-XXXXXX";assert(mkdtemp(temp));
    {
        std::vector<Panel> panels;
        const std::string empty,pose=std::string(temp)+"/runtime-pose.sock",settings=std::string(temp)+"/canvas.tsv";
        View v(panels,false,{},24,empty,pose,false,false,64,28,settings,60,false);
        v.window=window;v.mode=View::SceneMode::Canvas;v.canvasPath=v.layoutPath;
        v.controls.emplace(pose);v.controls->setCanvasMode(true);
        v.canvas=std::make_unique<canvas::Scene>(canvas::Ring{},canvas::Settings{},"",true);
        v.adoptList(list());v.canvas->snap();
        inputIndicators(v);mapAndCommands(v);savedView(v);arrivalsAndDeferredRestore(v);
        v.comfortSampleOpen=true;v.drawComfortSample(v.currentView());assert(v.comfortRaster.texture);
        v.comfortRaster.release();v.canvas->releaseGpu();
    }
    monitorSavedView(window,temp);
    AsyncFile::instance().flush();std::filesystem::remove_all(temp);
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"UX rendering: real focus indicators, map serialization and actions, persistent view, deferred restore, and arrivals passed\n";
}
