#include "gpu_capture.hpp"
#include "window_capture.hpp"
#include "pixels.hpp"
#include "hyprland-toplevel-export-client.h"
#include <wayland-client.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
constexpr unsigned thumbnailEdge=256;
constexpr double requestTimeout=3;   // seconds before a request without a result counts as failed
// Hyprland 0.56 creates no frame object for an unknown handle and sends nothing; a request on that id
// is a protocol error that would close the shared connection. A frame without events once a later
// wl_display.sync returns is such an orphan: its proxy goes without a destroy request.
constexpr int deadAfter=3;
constexpr double pumpSpacing=.00025; // update()/service() of many windows share one dispatch
bool supportedShm(uint32_t f){ return f==WL_SHM_FORMAT_XRGB8888 || f==WL_SHM_FORMAT_ARGB8888 || f==WL_SHM_FORMAT_XBGR8888 || f==WL_SHM_FORMAT_ABGR8888; }
}

struct WindowCaptureHub::Impl {
    wl_display* display=nullptr;
    wl_registry* registry=nullptr;
    wl_shm* shm=nullptr;
    zwp_linux_dmabuf_v1* dmabuf=nullptr;
    hyprland_toplevel_export_manager_v1* manager=nullptr;
    unsigned version=0;
    int deviceFd=-1;
    gbm_device* gbm=nullptr;
    bool deviceTried=false;
    std::string failure;
    const Clock::time_point start=Clock::now();
    double lastPump=-1;
    std::vector<WindowCapture::Impl*> windows;
    // Syncs sent after request batches and the newest one answered; the server has seen every
    // request made before sync `synced`.
    struct Sync { Impl* hub; unsigned serial; wl_callback* callback; };
    unsigned sent=0,synced=0;
    std::vector<std::unique_ptr<Sync>> syncs;
    ~Impl();
    static void syncDone(void* data,wl_callback*,uint32_t){
        auto* done=static_cast<Sync*>(data);
        auto& self=*done->hub;
        self.synced=std::max(self.synced,done->serial);
        wl_callback_destroy(done->callback);
        std::erase_if(self.syncs,[&](const auto& sync){ return sync.get()==done; });
    }
    static constexpr wl_callback_listener syncListener{syncDone};
    void sync(){
        auto next=std::make_unique<Sync>(Sync{this,++sent,wl_display_sync(display)});
        wl_callback_add_listener(next->callback,&syncListener,next.get());
        syncs.push_back(std::move(next));
    }
    double now() const { return std::chrono::duration<double>(Clock::now()-start).count(); }
    static void dmaFormat(void*,zwp_linux_dmabuf_v1*,uint32_t){}
    static void dmaModifier(void*,zwp_linux_dmabuf_v1*,uint32_t,uint32_t,uint32_t){}
    static constexpr zwp_linux_dmabuf_v1_listener dmaListener{dmaFormat,dmaModifier};
    static void global(void* data,wl_registry* reg,uint32_t id,const char* iface,uint32_t version){
        auto& self=*static_cast<Impl*>(data);
        if(!std::strcmp(iface,wl_shm_interface.name))
            self.shm=static_cast<wl_shm*>(wl_registry_bind(reg,id,&wl_shm_interface,1));
        else if(!std::strcmp(iface,zwp_linux_dmabuf_v1_interface.name) && version>=3){
            self.dmabuf=static_cast<zwp_linux_dmabuf_v1*>(wl_registry_bind(reg,id,&zwp_linux_dmabuf_v1_interface,3));
            zwp_linux_dmabuf_v1_add_listener(self.dmabuf,&dmaListener,&self);
        }
        else if(!std::strcmp(iface,hyprland_toplevel_export_manager_v1_interface.name)){
            self.version=std::min(version,2u);
            self.manager=static_cast<hyprland_toplevel_export_manager_v1*>(wl_registry_bind(reg,id,&hyprland_toplevel_export_manager_v1_interface,self.version));
        }
    }
    static void removed(void*,wl_registry*,uint32_t){}
    static constexpr wl_registry_listener registryListener{global,removed};
    // The DesktopCapture::Impl::pump rules: never block, flush the copy requests that buffer
    // negotiation queued, and report a lost connection once.
    void pump(){
        if(!display || !failure.empty())return;
        lastPump=now();
        if(wl_display_dispatch_pending(display)<0){failure="Wayland connection lost";return;}
        while(wl_display_prepare_read(display)!=0)
            if(wl_display_dispatch_pending(display)<0){failure="Wayland connection lost";return;}
        const int flushed=wl_display_flush(display);
        if(flushed<0 && errno!=EAGAIN){wl_display_cancel_read(display);failure="Wayland flush failed";return;}
        pollfd descriptor{wl_display_get_fd(display),POLLIN,0};
        const int result=poll(&descriptor,1,0);
        if(result>0 && (descriptor.revents & POLLIN)){
            if(wl_display_read_events(display)<0 || wl_display_dispatch_pending(display)<0)failure="Wayland connection lost";
        }else{
            wl_display_cancel_read(display);
            if((result<0 && errno!=EINTR) || (descriptor.revents & (POLLERR|POLLHUP|POLLNVAL)))failure="Wayland capture socket disconnected";
        }
        if(failure.empty() && wl_display_flush(display)<0 && errno!=EAGAIN)failure="Wayland flush failed";
    }
    void pumpSoon(){ if(now()-lastPump>=pumpSpacing)pump(); }
    void flush(){ if(display && failure.empty() && wl_display_flush(display)<0 && errno!=EAGAIN)failure="Wayland flush failed"; }
    gbm_device* device(){
        if(gbm || deviceTried)return gbm;
        const EGLDisplay egl=eglGetCurrentDisplay();
        if(egl==EGL_NO_DISPLAY)return nullptr;
        deviceTried=true;
        gbm=GpuCapture::openRenderDevice(egl,deviceFd);
        return gbm;
    }
};

struct WindowCapture::Impl {
    struct Lane {
        Impl* owner=nullptr;
        hyprland_toplevel_export_frame_v1* frame=nullptr;
        std::unique_ptr<GpuCapture> gpu;
        wl_buffer* shmBuffer=nullptr;
        void* shmPixels=MAP_FAILED;
        size_t shmSize=0;
        unsigned shmAllocW=0,shmAllocH=0,shmAllocStride=0,shmAllocFormat=0;
        bool ready=false,failed=false,submitted=false,gotBuffer=false,gpuMode=false,pulled=false;
        bool parked=false;    // an idle window's open frame: buffer_done sends no copy
        bool known=false;     // any event: the compositor created the frame object
        unsigned serial=0;    // the hub sync sent after this request
        uint32_t dmaFormat=0,dmaW=0,dmaH=0,shmFormat=0,shmW=0,shmH=0,shmStride=0,flags=0;
        uint64_t stamp=0;
        double requested=0,readyAt=0,last=Cadence::never;   // hub clock
        ~Lane(){ finish(); gpu.reset(); clearShm(); }
        void finish(){
            if(frame && known)hyprland_toplevel_export_frame_v1_destroy(frame);
            else if(frame)wl_proxy_destroy(reinterpret_cast<wl_proxy*>(frame));
            frame=nullptr;
        }
        bool resolved() const { return !frame || known || (owner->hub && owner->hub->synced>=serial); }
        bool orphan() const { return frame && !known && owner->hub && owner->hub->synced>=serial; }
        void clearShm(){
            if(shmBuffer)wl_buffer_destroy(shmBuffer);
            if(shmPixels!=MAP_FAILED)munmap(shmPixels,shmSize);
            shmBuffer=nullptr;shmPixels=MAP_FAILED;shmSize=0;shmAllocW=shmAllocH=shmAllocStride=shmAllocFormat=0;
        }
        bool inverted() const { return flags & HYPRLAND_TOPLEVEL_EXPORT_FRAME_V1_FLAGS_Y_INVERT; }
        bool allocateShm();
        void copyShm(CapturedFrame& frame,unsigned w,unsigned h) const;
        void submit(hyprland_toplevel_export_frame_v1* f);
        static void onBuffer(void* d,hyprland_toplevel_export_frame_v1*,uint32_t fmt,uint32_t w,uint32_t h,uint32_t stride){
            auto& s=*static_cast<Lane*>(d);s.known=s.gotBuffer=true;s.shmFormat=fmt;s.shmW=w;s.shmH=h;s.shmStride=stride;}
        static void onDamage(void* d,hyprland_toplevel_export_frame_v1*,uint32_t,uint32_t,uint32_t,uint32_t){static_cast<Lane*>(d)->known=true;}
        static void onFlags(void* d,hyprland_toplevel_export_frame_v1*,uint32_t f){auto& s=*static_cast<Lane*>(d);s.known=true;s.flags=f;}
        static void onReady(void* d,hyprland_toplevel_export_frame_v1*,uint32_t hi,uint32_t lo,uint32_t ns){
            auto& s=*static_cast<Lane*>(d);s.known=s.ready=true;s.readyAt=s.owner->clock();
            s.stamp=((uint64_t(hi)<<32|lo)*1000000000ull)+ns;}
        static void onFailed(void* d,hyprland_toplevel_export_frame_v1*){auto& s=*static_cast<Lane*>(d);s.known=s.failed=true;}
        static void onDma(void* d,hyprland_toplevel_export_frame_v1*,uint32_t fmt,uint32_t w,uint32_t h){
            auto& s=*static_cast<Lane*>(d);s.known=s.gotBuffer=true;s.dmaFormat=fmt;s.dmaW=w;s.dmaH=h;}
        static void onBufferDone(void* d,hyprland_toplevel_export_frame_v1* f){auto& s=*static_cast<Lane*>(d);s.known=true;s.submit(f);}
        static constexpr hyprland_toplevel_export_frame_v1_listener listener{onBuffer,onDamage,onFlags,onReady,onFailed,onDma,onBufferDone};
        static void release(void* d,wl_buffer* b){auto& s=*static_cast<Lane*>(d);if(s.gpu)s.gpu->noteRelease(b);}
        static constexpr wl_buffer_listener bufferListener{release};
    };
    WindowCaptureHub::Impl* hub=nullptr;
    std::uint64_t address=0;
    Cadence cadence;
    std::vector<std::unique_ptr<Lane>> lanes;   // stable addresses: the listeners hold them
    unsigned activeLanes=1;
    bool visible=true,ignoreDamage=false,idlePending=false,rebake=false;
    bool shmOnly=std::getenv("OMARCHY_XR_SHM_CAPTURE")!=nullptr;
    unsigned desiredWidth=16384,desiredHeight=16384;
    int shownLane=-1;
    std::uint64_t lastStamp=0;
    unsigned frames=0,requestCount=0,failures=0;
    int immediateFailures=0,retryMs=0;
    double retryAt=0,importMs=-1,requestToReadyMs=-1;
    const char* transport="none";
    std::string failure;
    ~Impl(){
        // Unanswered frames are resolved first: an orphan must not see a destroy request.
        if(hub && hub->failure.empty() && std::any_of(lanes.begin(),lanes.end(),[](const auto& l){ return !l->resolved(); }))
            if(wl_display_roundtrip(hub->display)<0)hub->failure="Wayland connection lost";
        lanes.clear();
        if(hub)std::erase(hub->windows,this);
    }
    double clock() const { return hub ? hub->now() : 0; }
    void detach(){ lanes.clear(); shownLane=-1; hub=nullptr; failure="Window capture connection closed"; }
    Lane& lane(unsigned index){
        while(lanes.size()<=index){ lanes.push_back(std::make_unique<Lane>()); lanes.back()->owner=this; }
        return *lanes[index];
    }
    void request(Lane& l,double slot,double now);
    void fail(Lane& l,double now,bool immediate);
    std::pair<unsigned,unsigned> fit(unsigned w,unsigned h,unsigned maxW,unsigned maxH) const;
    void import(Lane& l,int index,CapturedFrame& frame);
    bool collect(CapturedFrame& frame,double now);
    void issue(double now);
    bool rebakeShown(CapturedFrame& frame,unsigned maxW,unsigned maxH);
    bool goIdle(CapturedFrame& frame);
    void keepParked(double now);
    void unpark();
    void prune();
};

bool WindowCapture::Impl::Lane::allocateShm(){
    if(shmBuffer && shmAllocW==shmW && shmAllocH==shmH && shmAllocStride==shmStride && shmAllocFormat==shmFormat)return true;
    clearShm();
    if(!owner->hub || !owner->hub->shm || !supportedShm(shmFormat) || !shmW || !shmH || shmW>16384 || shmH>16384 ||
       shmStride<shmW*4 || shmStride%4 || uint64_t(shmStride)*shmH>INT_MAX)return false;
    const size_t size=size_t(shmStride)*shmH;
    const int fd=memfd_create("omarchy-xr-window",MFD_CLOEXEC);
    if(fd<0)return false;
    if(ftruncate(fd,off_t(size))!=0){close(fd);return false;}
    shmPixels=mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(shmPixels==MAP_FAILED){close(fd);return false;}
    auto pool=wl_shm_create_pool(owner->hub->shm,fd,int(size));
    shmBuffer=wl_shm_pool_create_buffer(pool,0,int(shmW),int(shmH),int(shmStride),shmFormat);
    wl_shm_pool_destroy(pool);close(fd);
    shmSize=size;shmAllocW=shmW;shmAllocH=shmH;shmAllocStride=shmStride;shmAllocFormat=shmFormat;
    return true;
}

// The SHM image, scaled to w x h, into the frame's RGBA pixels.
void WindowCapture::Impl::Lane::copyShm(CapturedFrame& frame,unsigned w,unsigned h) const {
    frame.sourceWidth=shmW;frame.sourceHeight=shmH;frame.width=w;frame.height=h;
    frame.texture=0;frame.rgba.resize(size_t(w)*h*4);
    const bool bgr=shmFormat==WL_SHM_FORMAT_XBGR8888 || shmFormat==WL_SHM_FORMAT_ABGR8888;
    copyRgbaScaled(shmPixels,frame.rgba.data(),shmW,shmH,shmStride,bgr,inverted(),w,h);
}

// buffer_done: copy into a GPU slot on the shared device, or SHM when DMA-BUF is unavailable.
void WindowCapture::Impl::Lane::submit(hyprland_toplevel_export_frame_v1* f){
    auto& o=*owner;
    if(parked)return;
    gpuMode=false;
    if(!o.shmOnly && dmaW && o.hub && o.hub->dmabuf){
        if(!gpu)gpu=std::make_unique<GpuCapture>(o.hub->device());
        if(gpu->allocate(o.hub->dmabuf,dmaW,dmaH,dmaFormat)){
            gpuMode=true;
            if(!wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(gpu->buffer())))wl_buffer_add_listener(gpu->buffer(),&bufferListener,this);
            gpu->markBusy();
        }else if(gpu->createFailed){o.shmOnly=true;gpu->clearSource();}
    }
    if(!gpuMode && !allocateShm()){failed=true;return;}
    pulled=o.ignoreDamage;
    hyprland_toplevel_export_frame_v1_copy(f,gpuMode?gpu->buffer():shmBuffer,pulled?1:0);
    submitted=true;
}

void WindowCapture::Impl::request(Lane& l,double slot,double now){
    l.frame=hyprland_toplevel_export_manager_v1_capture_toplevel(hub->manager,0,uint32_t(address&0xffffffffu));
    hyprland_toplevel_export_frame_v1_add_listener(l.frame,&Lane::listener,&l);
    l.ready=l.failed=l.submitted=l.gotBuffer=l.known=l.parked=false;l.dmaW=l.dmaH=l.flags=0;
    l.serial=hub->sent+1;l.requested=now;l.last=slot;++requestCount;
}

// A failed or expired request backs off 0.5-5 s; three immediate failures in a row mean the window is gone.
void WindowCapture::Impl::fail(Lane& l,double now,bool immediate){
    l.finish();++failures;
    immediateFailures=immediate ? immediateFailures+1 : 0;
    retryMs=FrameSource::nextRetryMs(retryMs);retryAt=now+retryMs/1000.;
}

std::pair<unsigned,unsigned> WindowCapture::Impl::fit(unsigned w,unsigned h,unsigned maxW,unsigned maxH) const {
    const float ratio=std::min({1.f,float(maxW)/std::max(1u,w),float(maxH)/std::max(1u,h)});
    return {std::max(1u,unsigned(std::ceil(w*ratio))),std::max(1u,unsigned(std::ceil(h*ratio)))};
}

void WindowCapture::Impl::import(Lane& l,int index,CapturedFrame& frame){
    const unsigned nativeW=l.gpuMode?l.gpu->capturedWidth():l.shmW,nativeH=l.gpuMode?l.gpu->capturedHeight():l.shmH;
    const auto [w,h]=fit(nativeW,nativeH,desiredWidth,desiredHeight);
    if(l.gpuMode){
        frame.sourceWidth=nativeW;frame.sourceHeight=nativeH;frame.width=w;frame.height=h;
        frame.texture=l.gpu->present(w,h,l.inverted());
        frame.rgba.clear();
    }else l.copyShm(frame,w,h);
    transport=l.gpuMode?"dmabuf":"shm";
    shownLane=index;lastStamp=l.stamp;++frames;rebake=false;
    const double done=clock();
    importMs=(done-l.readyAt)*1000;requestToReadyMs=(l.readyAt-l.requested)*1000;
}

// Import only the newest ready frame, and only when its stamp is newer than the shown one: two lanes
// can return the same commit, or finish together after a stall with the older one last. Retire failures.
bool WindowCapture::Impl::collect(CapturedFrame& frame,double now){
    int newest=-1;
    for(unsigned i=0;i<lanes.size();++i)
        if(lanes[i]->frame && lanes[i]->ready && (newest<0 || lanes[i]->stamp>lanes[size_t(newest)]->stamp))newest=int(i);
    const bool updated=visible && newest>=0 && lanes[size_t(newest)]->stamp>lastStamp;
    if(updated)import(*lanes[size_t(newest)],newest,frame);
    for(unsigned i=0;i<lanes.size();++i){
        auto& l=*lanes[i];
        if(!l.frame)continue;
        if(l.ready){
            if(!(updated && int(i)==newest) && l.gpuMode && l.gpu)l.gpu->captureSlot=-1;
            l.finish();immediateFailures=0;retryMs=0;
        }else if(l.orphan() || l.failed)fail(l,now,!l.gotBuffer);
        // A copy that waits for damage may wait for good on a still window; that is not a failure.
        else if(l.known && now-l.requested>requestTimeout && (!l.submitted || l.pulled))fail(l,now,false);
    }
    return updated;
}

// Each idle lane whose slot is due asks at once, so a request is always outstanding while visible.
void WindowCapture::Impl::issue(double now){
    if(!visible || !hub || !hub->manager || now<retryAt || immediateFailures>=deadAfter)return;
    bool sent=false;
    for(unsigned i=0;i<activeLanes;++i){
        auto& l=lane(i);
        if(l.frame)continue;
        const double slot=cadence.due(i,now,l.last);
        if(slot>now)continue;
        request(l,slot,now);sent=true;
    }
    if(sent){hub->sync();hub->flush();}
}

// The retained native image at a new size, without waiting for the next capture.
bool WindowCapture::Impl::rebakeShown(CapturedFrame& frame,unsigned maxW,unsigned maxH){
    if(shownLane<0 || size_t(shownLane)>=lanes.size())return false;
    auto& l=*lanes[size_t(shownLane)];
    if(l.gpu && l.gpu->retained()){
        const auto [w,h]=fit(l.gpu->width(),l.gpu->height(),maxW,maxH);
        frame.sourceWidth=l.gpu->width();frame.sourceHeight=l.gpu->height();frame.width=w;frame.height=h;
        frame.texture=l.gpu->present(w,h,l.gpu->invertY,true);frame.rgba.clear();
        return frame.texture!=0;
    }
    // The lane's next copy reuses its SHM buffer: while one is submitted the compositor may be
    // writing it, so the next frame arrives at the new size instead.
    if(l.shmPixels==MAP_FAILED || !l.shmW || (l.frame && l.submitted))return false;
    const auto [w,h]=fit(l.shmW,l.shmH,maxW,maxH);
    l.copyShm(frame,w,h);
    return true;
}

// Once per idle transition: cancel requests, keep a <= 256 px thumbnail, release full-size slots.
bool WindowCapture::Impl::goIdle(CapturedFrame& frame){
    if(!std::all_of(lanes.begin(),lanes.end(),[](const auto& l){ return l->resolved(); }))return false;
    idlePending=false;
    for(auto& l:lanes){l->finish();if(l->gpu)l->gpu->captureSlot=-1;}
    const bool baked=rebakeShown(frame,thumbnailEdge,thumbnailEdge);
    for(unsigned i=0;i<lanes.size();++i){
        auto& l=*lanes[i];
        if(l.gpu){ if(int(i)==shownLane)l.gpu->clearSource(); else l.gpu->clear(); }
        l.clearShm();
    }
    return baked;
}

// Idle keeps one frame open without copy, so the export session stays alive (§3.2: Hyprland ends a
// session 500 ms after its last frame, and the recording indicator would flicker). A window that is
// gone still fails it, which counts towards alive().
void WindowCapture::Impl::keepParked(double now){
    auto& l=lane(0);
    if(l.frame && (l.orphan() || l.failed))fail(l,now,!l.gotBuffer);
    if(l.frame || !hub->manager || now<retryAt || immediateFailures>=deadAfter)return;
    request(l,l.last,now);l.parked=true;
    hub->sync();hub->flush();
}

// Visible again: the parked frame goes in the same dispatch as the first new request.
void WindowCapture::Impl::unpark(){
    for(auto& l:lanes)if(l->parked){l->finish();l->parked=false;}
}

// Lanes beyond the rate's in-flight count leave once idle and no longer on screen.
void WindowCapture::Impl::prune(){
    while(lanes.size()>activeLanes && !lanes.back()->frame && int(lanes.size())-1!=shownLane)lanes.pop_back();
}

WindowCaptureHub::Impl::~Impl(){
    for(auto* w:std::vector<WindowCapture::Impl*>(windows))w->detach();
    for(auto& pending:syncs)wl_callback_destroy(pending->callback);
    if(manager)hyprland_toplevel_export_manager_v1_destroy(manager);
    if(dmabuf)zwp_linux_dmabuf_v1_destroy(dmabuf);
    if(shm)wl_shm_destroy(shm);
    if(registry)wl_registry_destroy(registry);
    if(gbm)gbm_device_destroy(gbm);
    if(deviceFd>=0)close(deviceFd);
    if(display)wl_display_disconnect(display);
}

WindowCaptureHub::WindowCaptureHub():impl(std::make_unique<Impl>()){}
WindowCaptureHub::~WindowCaptureHub()=default;
bool WindowCaptureHub::connect(std::string& error){
    auto& s=*impl;
    s.display=wl_display_connect(nullptr);
    if(!s.display){error=s.failure="Cannot connect to Wayland; run inside your Hyprland session";return false;}
    s.registry=wl_display_get_registry(s.display);
    wl_registry_add_listener(s.registry,&Impl::registryListener,&s);
    if(wl_display_roundtrip(s.display)<0 || wl_display_roundtrip(s.display)<0){error=s.failure="Wayland window discovery failed";return false;}
    if(!s.manager || s.version<2){error=s.failure="Window canvas needs Hyprland with hyprland_toplevel_export_manager_v1 v2";return false;}
    if(!s.shm && !s.dmabuf){error=s.failure="Compositor lacks shared-memory and DMA-BUF capture buffers";return false;}
    return true;
}
void WindowCaptureHub::pump(){impl->pump();}
void WindowCaptureHub::settle(double maxSeconds){
    auto& s=*impl;
    const double deadline=s.now()+maxSeconds;
    s.pump();
    while(s.display && s.failure.empty() && s.synced<s.sent){
        const double left=deadline-s.now();
        if(left<=0)break;
        pollfd descriptor{wl_display_get_fd(s.display),POLLIN,0};
        if(poll(&descriptor,1,std::max(1,int(left*1000)))<=0)break;
        s.pump();
    }
}
gbm_device* WindowCaptureHub::device(){return impl->device();}
const std::string& WindowCaptureHub::error() const{return impl->failure;}
unsigned WindowCaptureHub::version() const{return impl->version;}
double WindowCaptureHub::now() const{return impl->now();}
std::unique_ptr<WindowCapture> WindowCaptureHub::open(std::uint64_t address){
    if(!impl->manager || !impl->failure.empty())return nullptr;
    auto state=std::make_unique<WindowCapture::Impl>();
    state->hub=impl.get();state->address=address;
    impl->windows.push_back(state.get());
    return std::unique_ptr<WindowCapture>(new WindowCapture(std::move(state)));
}

WindowCapture::WindowCapture(std::unique_ptr<Impl> state):impl(std::move(state)){}
WindowCapture::~WindowCapture()=default;
bool WindowCapture::update(CapturedFrame& frame){
    auto& s=*impl;
    if(!s.hub || !s.hub->failure.empty())return false;
    s.hub->pumpSoon();
    if(!s.hub->failure.empty())return false;
    const double now=s.hub->now();
    if(!s.visible){
        if(s.idlePending)return s.goIdle(frame);
        s.keepParked(now);return false;
    }
    s.unpark();
    bool updated=s.collect(frame,now);
    if(!updated && s.rebake){s.rebake=false;updated=s.rebakeShown(frame,s.desiredWidth,s.desiredHeight);}
    s.issue(now);
    s.prune();
    return updated;
}
void WindowCapture::service(){if(impl->hub)impl->hub->pumpSoon();}
void WindowCapture::setDemand(bool visible,unsigned width,unsigned height){
    auto& s=*impl;
    width=std::max(1u,width);height=std::max(1u,height);
    if(visible && (!s.visible || width!=s.desiredWidth || height!=s.desiredHeight) && s.shownLane>=0)s.rebake=true;
    if(!visible && s.visible)s.idlePending=true;
    if(visible)s.idlePending=false;
    s.visible=visible;s.desiredWidth=width;s.desiredHeight=height;
}
void WindowCapture::setFrameRate(unsigned fps,unsigned inFlight,double phase){
    impl->cadence.retune(fps,inFlight,phase);
    impl->activeLanes=impl->cadence.lanes;
}
void WindowCapture::setIgnoreDamage(bool enabled){impl->ignoreDamage=enabled;}
const char* WindowCapture::transport() const{return impl->transport;}
unsigned WindowCapture::requests() const{return impl->requestCount;}
double WindowCapture::importLatencyMs() const{return impl->importMs;}
double WindowCapture::requestToReadyMs() const{return impl->requestToReadyMs;}
const std::string& WindowCapture::error() const{return impl->hub && !impl->hub->failure.empty() ? impl->hub->failure : impl->failure;}
bool WindowCapture::alive() const{return impl->immediateFailures<deadAfter;}
double WindowCapture::phase() const{return impl->cadence.phase;}
unsigned WindowCapture::lanes() const{return impl->activeLanes;}
unsigned WindowCapture::frames() const{return impl->frames;}
unsigned WindowCapture::failures() const{return impl->failures;}
