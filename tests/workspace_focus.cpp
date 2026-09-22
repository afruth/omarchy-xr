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
    }
    SDL_DestroyWindow(window);SDL_Quit();std::filesystem::remove_all(temp);
    std::cout<<"Workspace mailbox targets the requested monitor with flick-equivalent fit and smooth camera motion\n";
}
