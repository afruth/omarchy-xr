// Real renderer steering and mailbox protocol, without compositor input/capture.
#define main rendererMain
#include "../src/main.cpp"
#undef main
#include <cassert>

void grabControls(View& view,const std::string& directory);
void exercise(View& view,const std::string& directory) {
    view.controls.emplace(directory+"/pose.sock");
    view.notificationHud=std::make_unique<notifications::Hud>(directory);
    {std::ofstream file(directory+"/notifications.json");file<<"{\"version\":1,\"generation\":\"test\",\"time\":"
        <<std::fixed<<notifications::wallMilliseconds()<<",\"entries\":[{\"key\":\"one\",\"summary\":\"First\"},{\"key\":\"two\",\"summary\":\"Second\"}]}";}
    auto& hud=*view.notificationHud;
    for(int i=0;i<100 && !hud.visible();++i){SDL_Delay(10);hud.update(view.tracking.camera,monotonicSeconds());}
    assert(hud.count()==2);
    auto aim=[&]{
        auto now=monotonicSeconds();view.tracking.camera.timestamp=now;hud.update(view.tracking.camera,now);
        view.placeNotification(now);view.placeNotification(now+.1);view.placeNotification(now+.2);
        auto d=targeting::normalize(hud.position(0));
        view.tracking.camera.view=tracking::conjugate(tracking::orientation(0,-std::asin(d.y)*180/pi,-std::atan2(d.x,-d.z)*180/pi));
        view.placeNotification(now);assert(hud.highlight()==hud.front());
        view.controls->publishNotification(hud.highlight());AsyncFile::instance().flush();
        std::ifstream state(directory+"/pose.sock.controls.notification");std::string version,owner,token;state>>version>>owner>>token;
        assert(version=="v1" && owner==std::to_string(getpid()));return token;
    };
    int seq=0;
    auto request=[&](int mode,const std::string& token,long stamp=std::time(nullptr),std::string owner=std::to_string(getpid())) {
        std::ofstream file(directory+"/pose.sock.controls");++seq;
        file<<"v3 "<<owner<<' '<<seq<<" 0 "<<seq<<' '<<mode<<' '<<token<<' '<<stamp<<'\n';file.close();
        view.steer();
    };
    view.placeNotification(monotonicSeconds());
    const auto first=hud.front();const auto token=aim();
    const float x=view.targetPanX,y=view.targetPanY,z=view.targetPanZ;
    request(7,token);assert(hud.count()==2 && hud.front()!=first);
    assert(view.targetPanX==x && view.targetPanY==y && view.targetPanZ==z);
    auto next=aim();request(6,token);assert(hud.count()==2); // old gaze identity cannot close the new front
    request(6,next,std::time(nullptr)-5);assert(hud.count()==2);
    request(6,next,std::time(nullptr),"foreign");assert(hud.count()==2);
    request(6,"xyz");assert(hud.count()==2);
    request(6,next);assert(hud.count()==1);
    view.steer();assert(hud.count()==1); // serial is consumed exactly once
    assert(view.targetPanX==x && view.targetPanY==y && view.targetPanZ==z);
    next=aim();request(7,next);assert(hud.count()==1); // singleton cycling preserves it
    request(6,next);assert(hud.count()==0 && !hud.visible());
    // XR+N / XR+SHIFT+N (codes 20/21): the gazed card, else the front card, with or without gaze.
    {std::ofstream file(directory+"/notifications.json");file<<"{\"version\":1,\"generation\":\"keys\",\"time\":"
        <<std::fixed<<notifications::wallMilliseconds()<<",\"entries\":[{\"key\":\"three\",\"summary\":\"Third\"},{\"key\":\"four\",\"summary\":\"Fourth\"},{\"key\":\"five\",\"summary\":\"Fifth\"}]}";}
    for(int i=0;i<100 && hud.count()!=3;++i){SDL_Delay(10);hud.update(view.tracking.camera,monotonicSeconds());}
    assert(hud.count()==3);
    const auto front=hud.front();
    request(21,"-");assert(hud.count()==3 && hud.front()!=front);   // no gaze: cycles the stack
    request(20,"-");assert(hud.count()==2);                          // no gaze: dismisses the front card
    next=aim();request(20,next);assert(hud.count()==1);             // gazed card
    request(20,hextoken::encodeHex("unknown"));assert(hud.count()==0); // an unknown identity acts on the front card
    request(20,"-");assert(hud.count()==0);                          // nothing left: no-op
    hud.release();
}
// Codes 8..29 are accepted in both scenes (the renderer routes them); Alt-Tab's release bind sends mode 11 with the
// token "release"; prev/next (22) and grab (26) need their token; 20/21 may omit theirs.
void releaseToken(const std::string& directory) {
    const auto accepted=[&](bool canvasMode,int mode,const std::string& token) {
        const std::string pose=directory+(canvasMode?"/canvas.sock":"/monitors.sock");
        LiveControls controls(pose);controls.setCanvasMode(canvasMode);
        {std::ofstream file(pose+".controls");file<<"v3 "<<getpid()<<" 1 0 1 "<<mode<<' '<<token<<' '<<std::time(nullptr)<<'\n';}
        controls.update();
        return controls.fit==mode?controls.notificationTarget:std::string("rejected");
    };
    assert(accepted(true,11,hextoken::encodeHex("release"))=="release");
    assert(accepted(true,12,"-").empty());
    assert(accepted(true,14,"-")=="rejected"); // a direction needs its token
    assert(accepted(false,11,hextoken::encodeHex("release"))=="release");
    assert(accepted(false,22,hextoken::encodeHex("next"))=="next" && accepted(false,22,"-")=="rejected");
    assert(accepted(true,26,hextoken::encodeHex("begin"))=="begin" && accepted(false,26,"-")=="rejected");
    assert(accepted(false,20,"-").empty() && accepted(true,21,"-").empty() && accepted(false,28,"-").empty());
    assert(accepted(false,29,"-").empty() && accepted(true,29,"-").empty()); // the performance card
    assert(accepted(false,30,"-")=="rejected");
    assert(accepted(true,6,hextoken::encodeHex("card"))=="card" && accepted(false,7,hextoken::encodeHex("card"))=="card");
}
// The same flicks on a canvas View (offline scene, three windows): 6/7 dismiss and cycle and never pan.
// The inside-ring berth itself is covered by canvas_focus.cpp notificationInsideRing.
void canvasFlicks(SDL_Window* window,const std::string& directory) {
    std::filesystem::create_directories(directory);
    std::vector<Panel> none;const std::string empty;
    View view(none,false,spatial::Workspace{40},30,empty,empty,false,true,64,28,empty,60,false);
    view.window=window;view.mode=View::SceneMode::Canvas;
    view.canvas=std::make_unique<canvas::Scene>(canvas::Ring{},canvas::Settings{},"",true);
    const auto record=[](std::uint64_t address,unsigned w,unsigned h,int focus,windows::Place place) {
        windows::Record r;r.address=address;r.cls="foot";r.title="w"+std::to_string(address);r.w=w;r.h=h;r.focusHistoryID=focus;r.place=place;r.pid=int(address);
        return r;
    };
    windows::List list;
    list.records={record(0xa1,1920,1080,1,windows::Place::Park),record(0xb2,1280,720,0,windows::Place::Stage),record(0xc3,800,600,2,windows::Place::Park)};
    view.canvas->setFov(view.canvasFov());
    assert(view.canvas->adopt(list,1));view.canvas->tick(1,0);
    exercise(view,directory);
    grabControls(view,directory+"/grab");
    assert(view.monitorMathCalls==0);
}
// Grab (code 26, docs/xr-controls-plan.md §5.7) through the real mailbox: begin/end, a second begin, any other
// action, stale tracking and 30 s end it; in the canvas head pitch scrolls the cylinder.
void grabControls(View& view,const std::string& directory) {
    std::filesystem::create_directories(directory);
    view.controls.emplace(directory+"/pose.sock");
    int seq=0;
    const auto request=[&](int mode,const std::string& token) {
        std::ofstream file(directory+"/pose.sock.controls");++seq;
        file<<"v3 "<<getpid()<<' '<<seq<<" 0 "<<seq<<' '<<mode<<' '<<token<<' '<<std::time(nullptr)<<'\n';file.close();
        view.steer();
    };
    auto& camera=view.tracking.camera;
    const auto sample=[&](double pitch,double yaw) {
        SDL_Delay(2); // distinct stamps at microsecond precision
        const double now=monotonicSeconds();
        // to_string rounds to microseconds, so the stamp may be just after now: accept with 1 ms of slack.
        assert(camera.accept("euler-nwu-v1 "+std::to_string(now)+" 0 "+std::to_string(pitch)+" "+std::to_string(yaw),now+.001));
    };
    const auto begin=hextoken::encodeHex("begin"),end=hextoken::encodeHex("end");
    sample(0,5);sample(0,5);
    request(26,begin);assert(camera.grabbing);
    request(26,begin);assert(!camera.grabbing);               // a second press ends a grab whose release was missed
    request(26,begin);assert(camera.grabbing);
    const double neutral=camera.neutralYaw;
    sample(0,60);assert(camera.neutralYaw!=neutral && std::abs(std::remainder(camera.neutralYaw-camera.yaw-camera.grabOffset,360.0))<1e-9); // carried
    request(26,end);assert(!camera.grabbing);
    const double reached=camera.neutralYaw;
    sample(0,90);assert(camera.neutralYaw==reached);            // released: the reference stays
    request(26,begin);request(4,"-");assert(!camera.grabbing);  // another action ends it
    request(26,begin);assert(camera.grabbing);
    camera.timestamp=monotonicSeconds()-1;view.steer();assert(!camera.grabbing); // stale tracking
    sample(0,90);request(26,begin);view.grabStarted-=31;view.steer();assert(!camera.grabbing); // 30 s
    if(view.canvas) {
        sample(0,90);request(26,begin);
        const float before=view.canvas->camera.targetScrollY;
        sample(7,90);view.steer();
        const float lifted=before-view.canvas->camera.targetScrollY;
        assert(lifted>0);                                      // looking down lifts the camera
        request(26,end);
        sample(0,90);view.steer();assert(view.canvas->camera.targetScrollY==before-lifted);
    }
}
int main() {
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    auto* window=SDL_CreateWindow("Notification controls",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(window);
    auto context=SDL_GL_CreateContext(window);assert(context);
    char temp[]="/tmp/xr-notification-controls-XXXXXX";assert(mkdtemp(temp));
    {
        std::vector<Panel> panels;const std::string empty;
        View view(panels,false,spatial::Workspace{40},30,empty,empty,false,true,64,28,empty,60,false);
        view.window=window;exercise(view,temp);
    }
    canvasFlicks(window,std::string(temp)+"/canvas");
    {
        std::vector<Panel> panels;const std::string empty;
        View view(panels,false,spatial::Workspace{40},30,empty,empty,false,true,64,28,empty,60,false);
        view.window=window;grabControls(view,std::string(temp)+"/grab-monitors");
    }
    releaseToken(temp);
    AsyncFile::instance().flush();std::filesystem::remove_all(temp);
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"Gaze mailbox and renderer steering: cycle, dismiss, identity, stale/foreign input, replay, no camera fit (monitors and canvas), keyboard dismiss/cycle, the XR layer codes and grab passed\n";
}
