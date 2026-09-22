// Real renderer steering and mailbox protocol, without compositor input/capture.
#define main rendererMain
#include "../src/main.cpp"
#undef main
#include <cassert>

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
    hud.release();
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
    AsyncFile::instance().flush();std::filesystem::remove_all(temp);
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"Gaze mailbox and renderer steering: cycle, dismiss, identity, stale/foreign input, replay and no camera fit passed\n";
}
