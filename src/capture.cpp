#include "gpu_capture.hpp"
#include "capture.hpp"
#include <cstdlib>
#include "pixels.hpp"
#include "wlr-screencopy-client.h"
#include <wayland-client.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

struct DesktopCapture::Impl {
    struct Output { Impl* owner; uint32_t id; wl_output* proxy; std::string name; };
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_shm* shm = nullptr;
    zwp_linux_dmabuf_v1* dmabuf=nullptr;
    std::unique_ptr<GpuCapture> gpu;
    bool gpuMode=false,gpuDisabled=std::getenv("OMARCHY_XR_SHM_CAPTURE")!=nullptr,retry=false;
    bool visible=true,submitted=false,released=true,forceCopy=true,includeCursor=true;
    unsigned desiredWidth=16384,desiredHeight=16384,requestCount=0,version=1;
    unsigned allocatedWidth=0,allocatedHeight=0,allocatedStride=0,allocatedFormat=0;
    unsigned dmaFormat=0,dmaWidth=0,dmaHeight=0;
    zwlr_screencopy_manager_v1* manager = nullptr;
    zwlr_screencopy_frame_v1* pending = nullptr;
    wl_buffer* buffer = nullptr;
    void* pixels = MAP_FAILED;
    size_t size = 0;
    uint32_t format = 0, width = 0, height = 0, stride = 0, flags = 0;
    std::vector<std::unique_ptr<Output>> outputs;
    Output* selected = nullptr;
    bool ready = false, rebake = false;
    double interval = 1000./60;
    double importMs = -1;
    std::string failure;
    using Clock = std::chrono::steady_clock;
    Clock::time_point requested{}, next{}, readyAt{};

    void clearFrame() {
        if(pending)zwlr_screencopy_frame_v1_destroy(pending);
        pending=nullptr;flags=0;ready=false;submitted=false;dmaWidth=dmaHeight=0;
    }
    void clearBuffer(){
        if(buffer)wl_buffer_destroy(buffer);
        if(pixels!=MAP_FAILED)munmap(pixels,size);
        buffer=nullptr;pixels=MAP_FAILED;size=0;
    }
    ~Impl() {
        clearFrame();clearBuffer();gpu.reset();
        if(dmabuf)zwp_linux_dmabuf_v1_destroy(dmabuf);
        for (auto& out : outputs) wl_output_release(out->proxy);
        if (manager) zwlr_screencopy_manager_v1_destroy(manager);
        if (shm) wl_shm_destroy(shm);
        if (registry) wl_registry_destroy(registry);
        if (display) wl_display_disconnect(display);
    }
    static void geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t,
                         int32_t, const char*, const char*, int32_t) {}
    static void mode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
    static void done(void*, wl_output*) {}
    static void scale(void*, wl_output*, int32_t) {}
    static void name(void* data, wl_output*, const char* value) {
        static_cast<Output*>(data)->name = value;
    }
    static void description(void*, wl_output*, const char*) {}
    static constexpr wl_output_listener outputListener{geometry, mode, done, scale, name, description};
    static void dmaFormatEvent(void*,zwp_linux_dmabuf_v1*,uint32_t){}
    static void dmaModifierEvent(void*,zwp_linux_dmabuf_v1*,uint32_t,uint32_t,uint32_t){}
    static constexpr zwp_linux_dmabuf_v1_listener dmaListener{dmaFormatEvent,dmaModifierEvent};
    static void global(void* data, wl_registry* reg, uint32_t id, const char* iface, uint32_t version) {
        auto& self = *static_cast<Impl*>(data);
        if (std::strcmp(iface, wl_shm_interface.name) == 0)
            self.shm = static_cast<wl_shm*>(wl_registry_bind(reg, id, &wl_shm_interface, 1));
        else if (std::strcmp(iface, zwp_linux_dmabuf_v1_interface.name)==0 && version>=3){
            self.dmabuf=static_cast<zwp_linux_dmabuf_v1*>(wl_registry_bind(reg,id,&zwp_linux_dmabuf_v1_interface,3));
            zwp_linux_dmabuf_v1_add_listener(self.dmabuf,&dmaListener,&self);
        }
        else if (std::strcmp(iface, zwlr_screencopy_manager_v1_interface.name) == 0) {
            self.version=std::min(version,3u);
            self.manager = static_cast<zwlr_screencopy_manager_v1*>(wl_registry_bind(reg,id,&zwlr_screencopy_manager_v1_interface,self.version));
        }
        else if (std::strcmp(iface, wl_output_interface.name) == 0 && version >= 4) {
            auto out = std::make_unique<Output>();
            out->owner = &self; out->id = id;
            out->proxy = static_cast<wl_output*>(wl_registry_bind(reg, id, &wl_output_interface, 4));
            wl_output_add_listener(out->proxy, &outputListener, out.get());
            self.outputs.push_back(std::move(out));
        }
    }
    static void removed(void* data, wl_registry*, uint32_t id) {
        auto& self = *static_cast<Impl*>(data);
        for (auto it = self.outputs.begin(); it != self.outputs.end(); ++it) {
            if ((*it)->id != id) continue;
            if (self.selected == it->get()) {
                self.failure = "Captured output was disconnected";
                self.selected = nullptr;
            }
            wl_output_release((*it)->proxy);
            self.outputs.erase(it);
            break;
        }
    }
    static constexpr wl_registry_listener registryListener{global, removed};
    static void onBuffer(void* data, zwlr_screencopy_frame_v1* frame, uint32_t fmt,
                         uint32_t w, uint32_t h, uint32_t row) {
        auto& self = *static_cast<Impl*>(data);
        if (fmt != WL_SHM_FORMAT_XRGB8888 && fmt != WL_SHM_FORMAT_ARGB8888 &&
            fmt != WL_SHM_FORMAT_XBGR8888 && fmt != WL_SHM_FORMAT_ABGR8888) {
            self.failure = "Unsupported capture pixel format: " + std::to_string(fmt);
            return;
        }
        if (!w || !h || w > 16384 || h > 16384 || row < w * 4 || row % 4 ||
            uint64_t(row) * h > INT_MAX) {
            self.failure = "Invalid or excessively large capture buffer"; return;
        }
        self.format = fmt; self.width = w; self.height = h; self.stride = row;
        if(self.version<3)self.submit(frame);
    }
    static void release(void* data,wl_buffer* buffer){
        auto& self=*static_cast<Impl*>(data);
        self.released=true;
        if(self.gpu)self.gpu->noteRelease(buffer);
    }
    static constexpr wl_buffer_listener bufferListener{release};
    bool allocateShm(){
        const auto w=width,h=height,row=stride,fmt=format;
        if(buffer && allocatedWidth==w && allocatedHeight==h && allocatedStride==row && allocatedFormat==fmt)return true;
        clearBuffer();
        size = size_t(row) * h;
        const int fd = memfd_create("omarchy-xr-capture", MFD_CLOEXEC);
        if (fd < 0) { failure = "Cannot create capture shared memory"; return false; }
        if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
            close(fd); failure = "Cannot size capture shared memory"; return false;
        }
        pixels = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (pixels == MAP_FAILED) {
            close(fd); failure = "Cannot map capture shared memory"; return false;
        }
        wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int>(size));
        buffer = wl_shm_pool_create_buffer(pool, 0, static_cast<int>(w),
            static_cast<int>(h), static_cast<int>(row), fmt);
        wl_shm_pool_destroy(pool);
        close(fd);
        allocatedWidth=w;allocatedHeight=h;allocatedStride=row;allocatedFormat=fmt;
        wl_buffer_add_listener(buffer,&bufferListener,this);return true;
    }
    void submit(zwlr_screencopy_frame_v1* frame){
        if(!failure.empty())return;
        gpuMode=false;
        if(!gpuDisabled && dmaWidth && dmabuf){
            if(!gpu)gpu=std::make_unique<GpuCapture>();
            if(gpu->allocate(dmabuf,dmaWidth,dmaHeight,dmaFormat)){
                gpuMode=true;
                if(!wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(gpu->buffer())))wl_buffer_add_listener(gpu->buffer(),&bufferListener,this);
                gpu->markBusy();
            }else if(gpu->createFailed){gpuDisabled=true;gpu->clearSource();}
        }
        if(!gpuMode && !allocateShm())return;
        auto destination=gpuMode?gpu->buffer():buffer;
        if(version>=2 && !forceCopy)zwlr_screencopy_frame_v1_copy_with_damage(frame,destination);
        else zwlr_screencopy_frame_v1_copy(frame,destination);
        submitted=true;released=false;forceCopy=false;
    }
    static void onDma(void* data,zwlr_screencopy_frame_v1*,uint32_t format,uint32_t w,uint32_t h){
        auto& s=*static_cast<Impl*>(data);s.dmaFormat=format;s.dmaWidth=w;s.dmaHeight=h;
    }
    static void onBufferDone(void* data,zwlr_screencopy_frame_v1* frame){static_cast<Impl*>(data)->submit(frame);}
    static void onDamage(void*,zwlr_screencopy_frame_v1*,uint32_t,uint32_t,uint32_t,uint32_t){}
    static void onFlags(void* data, zwlr_screencopy_frame_v1*, uint32_t value) {
        static_cast<Impl*>(data)->flags = value;
    }
    static void onReady(void* data, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t) {
        auto& self=*static_cast<Impl*>(data);
        self.ready = true;
        self.readyAt = Clock::now();
    }
    static void onFailed(void* data, zwlr_screencopy_frame_v1*) {
        auto& s=*static_cast<Impl*>(data);
        if(s.gpuMode){s.gpuDisabled=true;s.retry=true;s.released=true;}
        else s.failure="Compositor rejected capture (output unavailable or capture blocked)";
    }
    // Version 3 negotiates GPU and SHM buffers; older compositors use SHM.
    static constexpr zwlr_screencopy_frame_v1_listener frameListener{
        onBuffer, onFlags, onReady, onFailed, onDamage, onDma, onBufferDone};

    void pump() {
        if (wl_display_dispatch_pending(display) < 0) { failure = "Wayland connection lost"; return; }
        while (wl_display_prepare_read(display) != 0) {
            if (wl_display_dispatch_pending(display) < 0) { failure = "Wayland connection lost"; return; }
        }
        const int flushed = wl_display_flush(display);
        if (flushed < 0 && errno != EAGAIN) {
            wl_display_cancel_read(display); failure = "Wayland flush failed"; return;
        }
        pollfd descriptor{wl_display_get_fd(display), POLLIN, 0};
        const int result = poll(&descriptor, 1, 0);
        if (result > 0 && (descriptor.revents & POLLIN)) {
            if (wl_display_read_events(display) < 0 || wl_display_dispatch_pending(display) < 0)
                failure = "Wayland connection lost";
        } else {
            wl_display_cancel_read(display);
            if ((result < 0 && errno != EINTR) || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)))
                failure = "Wayland capture socket disconnected";
        }
        // Buffer negotiation callbacks enqueue copy requests. Flush immediately,
        // rather than holding them until the next rendered frame.
        if(wl_display_flush(display)<0 && errno!=EAGAIN)failure="Wayland flush failed";

    }
};

DesktopCapture::DesktopCapture() : impl(std::make_unique<Impl>()) {}
DesktopCapture::~DesktopCapture() = default;
void DesktopCapture::service(){if(impl->display && impl->failure.empty())impl->pump();}
void DesktopCapture::setIncludeCursor(bool enabled){impl->includeCursor=enabled;}
void DesktopCapture::setFrameRate(unsigned fps) { impl->interval = 1000. / std::clamp(fps, 1u, 120u); }
void DesktopCapture::setDemand(bool visible,unsigned width,unsigned height){
    width=std::max(1u,width);height=std::max(1u,height);
    // The native GPU image does not depend on the requested size. Re-blit it
    // immediately and let the next capture replace it when the compositor answers.
    if(visible && (!impl->visible || width!=impl->desiredWidth || height!=impl->desiredHeight) && impl->gpu && impl->gpu->retained())
        impl->rebake=true;
    impl->visible=visible;impl->desiredWidth=width;impl->desiredHeight=height;
}
const char* DesktopCapture::transport() const{return impl->gpuMode?"dmabuf":"shm";}
double DesktopCapture::importLatencyMs() const{return impl->importMs;}
unsigned DesktopCapture::requests() const{return impl->requestCount;}
const std::string& DesktopCapture::error() const { return impl->failure; }
bool DesktopCapture::connect() {
    auto& s = *impl;
    s.display = wl_display_connect(nullptr);
    if (!s.display) { s.failure = "Cannot connect to Wayland; run inside your Hyprland session"; return false; }
    s.registry = wl_display_get_registry(s.display);
    wl_registry_add_listener(s.registry, &Impl::registryListener, &s);
    if (wl_display_roundtrip(s.display) < 0 || wl_display_roundtrip(s.display) < 0) {
        s.failure = "Wayland output discovery failed"; return false;
    }
    if (!s.manager || !s.shm) {
        s.failure = "Compositor lacks wlr-screencopy/shared-memory capture support"; return false;
    }
    return true;
}
std::vector<std::string> DesktopCapture::outputs() const {
    std::vector<std::string> names;
    for (const auto& out : impl->outputs) if (!out->name.empty()) names.push_back(out->name);
    return names;
}
bool DesktopCapture::select(const std::string& name) {
    for (auto& out : impl->outputs) if (out->name == name) { impl->selected = out.get(); return true; }
    impl->failure = "Unknown output '" + name + "'; use --list-outputs";
    return false;
}
bool DesktopCapture::update(CapturedFrame& frame) {
    auto& s = *impl;
    if (!s.failure.empty() || !s.selected) return false;
    s.pump();
    if (!s.failure.empty()) return false;
    const auto now = Impl::Clock::now();
    if(s.retry){s.clearFrame();s.gpu->clearSource();s.gpuMode=false;s.retry=false;s.forceCopy=true;}
    if (s.pending && !s.ready && (!s.submitted || s.version<2) && now - s.requested > std::chrono::seconds(3)) {
        s.failure = "Capture timed out after three seconds"; return false;
    }
    bool updated = false;
    if (s.ready) {
        if(s.visible){
            const unsigned nativeWidth=s.gpuMode?s.gpu->capturedWidth():s.width,nativeHeight=s.gpuMode?s.gpu->capturedHeight():s.height;
            const float ratio=std::min({1.f,float(s.desiredWidth)/nativeWidth,float(s.desiredHeight)/nativeHeight});
            frame.sourceWidth=nativeWidth;frame.sourceHeight=nativeHeight;
            frame.width=std::max(1u,unsigned(std::ceil(nativeWidth*ratio)));
            frame.height=std::max(1u,unsigned(std::ceil(nativeHeight*ratio)));
            if(s.gpuMode){
                frame.texture=s.gpu->present(frame.width,frame.height,s.flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT);
                frame.rgba.clear();s.rebake=false;
            }else{
                frame.texture=0;frame.rgba.resize(size_t(frame.width)*frame.height*4);
                const bool bgr=s.format==WL_SHM_FORMAT_XBGR8888 || s.format==WL_SHM_FORMAT_ABGR8888;
                copyRgbaScaled(s.pixels,frame.rgba.data(),s.width,s.height,s.stride,bgr,
                    s.flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT,frame.width,frame.height);
            }
            s.importMs=std::chrono::duration<double,std::milli>(Impl::Clock::now()-s.readyAt).count();
            updated=true;
        }
        s.clearFrame();
    }else if(s.rebake && s.visible && s.gpu && s.gpu->retained()){
        const unsigned nativeWidth=s.gpu->width(),nativeHeight=s.gpu->height();
        const float ratio=std::min({1.f,float(s.desiredWidth)/nativeWidth,float(s.desiredHeight)/nativeHeight});
        frame.sourceWidth=nativeWidth;frame.sourceHeight=nativeHeight;
        frame.width=std::max(1u,unsigned(std::ceil(nativeWidth*ratio)));
        frame.height=std::max(1u,unsigned(std::ceil(nativeHeight*ratio)));
        frame.texture=s.gpu->present(frame.width,frame.height,s.gpu->invertY,true);
        frame.rgba.clear();s.rebake=false;updated=true;
    }
    if (s.visible && !s.pending && s.released && now + std::chrono::milliseconds(1) >= s.next) {
        s.pending=zwlr_screencopy_manager_v1_capture_output(s.manager,s.includeCursor?1:0,s.selected->proxy);
        zwlr_screencopy_frame_v1_add_listener(s.pending,&Impl::frameListener,&s);
        s.requested=now;
        const auto interval=std::chrono::duration_cast<Impl::Clock::duration>(std::chrono::duration<double,std::milli>(s.interval));
        // Preserve cadence across sub-ms vblank jitter; never accumulate a backlog.
        if(s.next==Impl::Clock::time_point{} || now-s.next>interval)s.next=now+interval;
        else s.next+=interval;
        ++s.requestCount;wl_display_flush(s.display);
    }
    return updated;
}
