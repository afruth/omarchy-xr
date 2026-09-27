// Execute the real renderer's input/camera path with a hidden window and synthetic
// monitor geometry. No compositor focus, capture, glasses or desktop input needed.
#define main rendererMain
#include "../src/main.cpp"
#undef main
#include <cassert>

void exerciseFocus(View& view, const std::string& pose) {
    view.controls.emplace(pose);
    view.geometry={{"OMXR-left",0,0,1920,1080,40},{"OMXR-right",1944,0,1920,1080,40}};
    view.left=0;view.right=3864;view.cx=1932;view.cy=540;view.yaw=20;
    view.gaze.current=targeting::Hit{};view.gaze.current->output="OMXR-left";
    view.flickIn();
    const auto rotation=view.targetRotation;
    const float x=view.targetPanX,y=view.targetPanY,z=view.targetPanZ,depth=view.focusDepth;
    view.fit();
    view.gaze.current->output="OMXR-right"; // explicit keyboard target wins over gaze
    auto request=[&](int seq,const std::string& name){
        std::ofstream file(pose+".controls.focus");
        file<<"v1 "<<getpid()<<' '<<seq<<' '<<name<<' '<<std::time(nullptr)<<'\n';
        file.close();view.steer();
    };
    view.yaw=20;request(1,"OMXR-left");
    assert(view.selection.output=="OMXR-left" && view.levelOutput=="OMXR-left" && view.level==View::Level::Monitor);
    assert(std::abs(view.focusDepth-depth)<1e-5 && std::abs(view.targetPanX-x)<1e-5);
    assert(std::abs(view.targetPanY-y)<1e-5 && std::abs(view.targetPanZ-z)<1e-5);
    assert(std::abs(view.targetRotation.w-rotation.w)<1e-5 && std::abs(view.targetRotation.y-rotation.y)<1e-5);
    assert(view.panX==0 && view.panY==0 && view.panZ==0); // schedules easing, never jumps the camera
    assert(view.interactionUntil>monotonicSeconds() && !view.panCamera && !view.zoomGaze);
    view.controls->paneValid=true;view.controls->paneOutput="OMXR-left";
    view.controls->paneX=100;view.controls->paneY=50;view.controls->paneW=800;view.controls->paneH=600;
    assert(view.fitPane() && view.level==View::Level::Pane);
    request(2,"OMXR-left");assert(view.level==View::Level::Monitor && view.focusDepth==depth);
    request(3,"OMXR-right");assert(view.selection.output=="OMXR-right" && view.levelOutput=="OMXR-right");
    const float rightX=view.targetPanX;
    request(4,"OMXR-disconnected");assert(view.selection.output=="OMXR-right" && view.targetPanX==rightX);
    view.fit();view.steer();assert(view.level==View::Level::Overview); // no replay without a new request
    request(5,"OMXR-left");
    for (int frame=0;frame<90;++frame) {
        view.lastCameraTime=monotonicSeconds()-1./120;
        view.easeCamera();
    }
    assert(std::abs(view.panX-view.targetPanX)<.005 && std::abs(view.panZ-view.targetPanZ)<.005);
}

// XR layer keys in the monitor scene (docs/xr-controls-plan.md §5.3-5.4): focus frames the gazed window's pane
// (at once when the pane already holds the look point, else once the adapter publishes it, else the monitor);
// previous/next frame the pane the adapter published with them; fill frames the gazed monitor.
void exerciseLayerKeys(View& view, const std::string& pose) {
    int seq=10;
    const auto pane=[&](const std::string& body) {
        std::ofstream file(pose+".controls.pane");file<<"v1 "<<getpid()<<' '<<++seq<<' '<<body<<' '<<std::time(nullptr)<<'\n';file.close();
        usleep(2000);view.controls->update();
    };
    const auto look=[&](const std::string& output,float x,float y) {
        view.gaze.current=targeting::Hit{};view.gaze.current->output=output;view.gaze.current->pixelX=x;view.gaze.current->pixelY=y;
    };
    view.fit();
    pane("OMXR-left 100 50 800 600");
    look("OMXR-left",300,200);
    auto serial=view.pointerSerial;
    view.monitorKey(18,"");
    assert(view.pointerSerial==serial+1 && view.pointerX==300 && view.pointerY==200); // the adapter focuses the window there
    assert(view.level==View::Level::Pane && view.paneWaitUntil==0);                    // the pane already holds it
    view.fit();look("OMXR-left",1500,900);view.monitorKey(18,"");
    assert(view.level==View::Level::Overview && view.paneWaitUntil>0);                  // waits for the adapter
    pane("OMXR-left 1000 500 900 580");view.steerPaneWait(monotonicSeconds());
    assert(view.level==View::Level::Pane && view.paneWaitUntil==0 && view.selection.output=="OMXR-left");
    view.fit();look("OMXR-left",50,1000);view.monitorKey(18,"");
    view.steerPaneWait(monotonicSeconds()+.6);
    assert(view.level==View::Level::Monitor && view.levelOutput=="OMXR-left");         // nothing came: the monitor
    // Previous/next: the pane arrives with the code (same update) or is the last one after 0.5 s.
    view.fit();pane("OMXR-right 0 0 1920 1080");
    assert(view.controls->paneChanged);view.monitorKey(22,"next");
    assert(view.level==View::Level::Pane && view.selection.output=="OMXR-right");
    view.fit();view.controls->update();assert(!view.controls->paneChanged);
    view.monitorKey(22,"prev");assert(view.level==View::Level::Overview);
    view.steerPaneWait(monotonicSeconds()+.6);assert(view.level==View::Level::Pane && view.levelOutput=="OMXR-right");
    // Fill: the adapter maximized the window; the camera frames the gazed monitor.
    look("OMXR-left",10,10);view.monitorKey(10,"");
    assert(view.level==View::Level::Monitor && view.levelOutput=="OMXR-left");
}

int main() {
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    SDL_Window* window=SDL_CreateWindow("XR camera test",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    assert(window);
    char temp[]="/tmp/xr-focus-renderer-XXXXXX";assert(mkdtemp(temp));
    const std::string pose=std::string(temp)+"/pose.sock",empty;
    {
        std::vector<Panel> panels;
        View view(panels,false,spatial::Workspace{80},24,empty,pose,false,true,64,28,empty,60,false);
        view.window=window;
        exerciseFocus(view,pose);
        exerciseLayerKeys(view,pose);
    }
    SDL_DestroyWindow(window);SDL_Quit();std::filesystem::remove_all(temp);
    std::cout<<"Workspace mailbox targets the requested monitor with flick-equivalent fit and smooth camera motion; XR focus, previous/next and fill frame the right window passed\n";
}
