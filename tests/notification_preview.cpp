// Real renderer, synthetic desktop content. No compositor captures or SDK calls.
#define main rendererMain
#include "../src/main.cpp"
#undef main
#include <cassert>

namespace {
constexpr int width=1920,height=1080;
void screenshot(const std::filesystem::path& path){
    std::vector<unsigned char> pixels(width*height*4),upright(pixels.size());
    glReadPixels(0,0,width,height,GL_BGRA,GL_UNSIGNED_BYTE,pixels.data());
    for(int y=0;y<height;++y)std::copy_n(pixels.data()+(height-y-1)*width*4,width*4,upright.data()+y*width*4);
    auto* image=cairo_image_surface_create_for_data(upright.data(),CAIRO_FORMAT_ARGB32,width,height,width*4);
    assert(cairo_surface_write_to_png(image,path.c_str())==CAIRO_STATUS_SUCCESS);cairo_surface_destroy(image);
}
void assertReadingVisible(){
    // A head-on close-up used to fail its own depth test and render black.
    std::vector<unsigned char> pixels(400*200*4);
    glReadPixels(width/2-200,height/2-100,400,200,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    int visible=0;
    for(size_t i=0;i<pixels.size();i+=4)if(pixels[i]+pixels[i+1]+pixels[i+2]>30)++visible;
    assert(visible>400*200/2);
}
void publish(const std::filesystem::path& folder){
    std::ofstream file(folder/"notifications.json.tmp");
    file<<"{\"version\":1,\"generation\":\"preview\",\"time\":"<<std::fixed<<notifications::wallMilliseconds()
        <<",\"count\":3,\"palette\":{\"background\":\"#16242d\",\"text\":\"#d6e2ee\",\"accent\":\"#8bc9eb\"},"
        "\"entries\":[{\"key\":\"preview\",\"app\":\"Omarchy\",\"summary\":\"Your workspace is ready\","
        "\"body\":\"Floating beside your workspace.\\nLook here and flick up to dismiss.\"},{\"key\":\"build\",\"app\":\"Build\",\"summary\":\"All tests passed\"},"
        "{\"key\":\"mail\",\"app\":\"Mail\",\"summary\":\"New message\"}]}";
    file.close();std::filesystem::rename(folder/"notifications.json.tmp",folder/"notifications.json");
}
void makeScene(View& view,const std::filesystem::path& directory){
    view.sceneBounds();view.primeCamera();view.panZ=view.targetPanZ;
    {std::ofstream file(directory/"environment.tsv");file<<"72 0 \"builtin:tron\" 1\n";}
    view.environment=std::make_unique<SkyEnvironment>((directory/"environment.tsv").string());
    // Reproducible pixels: the sky pulse runs on the synthetic clock and the accent is the stock
    // colour, not this machine's Omarchy theme, so check-preview can diff against a baseline.
    view.accent.path=(directory/"colors.toml").string();
    view.environment->update(100);view.accent.update(100);
    publish(directory);view.notificationHud=std::make_unique<notifications::Hud>(directory.string());
    for(int i=0;i<100 && !view.notificationHud->visible();i++){SDL_Delay(10);view.notificationHud->update(view.tracking.camera,100);}
    assert(view.notificationHud->visible());
}
void run(View& view,const std::filesystem::path& directory,bool video){
    auto frame=[&](double time){
        publish(directory);view.lastCameraTime=time;view.tracking.camera.timestamp=time;
        view.notificationHud->update(view.tracking.camera,time);view.placeNotification(time);
    };
    auto capture=[&](const char* name){
        if(std::string(name).find("frames/")!=0){const auto& f=view.notificationHud->placement();
            std::cout<<name<<" position "<<f.position.x<<","<<f.position.y<<","<<f.position.z<<" safe "<<f.safe<<" in view "<<f.onscreen<<" phase "<<f.phase()<<" opacity "<<f.opacity<<"\n";}
        view.renderScene(width*2,height,true,false,width*2,height);
        screenshot(directory/name);};
    for(int i=0;i<120;++i)frame(100+i/60.);
    capture("overview.png");assert(view.notificationHud->placement().safe);
    // A measured 45-degree head turn, then settle. The saved sequence includes
    // the entire retreat -> behind-screen orbit -> approach trajectory.
    std::ofstream trace(directory/"motion.csv");trace<<"time,x,y,z,phase,safe,behind,in_view,speed\n";
    for(int i=120;i<1320;++i){
        const float yaw=std::min(45.f,(i-120)*.5f);
        view.tracking.camera.view=tracking::conjugate(tracking::orientation(0,0,yaw));frame(100+i/60.);
        const auto& f=view.notificationHud->placement();
        trace<<i/60.<<','<<f.position.x<<','<<f.position.y<<','<<f.position.z<<','<<f.phase()<<','<<f.safe<<','<<f.behind<<','<<f.onscreen<<','<<f.speed()<<'\n';
        if(i==180)capture("turn.png");
        if(i==420)capture("behind.png");
        if(video && i%2==0){char name[64];std::snprintf(name,sizeof(name),"frames/%04d.png",(i-120)/2);capture(name);}
    }
    capture("settled.png");assert(view.notificationHud->placement().safe);
    view.tracking.camera.view={};view.panZ=view.distance-1.25f;
    for(int i=1320;i<3120;++i)frame(100+i/60.);
    assert(!view.notificationHud->placement().onscreen);
    capture("zoomed.png");
    // Turn toward the card: it should stay put while being read.
    const auto p=view.notificationHud->position(0);
    const auto d=targeting::normalize(targeting::add(p,{view.panX,view.panY,view.panZ}));
    view.tracking.camera.view=tracking::conjugate(tracking::orientation(0,-std::asin(d.y)*180/pi,-std::atan2(d.x,-d.z)*180/pi));
    for(int i=3120;i<3240;++i)frame(100+i/60.);
    capture("reading.png");
    assert(view.notificationHud->placement().onscreen);
    assertReadingVisible();
    assert(!view.notificationHud->highlight().empty());
    assert(view.notificationHud->flick(view.notificationHud->highlight(),false));
    capture("cycled.png");
}
}
int main(int argc,char** argv){
    if(argc<2){std::cerr<<"Usage: notification-preview OUTPUT_DIR [--video]\n";return 1;}
    const std::filesystem::path directory=argv[1];std::filesystem::create_directories(directory/"frames");
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    auto* window=SDL_CreateWindow("Spatial notification preview",0,0,width*2,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(window);
    auto context=SDL_GL_CreateContext(window);assert(context);
    std::cout<<"OpenGL renderer: "<<glGetString(GL_RENDERER)<<'\n';
    const std::string empty;std::vector<Panel> panels(3);
    for(int i=0;i<3;++i)panels[i].layout={"preview-"+std::to_string(i),float(i*1950),0,1920,1080};
    View view(panels,false,spatial::Workspace{40},30,empty,empty,false,true,64,28,empty,60,false);view.window=window;
    makeScene(view,directory);glEnable(GL_DEPTH_TEST);run(view,directory,argc>2);
    assert(glGetError()==GL_NO_ERROR);
    view.notificationHud->release();view.notificationHud.reset();view.environment->release();view.halo.release();
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
}
