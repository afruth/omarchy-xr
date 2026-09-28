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
// previous/next show the pane the adapter published with them at the current zoom level; Up/Down step the
// levels; fill frames the gazed monitor.
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
    // Previous/next keep the zoom level. The pane arrives with the code (same update) or is the last one after
    // 0.5 s. All monitors: only the selection moves; the camera stays.
    view.fit();pane("OMXR-right 0 0 1920 1080");
    const auto overviewRotation=view.targetRotation;const float overviewZ=view.targetPanZ;
    assert(view.controls->paneChanged);view.monitorKey(22,"next");
    assert(view.level==View::Level::Overview && view.selection.output=="OMXR-right" && view.targetPanZ==overviewZ);
    assert(std::abs(view.targetRotation.w-overviewRotation.w)<1e-9 && std::abs(view.targetRotation.y-overviewRotation.y)<1e-9);
    view.controls->update();assert(!view.controls->paneChanged);
    view.monitorKey(22,"prev");view.steerPaneWait(monotonicSeconds()+.6);assert(view.level==View::Level::Overview);
    // Monitor level: the next window is centred at the zoom that fits its monitor, even off the monitor centre.
    view.fitOutput("OMXR-left");const float monitorDepth=view.focusDepth;
    pane("OMXR-right 100 50 800 600");view.monitorKey(22,"next");
    assert(view.level==View::Level::Monitor && view.levelOutput=="OMXR-right" && std::abs(view.focusDepth-monitorDepth)<1e-4f);
    assert(std::abs(view.focusX-(500-960)/900.f)<1e-4f && std::abs(view.focusY-(540-350)/900.f)<1e-4f);
    // Window level: the next window is fitted.
    view.fitPane();assert(view.level==View::Level::Pane);
    pane("OMXR-left 1000 500 900 580");view.monitorKey(22,"next");
    assert(view.level==View::Level::Pane && view.levelOutput=="OMXR-left" && view.focusDepth<monitorDepth);
    // Up/Down (codes 30/31): all -> the gazed monitor -> the gazed window (focused), and back.
    view.fit();look("OMXR-left",1200,700);view.sceneKey(30,"");
    assert(view.level==View::Level::Monitor && view.levelOutput=="OMXR-left");
    serial=view.pointerSerial;view.sceneKey(30,"");
    assert(view.pointerSerial==serial+1 && view.level==View::Level::Pane && view.levelOutput=="OMXR-left");   // the pane holds the look point
    view.sceneKey(31,"");assert(view.level==View::Level::Monitor && view.levelOutput=="OMXR-left");
    view.sceneKey(31,"");assert(view.level==View::Level::Overview);
    // Fill: the adapter maximized the window; the camera frames the gazed monitor.
    look("OMXR-left",10,10);view.monitorKey(10,"");
    assert(view.level==View::Level::Monitor && view.levelOutput=="OMXR-left");
}

// Monitor-scene search (docs/xr-controls-plan.md §5.5) through the real mailboxes: the prompt opens on the gazed
// monitor, .results lists the ranked windows (recent first, then the query), keys move the selection, Enter
// asks the adapter to land (.land) and closes the prompt, Esc closes it.
void exerciseSearch(View& view, const std::string& pose) {
    const auto base=pose+".controls";
    const auto read=[](const std::string& file) { AsyncFile::instance().flush(); std::ifstream in(file); return std::string((std::istreambuf_iterator<char>(in)), {}); };
    const auto lines=[&](const std::string& file) { std::vector<std::string> out; std::istringstream in(read(file)); for (std::string l; std::getline(in, l);) out.push_back(l); return out; };
    const auto row=[](const char* address,const char* cls,const char* title,int focus) {
        return std::string(address)+' '+hextoken::encodeHex(cls)+' '+hextoken::encodeHex(title)+" 800 600 0 0 "+std::to_string(focus)+" off 0 100 0 0";
    };
    {std::ofstream file(base+".windows");file<<"v1 "<<getpid()<<" 1 "<<std::time(nullptr)<<'\n'<<row("0xa1","firefox","Docs - Mozilla Firefox",2)<<'\n'
        <<row("0xb2","foot","build: make check",0)<<'\n'<<row("0xc3","code","main.cpp - omarchy-xr",1)<<'\n';}
    usleep(2000);view.controls->update();assert(view.controls->windows && view.controls->windows->records.size()==3);
    view.gaze.current=targeting::Hit{};view.gaze.current->output="OMXR-right";
    view.monitorKey(9,"");
    assert(view.monitorSearch.open && read(base+".prompt").find(" 1 OMXR-right ")!=std::string::npos);
    auto results=lines(base+".results");
    assert(results.size()==4 && results[1]=="1 "+hextoken::encodeHex("foot")+' '+hextoken::encodeHex("build: make check")); // most recent first, selected
    assert(results[2].starts_with("0 "+hextoken::encodeHex("code")) && results[3].starts_with("0 "+hextoken::encodeHex("firefox")));
    const auto prompt=[&]{ std::istringstream in(read(base+".prompt")); std::string v,o; unsigned long long seq; in>>v>>o>>seq; return seq; }();
    int edit=0;
    const auto type=[&](const std::string& text,const std::string& keys,bool open=true) {
        std::ofstream file(base+".search");
        edit+=keys=="-" ? 1 : int(std::count(keys.begin(), keys.end(), ',')+1); // the prompt counts every key
        file<<"v1 "<<getpid()<<' '<<prompt<<' '<<edit<<' '<<hextoken::encodeHex(text)<<' '<<(open?1:0)<<' '<<keys<<' '<<std::time(nullptr)<<'\n';
        file.close();usleep(2000);view.steer();
    };
    type("fire","-");results=lines(base+".results");
    assert(results.size()==2 && results[1].starts_with("1 "+hextoken::encodeHex("firefox")));
    type("","-");type("","down,down");results=lines(base+".results");
    assert(results[3].starts_with("1 "+hextoken::encodeHex("firefox")));     // two down: the third row
    type("","up,enter");
    assert(!view.monitorSearch.open && read(base+".prompt").find(" 0 - ")!=std::string::npos);
    assert(read(base+".land").find(" 0xc3 ")!=std::string::npos);                // the row above: code
    assert(lines(base+".results").size()==1 && view.paneFallback==View::PaneFallback::None && view.paneWaitUntil>0);
    view.monitorKey(9,"");assert(view.monitorSearch.open);
    const auto again=[&]{ std::istringstream in(read(base+".prompt")); std::string v,o; unsigned long long seq; in>>v>>o>>seq; return seq; }();
    std::ofstream(base+".search")<<"v1 "<<getpid()<<' '<<again<<" 1 - 0 esc "<<std::time(nullptr)<<'\n';usleep(2000);view.steer();
    assert(!view.monitorSearch.open);
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
        exerciseSearch(view,pose);
    }
    SDL_DestroyWindow(window);SDL_Quit();std::filesystem::remove_all(temp);
    std::cout<<"Workspace mailbox targets the requested monitor with flick-equivalent fit and smooth camera motion; XR focus, previous/next and fill frame the right window; search lists, filters, selects, lands and closes passed\n";
}
