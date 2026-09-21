#include "direct_output.hpp"
#include "vblank.hpp"
#include "drm-lease-client.h"
#include <wayland-client.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <map>
#include <chrono>
#include <cstdint>

struct DirectOutput::Impl {
    struct Connector {Impl* owner; wp_drm_lease_device_v1* device; wp_drm_lease_connector_v1* proxy;std::string name;uint32_t id=0;bool withdrawn=false;};
    wl_display* display=nullptr;wl_registry* registry=nullptr;
    std::vector<wp_drm_lease_device_v1*> devices;
    std::vector<std::unique_ptr<Connector>> outputs;
    wp_drm_lease_v1* lease=nullptr;int fd=-1;bool ended=false,flipping=false;
    bool haveVblank=false;std::uint64_t lastVblankUs=0;unsigned missed=0;
    uint32_t crtc=0,connector=0;drmModeModeInfo mode{};
    gbm_device* gbm=nullptr;gbm_surface* surface=nullptr;gbm_bo* front=nullptr;
    EGLDisplay egl=EGL_NO_DISPLAY;EGLContext context=EGL_NO_CONTEXT;EGLSurface eglSurface=EGL_NO_SURFACE;
    std::map<gbm_bo*,uint32_t> framebuffers;
    static void name(void*d,wp_drm_lease_connector_v1*,const char*n){static_cast<Connector*>(d)->name=n;}
    static void description(void*,wp_drm_lease_connector_v1*,const char*){}
    static void id(void*d,wp_drm_lease_connector_v1*,uint32_t n){static_cast<Connector*>(d)->id=n;}
    static void doneConnector(void*,wp_drm_lease_connector_v1*){}
    static void withdrawn(void*d,wp_drm_lease_connector_v1*){static_cast<Connector*>(d)->withdrawn=true;}
    static constexpr wp_drm_lease_connector_v1_listener connectorListener{name,description,id,doneConnector,withdrawn};
    static void drmFd(void*,wp_drm_lease_device_v1*,int f){close(f);}
    static void newConnector(void*d,wp_drm_lease_device_v1*v,wp_drm_lease_connector_v1*c){
        auto& self=*static_cast<Impl*>(d);auto out=std::make_unique<Connector>();out->owner=&self;out->device=v;out->proxy=c;
        wp_drm_lease_connector_v1_add_listener(c,&connectorListener,out.get());self.outputs.push_back(std::move(out));
    }
    static void doneDevice(void*,wp_drm_lease_device_v1*){}
    static constexpr wp_drm_lease_device_v1_listener deviceListener{drmFd,newConnector,doneDevice,doneDevice};
    static void global(void*d,wl_registry*r,uint32_t n,const char*iface,uint32_t){
        if(std::strcmp(iface,"wp_drm_lease_device_v1"))return;
        auto& self=*static_cast<Impl*>(d);
        auto* v=static_cast<wp_drm_lease_device_v1*>(wl_registry_bind(r,n,&wp_drm_lease_device_v1_interface,1));
        wp_drm_lease_device_v1_add_listener(v,&deviceListener,d);self.devices.push_back(v);
    }
    static void removed(void*,wl_registry*,uint32_t){}
    static constexpr wl_registry_listener registryListener{global,removed};
    static void leased(void*d,wp_drm_lease_v1*,int f){static_cast<Impl*>(d)->fd=f;}
    static void finished(void*d,wp_drm_lease_v1*){static_cast<Impl*>(d)->ended=true;}
    static constexpr wp_drm_lease_v1_listener leaseListener{leased,finished};
    Impl(){
        display=wl_display_connect(nullptr);if(!display)throw std::runtime_error("Cannot connect to Wayland for DRM lease");
        registry=wl_display_get_registry(display);wl_registry_add_listener(registry,&registryListener,this);
        for(int i=0;i<3;++i) if(wl_display_roundtrip(display)<0) throw std::runtime_error("DRM lease discovery failed");
    }
    ~Impl(){
        if(fd>=0 && crtc)drmModeSetCrtc(fd,crtc,0,0,0,nullptr,0,nullptr);
        if(egl!=EGL_NO_DISPLAY){eglMakeCurrent(egl,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(context!=EGL_NO_CONTEXT)eglDestroyContext(egl,context);if(eglSurface!=EGL_NO_SURFACE)eglDestroySurface(egl,eglSurface);eglTerminate(egl);}
        if(front && surface)gbm_surface_release_buffer(surface,front);
        for(auto [bo,id]:framebuffers){(void)bo;drmModeRmFB(fd,id);}
        if(surface)gbm_surface_destroy(surface);
        if(gbm)gbm_device_destroy(gbm);
        if(fd>=0)close(fd);
        if(lease)wp_drm_lease_v1_destroy(lease);
        for(auto& o:outputs)wp_drm_lease_connector_v1_destroy(o->proxy);
        for(auto* d:devices)wp_drm_lease_device_v1_destroy(d);
        if(registry)wl_registry_destroy(registry);
        if(display){wl_display_flush(display);wl_display_disconnect(display);}
    }
    void open(const std::string& target,bool stereo){
        Connector* selected=nullptr;for(auto& o:outputs)if(o->name==target && !o->withdrawn)selected=o.get();
        if(!selected)throw std::runtime_error("Output is not offered for DRM lease: "+target);
        connector=selected->id;
        auto* request=wp_drm_lease_device_v1_create_lease_request(selected->device);
        wp_drm_lease_request_v1_request_connector(request,selected->proxy);
        lease=wp_drm_lease_request_v1_submit(request);wp_drm_lease_v1_add_listener(lease,&leaseListener,this);
        wl_display_roundtrip(display);
        if(fd<0 || ended)throw std::runtime_error("Compositor refused the display lease");
        auto* res=drmModeGetResources(fd);auto* conn=drmModeGetConnector(fd,connector);
        if(!res || !conn){if(res)drmModeFreeResources(res);if(conn)drmModeFreeConnector(conn);throw std::runtime_error("Cannot inspect leased DRM resources");}
        for(int i=0;i<conn->count_modes;++i){const auto& m=conn->modes[i];if(m.hdisplay==(stereo?3840:1920) && m.vdisplay==1080 && (!mode.hdisplay || m.vrefresh>mode.vrefresh))mode=m;}
        for(int i=0;i<conn->count_encoders && !crtc;++i){auto* enc=drmModeGetEncoder(fd,conn->encoders[i]);if(!enc)continue;for(int j=0;j<res->count_crtcs;++j)if(enc->possible_crtcs&(1u<<j)){crtc=res->crtcs[j];break;}drmModeFreeEncoder(enc);}
        drmModeFreeConnector(conn);drmModeFreeResources(res);
        if(!mode.hdisplay || !crtc)throw std::runtime_error("Requested XR mode or CRTC missing from lease");
        gbm=gbm_create_device(fd);if(!gbm)throw std::runtime_error("Cannot create GBM device");
        surface=gbm_surface_create(gbm,mode.hdisplay,mode.vdisplay,GBM_FORMAT_XRGB8888,GBM_BO_USE_SCANOUT|GBM_BO_USE_RENDERING);
        if(!surface)throw std::runtime_error("Cannot create XR scanout surface");
        egl=eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR,gbm,nullptr);
        if(egl==EGL_NO_DISPLAY || !eglInitialize(egl,nullptr,nullptr) || !eglBindAPI(EGL_OPENGL_API))throw std::runtime_error("Cannot initialize direct OpenGL");
        EGLint attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};
        EGLConfig configs[64],config=nullptr;EGLint count=0;
        eglChooseConfig(egl,attrs,configs,64,&count);
        for(int i=0;i<count;++i){EGLint visual;eglGetConfigAttrib(egl,configs[i],EGL_NATIVE_VISUAL_ID,&visual);if(visual==GBM_FORMAT_XRGB8888){config=configs[i];break;}}
        if(!config)throw std::runtime_error("No suitable direct EGL config");
        context=eglCreateContext(egl,config,EGL_NO_CONTEXT,nullptr);
        eglSurface=eglCreateWindowSurface(egl,config,reinterpret_cast<EGLNativeWindowType>(surface),nullptr);
        if(context==EGL_NO_CONTEXT || eglSurface==EGL_NO_SURFACE || !eglMakeCurrent(egl,eglSurface,eglSurface,context))throw std::runtime_error("Cannot activate direct EGL context");
        std::cout<<"Direct DRM lease: "<<target<<" "<<mode.hdisplay<<"x"<<mode.vdisplay<<"@"<<mode.vrefresh<<std::endl;
    }
    bool pump(){
        while(wl_display_prepare_read(display)!=0)if(wl_display_dispatch_pending(display)<0)return false;
        wl_display_flush(display);pollfd p{wl_display_get_fd(display),POLLIN,0};
        if(poll(&p,1,0)>0){if(wl_display_read_events(display)<0)return false;}else wl_display_cancel_read(display);
        if(wl_display_dispatch_pending(display)<0)return false;
        return !ended;
    }
    static void flip(int,unsigned,unsigned sec,unsigned usec,void* d){
        auto& self=*static_cast<Impl*>(d);
        const auto now=static_cast<std::uint64_t>(sec)*1000000ull+usec;
        if(self.haveVblank && vblankIntervalMissed(self.lastVblankUs,now,self.mode.vrefresh)) ++self.missed;
        self.lastVblankUs=now;self.haveVblank=true;self.flipping=false;
    }
    void swap(const std::function<void()>& service){
        if(!eglSwapBuffers(egl,eglSurface))throw std::runtime_error("Direct EGL swap failed");
        auto* next=gbm_surface_lock_front_buffer(surface);if(!next)throw std::runtime_error("Cannot lock scanout buffer");
        uint32_t fb=0;
        if(framebuffers.contains(next))fb=framebuffers[next];else{
            uint32_t handles[4]={gbm_bo_get_handle(next).u32,0,0,0},strides[4]={gbm_bo_get_stride(next),0,0,0},offsets[4]={};
            if(drmModeAddFB2(fd,mode.hdisplay,mode.vdisplay,GBM_FORMAT_XRGB8888,handles,strides,offsets,&fb,0)){gbm_surface_release_buffer(surface,next);throw std::runtime_error("Cannot register scanout framebuffer");}
            framebuffers[next]=fb;
        }
        if(!front){if(drmModeSetCrtc(fd,crtc,fb,0,0,&connector,1,&mode)){gbm_surface_release_buffer(surface,next);throw std::runtime_error("Cannot set leased display mode");}}
        else{
            flipping=true;
            if(drmModePageFlip(fd,crtc,fb,DRM_MODE_PAGE_FLIP_EVENT,this)){gbm_surface_release_buffer(surface,next);throw std::runtime_error("DRM page flip failed");}
            drmEventContext events{};events.version=2;events.page_flip_handler=flip;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            while(flipping){
                const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
                if(remaining<=0){gbm_surface_release_buffer(surface,next);throw std::runtime_error("DRM page flip timed out");}
                pollfd p{fd,POLLIN,0};
                const int ready=poll(&p,1,static_cast<int>(service?std::min<int64_t>(remaining,2):remaining));
                // SIGTERM asks the main loop to stop. Finish the outstanding
                // flip before releasing its buffer; EINTR is not a timeout.
                if(ready<0 && errno==EINTR)continue;
                if(ready==0){if(service)service();continue;}
                if(ready<0 || !(p.revents&POLLIN) || drmHandleEvent(fd,&events)<0){
                    gbm_surface_release_buffer(surface,next);
                    throw std::runtime_error("DRM page flip event failed");
                }
            }
            gbm_surface_release_buffer(surface,front);
        }
        front=next;
    }
};
DirectOutput::DirectOutput(const std::string& name,bool stereo):impl(std::make_unique<Impl>()){impl->open(name,stereo);}
DirectOutput::~DirectOutput()=default;
int DirectOutput::width()const{return impl->mode.hdisplay;}
int DirectOutput::height()const{return impl->mode.vdisplay;}
bool DirectOutput::pump(){return impl->pump();}
void DirectOutput::swap(const std::function<void()>& service){impl->swap(service);}
unsigned DirectOutput::refreshHz() const {return impl->mode.vrefresh;}
unsigned DirectOutput::missedVblanks() const {return impl->missed;}
bool DirectOutput::hasVblank() const {return impl->haveVblank;}
std::uint64_t DirectOutput::lastVblankUs() const {return impl->lastVblankUs;}
std::vector<std::string> DirectOutput::connectors(){Impl p;std::vector<std::string> result;for(auto& o:p.outputs)if(!o->withdrawn)result.push_back(o->name);return result;}
