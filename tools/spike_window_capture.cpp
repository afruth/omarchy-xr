// M0 spike for the window canvas (docs/infinite-canvas-plan.md §3.6, S1/S2/S4/S12).
// Captures Hyprland toplevels by address with hyprland_toplevel_export_v1 over one Wayland
// connection, imports DMA-BUFs into a GL context and optionally renders a synthetic stereo
// load that samples every texture. Prints one JSON line per second, then a summary.
//
// usage: spike-window-capture [--seconds N] [--render HZ] [--pull] [--shm] [--cursor]
//                             [--inflight K] ADDRESS[:FPS] ...
// ADDRESS[:FPS[:pull][:iK]] — ":pull" sets ignore_damage and ":iK" K requests in flight for that window.
// ADDRESS is the hex address from `hyprctl clients -j`; FPS defaults to 60. --inflight K keeps
// K staggered requests per window; fps then counts distinct presentation timestamps.
//        spike-window-capture --client WIDTHxHEIGHT TITLE [MAX_FPS]
// opens an animated vsync'd test window and prints its own presented fps once per second.
#include "gpu_capture.hpp"
#include "hyprland-toplevel-export-client.h"
#include <SDL2/SDL.h>
#include <wayland-client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <poll.h>
#include <string>
#include <sys/mman.h>
#include <vector>
#include <map>
#include <set>

using Clock=std::chrono::steady_clock;
static double ms(Clock::duration d){return std::chrono::duration<double,std::milli>(d).count();}

struct Globals {
    wl_display* display=nullptr;
    wl_shm* shm=nullptr;
    zwp_linux_dmabuf_v1* dmabuf=nullptr;
    hyprland_toplevel_export_manager_v1* manager=nullptr;
    unsigned managerVersion=0;
};

struct Window {
    Globals* g=nullptr;
    uint64_t address=0;
    double interval=1000./60;
    bool shmOnly=false,pull=false,cursor=false;
    hyprland_toplevel_export_frame_v1* frame=nullptr;
    std::unique_ptr<GpuCapture> gpu;
    bool gpuMode=false,ready=false,failed=false,submitted=false;
    uint32_t shmFormat=0,shmW=0,shmH=0,shmStride=0,dmaFormat=0,dmaW=0,dmaH=0,flags=0;
    wl_buffer* shmBuffer=nullptr;void* shmPixels=MAP_FAILED;size_t shmSize=0;unsigned shmAllocW=0,shmAllocH=0;
    GLuint shmTexture=0;
    GLuint texture=0;unsigned texW=0,texH=0;
    Clock::time_point requested{},next{},readyAt{};
    uint64_t stamp=0;std::vector<uint64_t> stamps;
    // stats for the current second and the whole run
    unsigned frames=0,failures=0,totalFrames=0,totalFailures=0;
    std::vector<double> waitMs,importMs,allImport,allWait;
    uint64_t bytes=0;
    std::string transport="none";

    static void onBuffer(void* d,hyprland_toplevel_export_frame_v1*,uint32_t fmt,uint32_t w,uint32_t h,uint32_t stride){
        auto& s=*static_cast<Window*>(d);s.shmFormat=fmt;s.shmW=w;s.shmH=h;s.shmStride=stride;}
    static void onDamage(void*,hyprland_toplevel_export_frame_v1*,uint32_t,uint32_t,uint32_t,uint32_t){}
    static void onFlags(void* d,hyprland_toplevel_export_frame_v1*,uint32_t f){static_cast<Window*>(d)->flags=f;}
    static void onReady(void* d,hyprland_toplevel_export_frame_v1*,uint32_t hi,uint32_t lo,uint32_t ns){
        auto& s=*static_cast<Window*>(d);s.ready=true;s.readyAt=Clock::now();
        s.stamp=((uint64_t(hi)<<32|lo)*1000000000ull)+ns;}
    static void onFailed(void* d,hyprland_toplevel_export_frame_v1*){static_cast<Window*>(d)->failed=true;}
    static void onDma(void* d,hyprland_toplevel_export_frame_v1*,uint32_t fmt,uint32_t w,uint32_t h){
        auto& s=*static_cast<Window*>(d);s.dmaFormat=fmt;s.dmaW=w;s.dmaH=h;}
    static void onBufferDone(void* d,hyprland_toplevel_export_frame_v1* f){static_cast<Window*>(d)->submit(f);}
    static constexpr hyprland_toplevel_export_frame_v1_listener listener{onBuffer,onDamage,onFlags,onReady,onFailed,onDma,onBufferDone};
    static void release(void* d,wl_buffer* b){auto& s=*static_cast<Window*>(d);if(s.gpu)s.gpu->noteRelease(b);}
    static constexpr wl_buffer_listener bufferListener{release};

    bool allocateShm(){
        if(shmBuffer && shmAllocW==shmW && shmAllocH==shmH)return true;
        if(shmBuffer)wl_buffer_destroy(shmBuffer);
        if(shmPixels!=MAP_FAILED)munmap(shmPixels,shmSize);
        shmSize=size_t(shmStride)*shmH;
        int fd=memfd_create("spike-capture",MFD_CLOEXEC);
        if(fd<0 || ftruncate(fd,off_t(shmSize))!=0){if(fd>=0)close(fd);return false;}
        shmPixels=mmap(nullptr,shmSize,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        auto pool=wl_shm_create_pool(g->shm,fd,int(shmSize));
        shmBuffer=wl_shm_pool_create_buffer(pool,0,int(shmW),int(shmH),int(shmStride),shmFormat);
        wl_shm_pool_destroy(pool);close(fd);
        shmAllocW=shmW;shmAllocH=shmH;return shmPixels!=MAP_FAILED;
    }
    void submit(hyprland_toplevel_export_frame_v1* f){
        gpuMode=false;
        if(!shmOnly && dmaW && g->dmabuf){
            if(!gpu)gpu=std::make_unique<GpuCapture>();
            if(gpu->allocate(g->dmabuf,dmaW,dmaH,dmaFormat)){
                gpuMode=true;
                if(!wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(gpu->buffer())))wl_buffer_add_listener(gpu->buffer(),&bufferListener,this);
                gpu->markBusy();
            }else if(gpu->createFailed){shmOnly=true;gpu->clearSource();}
        }
        if(!gpuMode && !allocateShm()){failed=true;return;}
        transport=gpuMode?"dmabuf":"shm";
        hyprland_toplevel_export_frame_v1_copy(f,gpuMode?gpu->buffer():shmBuffer,pull?1:0);
        submitted=true;
    }
    void request(Clock::time_point now){
        frame=hyprland_toplevel_export_manager_v1_capture_toplevel(g->manager,cursor?1:0,uint32_t(address&0xffffffffu));
        hyprland_toplevel_export_frame_v1_add_listener(frame,&listener,this);
        requested=now;ready=failed=submitted=false;dmaW=dmaH=0;
        const auto step=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(interval));
        if(next==Clock::time_point{} || now-next>step)next=now+step;else next+=step;
    }
    void finish(){
        if(frame)hyprland_toplevel_export_frame_v1_destroy(frame);
        frame=nullptr;
    }
    // Import the ready frame into `texture` at native size (a real renderer would downscale here).
    void import(){
        const auto t0=Clock::now();
        const unsigned w=gpuMode?dmaW:shmW,h=gpuMode?dmaH:shmH;
        if(gpuMode){
            texture=gpu->present(w,h,flags&HYPRLAND_TOPLEVEL_EXPORT_FRAME_V1_FLAGS_Y_INVERT);
        }else{
            if(!shmTexture)glGenTextures(1,&shmTexture);
            glBindTexture(GL_TEXTURE_2D,shmTexture);
            glPixelStorei(GL_UNPACK_ROW_LENGTH,int(shmStride/4));
            if(texW!=w || texH!=h)glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,int(w),int(h),0,GL_BGRA,GL_UNSIGNED_BYTE,shmPixels);
            else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,int(w),int(h),GL_BGRA,GL_UNSIGNED_BYTE,shmPixels);
            glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            texture=shmTexture;
        }
        glFinish();
        texW=w;texH=h;
        const double imp=ms(Clock::now()-readyAt),wait=ms(readyAt-requested);
        (void)t0;
        importMs.push_back(imp);allImport.push_back(imp);waitMs.push_back(wait);allWait.push_back(wait);
        bytes+=uint64_t(w)*h*4;++frames;++totalFrames;stamps.push_back(stamp);
    }
};

static void global(void* d,wl_registry* r,uint32_t id,const char* iface,uint32_t version){
    auto& g=*static_cast<Globals*>(d);
    if(!strcmp(iface,wl_shm_interface.name))g.shm=static_cast<wl_shm*>(wl_registry_bind(r,id,&wl_shm_interface,1));
    else if(!strcmp(iface,zwp_linux_dmabuf_v1_interface.name) && version>=3)
        g.dmabuf=static_cast<zwp_linux_dmabuf_v1*>(wl_registry_bind(r,id,&zwp_linux_dmabuf_v1_interface,3));
    else if(!strcmp(iface,hyprland_toplevel_export_manager_v1_interface.name)){
        g.managerVersion=std::min(version,2u);
        g.manager=static_cast<hyprland_toplevel_export_manager_v1*>(wl_registry_bind(r,id,&hyprland_toplevel_export_manager_v1_interface,g.managerVersion));
    }
}
static void removed(void*,wl_registry*,uint32_t){}
static constexpr wl_registry_listener registryListener{global,removed};

static double pct(std::vector<double> v,double p){
    if(v.empty())return -1;
    std::sort(v.begin(),v.end());
    return v[std::min(v.size()-1,size_t(p*(v.size()-1)+.5))];
}

// Synthetic scene: draw every texture twice (two eyes) into a 3840x1080 FBO, like the viewer's SBS pass.
struct Load {
    GLuint fbo=0,color=0;
    std::vector<double> frameMs;
    void init(){
        glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,3840,1080,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
    }
    void draw(const std::vector<std::unique_ptr<Window>>& windows){
        const auto t0=Clock::now();
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,3840,1080);
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_2D);
        const int n=int(windows.size()),cols=std::max(1,int(std::ceil(std::sqrt(double(n)))));
        for(int eye=0;eye<2;++eye){
            glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,cols,cols,0,-1,1);
            glViewport(eye*1920,0,1920,1080);
            for(int i=0;i<n;++i){
                if(!windows[i]->texture)continue;
                const float x=float(i%cols),y=float(i/cols);
                glBindTexture(GL_TEXTURE_2D,windows[i]->texture);
                glBegin(GL_QUADS);
                glTexCoord2f(0,0);glVertex2f(x,y);glTexCoord2f(1,0);glVertex2f(x+.95f,y);
                glTexCoord2f(1,1);glVertex2f(x+.95f,y+.95f);glTexCoord2f(0,1);glVertex2f(x,y+.95f);
                glEnd();
            }
        }
        glDisable(GL_TEXTURE_2D);glBindFramebuffer(GL_FRAMEBUFFER,0);
        glFinish();
        frameMs.push_back(ms(Clock::now()-t0));
    }
};

// A cheap animated client: frame callbacks drive SDL's vsync'd swap, so the printed rate is the
// rate Hyprland lets the client draw at (throttled on hidden workspaces or off-screen).
static int runClient(const char* size,const char* title,double cap){
    unsigned w=0,h=0;if(sscanf(size,"%ux%u",&w,&h)!=2 || !w || !h)return 2;
    SDL_SetHint(SDL_HINT_VIDEODRIVER,"wayland");
    SDL_SetHint("SDL_VIDEO_WAYLAND_WMCLASS","omarchy-xr-spike-client");
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    auto window=SDL_CreateWindow(title,SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,int(w),int(h),SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    auto context=window?SDL_GL_CreateContext(window):nullptr;
    if(!context)return 1;
    SDL_GL_SetSwapInterval(1);
    const auto start=Clock::now();auto report=start+std::chrono::seconds(1);
    unsigned frames=0;
    // Titles starting with "fs" ask for fullscreen after 1.5 s, like a browser's F11.
    bool askFullscreen=!strncmp(title,"fs",2);
    for(bool running=true;running;){
        for(SDL_Event e;SDL_PollEvent(&e);)if(e.type==SDL_QUIT)running=false;
        if(askFullscreen && Clock::now()-start>std::chrono::milliseconds(1500)){SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP);askFullscreen=false;}
        int dw=0,dh=0;SDL_GL_GetDrawableSize(window,&dw,&dh);
        const double t=ms(Clock::now()-start)/1000;
        glViewport(0,0,dw,dh);
        glClearColor(float(.5+.5*std::sin(t)),float(.5+.5*std::sin(t*1.3+2)),float(.5+.5*std::sin(t*.7+4)),1);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        const int bar=std::max(8,dw/20),x=int((std::fmod(t,2.)/2.)*(dw-bar));
        glScissor(x,0,bar,dh);glClearColor(1,1,1,1);glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        SDL_GL_SwapWindow(window);++frames;
        if(cap>0)SDL_Delay(Uint32(1000./cap));
        if(Clock::now()>=report){
            report+=std::chrono::seconds(1);
            printf("{\"t\":%.1f,\"title\":\"%s\",\"fps\":%u}\n",t,title,frames);fflush(stdout);frames=0;
        }
    }
    return 0;
}

int main(int argc,char** argv){
    if((argc==4 || argc==5) && !strcmp(argv[1],"--client"))return runClient(argv[2],argv[3],argc==5?atof(argv[4]):0);
    double seconds=10,renderHz=0;bool pull=false,shm=false,cursor=false;int inflight=1;
    std::vector<std::pair<uint64_t,double>> targets;
    std::set<uint64_t> pulled;
    std::map<uint64_t,int> lanes;  // per-window ":iK" overrides --inflight
    for(int i=1;i<argc;++i){
        std::string a=argv[i];
        if(a=="--seconds" && i+1<argc)seconds=atof(argv[++i]);
        else if(a=="--render" && i+1<argc)renderHz=atof(argv[++i]);
        else if(a=="--pull")pull=true;
        else if(a=="--inflight" && i+1<argc)inflight=std::clamp(atoi(argv[++i]),1,4);
        else if(a=="--shm")shm=true;
        else if(a=="--cursor")cursor=true;
        else{
            auto colon=a.find(':');
            targets.push_back({std::stoull(a.substr(0,colon),nullptr,16),colon==std::string::npos?60.:atof(a.c_str()+colon+1)});
            if(a.find(":pull")!=std::string::npos)pulled.insert(targets.back().first);
            if(auto k=a.find(":i");k!=std::string::npos)lanes[targets.back().first]=std::clamp(atoi(a.c_str()+k+2),1,4);
        }
    }
    if(targets.empty()){fprintf(stderr,"usage: %s [--seconds N] [--render HZ] [--pull] [--shm] [--cursor] ADDRESS[:FPS]...\n",argv[0]);return 2;}

    SDL_SetHint(SDL_HINT_VIDEODRIVER,"wayland");
    if(SDL_Init(SDL_INIT_VIDEO)!=0){fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    auto sdlWindow=SDL_CreateWindow("omarchy-xr-spike",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    auto context=sdlWindow?SDL_GL_CreateContext(sdlWindow):nullptr;
    if(!context){fprintf(stderr,"GL context: %s\n",SDL_GetError());return 1;}
    fprintf(stderr,"GL renderer: %s\n",glGetString(GL_RENDERER));

    Globals g;
    g.display=wl_display_connect(nullptr);
    if(!g.display){fprintf(stderr,"no wayland\n");return 1;}
    auto registry=wl_display_get_registry(g.display);
    wl_registry_add_listener(registry,&registryListener,&g);
    wl_display_roundtrip(g.display);wl_display_roundtrip(g.display);
    if(!g.manager){fprintf(stderr,"compositor lacks hyprland_toplevel_export_manager_v1\n");return 1;}
    fprintf(stderr,"toplevel export v%u, dmabuf %s\n",g.managerVersion,g.dmabuf?"yes":"no");

    std::vector<std::unique_ptr<Window>> windows;
    const auto boot=Clock::now();
    // Spread first requests of equal-rate windows over one period so captures do not arrive in bursts.
    std::map<double,std::pair<int,int>> phase;  // fps -> (next index, count)
    for(auto [address,fps]:targets)++phase[fps].second;
    for(auto [address,fps]:targets){const int k=lanes.count(address)?lanes[address]:inflight;
        const double offset=(1000./std::clamp(fps,0.1,240.))*phase[fps].first++/phase[fps].second;
        for(int lane=0;lane<k;++lane){
        auto w=std::make_unique<Window>();
        const double interval=1000./std::clamp(fps,0.1,240.);
        w->g=&g;w->address=address;w->interval=interval*k;
        w->next=boot+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(offset/k+interval*lane));
        w->shmOnly=shm;w->pull=pull || pulled.count(address);w->cursor=cursor;
        windows.push_back(std::move(w));
    }}
    std::map<uint64_t,std::set<uint64_t>> unique;
    Load load;if(renderHz>0)load.init();
    const auto start=Clock::now();auto report=start+std::chrono::seconds(1);
    auto nextRender=start;
    const auto renderStep=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(renderHz>0?1000./renderHz:1e9));
    while(Clock::now()-start<std::chrono::duration<double>(seconds)){
        auto now=Clock::now();
        for(auto& w:windows){
            if(w->frame && w->ready){w->import();w->finish();}
            else if(w->frame && w->failed){++w->failures;++w->totalFailures;w->finish();}
            else if(w->frame && now-w->requested>std::chrono::seconds(3)){++w->failures;++w->totalFailures;w->finish();}
            if(!w->frame && now>=w->next)w->request(now);
        }
        if(renderHz>0 && now>=nextRender){load.draw(windows);nextRender+=renderStep;if(now-nextRender>renderStep)nextRender=now+renderStep;}
        wl_display_flush(g.display);
        pollfd p{wl_display_get_fd(g.display),POLLIN,0};
        while(wl_display_prepare_read(g.display)!=0)wl_display_dispatch_pending(g.display);
        if(poll(&p,1,1)>0)wl_display_read_events(g.display);else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);
        if(Clock::now()>=report){
            report+=std::chrono::seconds(1);
            printf("{\"t\":%.1f,\"windows\":[",ms(Clock::now()-start)/1000);
            for(size_t i=0;i<windows.size();++i){
                auto& w=*windows[i];
                printf("%s{\"addr\":\"%lx\",\"fps\":%u,\"fail\":%u,\"size\":\"%ux%u\",\"transport\":\"%s\",\"wait_p50\":%.1f,\"import_p50\":%.2f,\"import_p99\":%.2f,\"MBps\":%.1f}",
                    i?",":"",(unsigned long)w.address,w.frames,w.failures,w.texW,w.texH,w.transport.c_str(),pct(w.waitMs,.5),pct(w.importMs,.5),pct(w.importMs,.99),w.bytes/1e6);
                w.frames=w.failures=0;w.waitMs.clear();w.importMs.clear();w.bytes=0;
            }
            printf("],\"unique\":{");
            std::map<uint64_t,std::set<uint64_t>> second;
            for(auto& w:windows){for(auto t:w->stamps){second[w->address].insert(t);unique[w->address].insert(t);}w->stamps.clear();}
            bool first=true;
            for(auto& [a,set]:second){printf("%s\"%lx\":%zu",first?"":",",(unsigned long)a,set.size());first=false;}
            printf("}");
            if(renderHz>0){printf(",\"render_p50\":%.2f,\"render_p99\":%.2f,\"render_n\":%zu",pct(load.frameMs,.5),pct(load.frameMs,.99),load.frameMs.size());load.frameMs.clear();}
            printf("}\n");fflush(stdout);
        }
    }
    const double elapsed=ms(Clock::now()-start)/1000;
    printf("{\"summary\":true,\"seconds\":%.1f,\"windows\":[",elapsed);
    for(size_t i=0;i<windows.size();++i){
        auto& w=*windows[i];
        printf("%s{\"addr\":\"%lx\",\"avg_fps\":%.1f,\"failures\":%u,\"transport\":\"%s\",\"wait_p50\":%.1f,\"import_p50\":%.2f,\"import_p99\":%.2f}",
            i?",":"",(unsigned long)w.address,w.totalFrames/elapsed,w.totalFailures,w.transport.c_str(),pct(w.allWait,.5),pct(w.allImport,.5),pct(w.allImport,.99));
    }
    printf("],\"unique_fps\":{");
    bool first=true;
    for(auto& w:windows)for(auto t:w->stamps)unique[w->address].insert(t);
    for(auto& [a,set]:unique){printf("%s\"%lx\":%.1f",first?"":",",(unsigned long)a,set.size()/elapsed);first=false;}
    printf("}}\n");
    for(auto& w:windows)w->finish();
    wl_display_flush(g.display);
    return 0;
}
