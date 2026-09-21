#pragma once
#include "gpu_capture.hpp"
#include "xdg-shell-client.h"
#include <array>
#include <memory>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <cerrno>

// A compositor-owned window fed by the renderer's GPU. Separate Wayland queue;
// never waits for frame callbacks/releases, never reads screen pixels on the CPU.
class Spectator {
    struct Slot {
        GpuCapture image;
        GLuint depth=0;
        bool busy=false;
        int depthWidth=0,depthHeight=0;
        ~Slot(){if(depth)glDeleteRenderbuffers(1,&depth);}
        static void released(void* p,wl_buffer*){static_cast<Slot*>(p)->busy=false;}
        static constexpr wl_buffer_listener listener{released};
    };
    wl_display* display=nullptr;
    wl_registry* registry=nullptr;
    wl_compositor* compositor=nullptr;
    zwp_linux_dmabuf_v1* dma=nullptr;
    xdg_wm_base* shell=nullptr;
    wl_surface* surface=nullptr;
    xdg_surface* role=nullptr;
    xdg_toplevel* toplevel=nullptr;
    wl_callback* callback=nullptr;
    std::array<std::unique_ptr<Slot>,3> slots;
    Slot* drawing=nullptr;
    bool configured=false,closed=false;
    int w=1280,h=720,pendingW=1280,pendingH=720;
    double lastFrame=0;
    static void ping(void*,xdg_wm_base* s,uint32_t serial){xdg_wm_base_pong(s,serial);}
    static constexpr xdg_wm_base_listener shellListener{ping};
    static void format(void*,zwp_linux_dmabuf_v1*,uint32_t){}
    static void modifier(void*,zwp_linux_dmabuf_v1*,uint32_t,uint32_t,uint32_t){}
    static constexpr zwp_linux_dmabuf_v1_listener dmaListener{format,modifier};
    static void global(void* data,wl_registry* registry,uint32_t name,const char* iface,uint32_t version){
        auto& self=*static_cast<Spectator*>(data);
        if(!std::strcmp(iface,wl_compositor_interface.name))self.compositor=static_cast<wl_compositor*>(wl_registry_bind(registry,name,&wl_compositor_interface,std::min(version,4u)));
        else if(!std::strcmp(iface,xdg_wm_base_interface.name)){
            self.shell=static_cast<xdg_wm_base*>(wl_registry_bind(registry,name,&xdg_wm_base_interface,1));
            xdg_wm_base_add_listener(self.shell,&shellListener,&self);
        }else if(!std::strcmp(iface,zwp_linux_dmabuf_v1_interface.name) && version>=3){
            self.dma=static_cast<zwp_linux_dmabuf_v1*>(wl_registry_bind(registry,name,&zwp_linux_dmabuf_v1_interface,3));
            zwp_linux_dmabuf_v1_add_listener(self.dma,&dmaListener,&self);
        }
    }
    static void removed(void*,wl_registry*,uint32_t){}
    static constexpr wl_registry_listener registryListener{global,removed};
    static void configure(void* data,xdg_surface* role,uint32_t serial){
        auto& self=*static_cast<Spectator*>(data);
        xdg_surface_ack_configure(role,serial);self.w=self.pendingW;self.h=self.pendingH;self.configured=true;
    }
    static constexpr xdg_surface_listener surfaceListener{configure};
    static void size(void* data,xdg_toplevel*,int32_t w,int32_t h,wl_array*){
        auto& self=*static_cast<Spectator*>(data);
        if(w>0)self.pendingW=std::clamp(w,1,3840);
        if(h>0)self.pendingH=std::clamp(h,1,2160);
    }
    static void close(void* data,xdg_toplevel*){static_cast<Spectator*>(data)->closed=true;}
    static void bounds(void*,xdg_toplevel*,int32_t,int32_t){}
    static void capabilities(void*,xdg_toplevel*,wl_array*){}
    static constexpr xdg_toplevel_listener topListener{size,close,bounds,capabilities};
    static void done(void* data,wl_callback* callback,uint32_t){
        auto& self=*static_cast<Spectator*>(data);wl_callback_destroy(callback);self.callback=nullptr;
    }
    static constexpr wl_callback_listener frameListener{done};
    void cleanup(){
        drawing=nullptr;for(auto& slot:slots)slot.reset();
        if(callback)wl_callback_destroy(callback);
        if(toplevel)xdg_toplevel_destroy(toplevel);
        if(role)xdg_surface_destroy(role);
        if(surface)wl_surface_destroy(surface);
        if(shell)xdg_wm_base_destroy(shell);
        if(dma)zwp_linux_dmabuf_v1_destroy(dma);
        if(compositor)wl_compositor_destroy(compositor);
        if(registry)wl_registry_destroy(registry);
        if(display){wl_display_flush(display);wl_display_disconnect(display);}
    }
public:
    unsigned frames=0;
    Spectator(){
        try{
            display=wl_display_connect(nullptr);if(!display)throw std::runtime_error("Spectator: cannot connect to Wayland");
            registry=wl_display_get_registry(display);wl_registry_add_listener(registry,&registryListener,this);
            if(wl_display_roundtrip(display)<0 || !compositor || !shell || !dma)throw std::runtime_error("Spectator: requires xdg-shell and DMA-BUF");
            surface=wl_compositor_create_surface(compositor);role=xdg_wm_base_get_xdg_surface(shell,surface);
            xdg_surface_add_listener(role,&surfaceListener,this);
            toplevel=xdg_surface_get_toplevel(role);xdg_toplevel_add_listener(toplevel,&topListener,this);
            xdg_toplevel_set_app_id(toplevel,"omarchy-xr-spectator");
            xdg_toplevel_set_title(toplevel,"Omarchy XR — Mono spectator");
            xdg_toplevel_set_min_size(toplevel,320,180);xdg_toplevel_set_max_size(toplevel,1920,1080);
            wl_surface_commit(surface);wl_display_flush(display);
        }catch(...){cleanup();throw;}
    }
    ~Spectator(){cleanup();}
    bool pump(){
        if(closed)return false;
        while(wl_display_prepare_read(display)!=0)if(wl_display_dispatch_pending(display)<0)return false;
        wl_display_flush(display);pollfd descriptor{wl_display_get_fd(display),POLLIN,0};
        if(poll(&descriptor,1,0)>0 && (descriptor.revents&POLLIN)){
            if(wl_display_read_events(display)<0 || wl_display_dispatch_pending(display)<0)return false;
        }else wl_display_cancel_read(display);
        return !closed && !wl_display_get_error(display);
    }
    int width()const{return w;}
    int height()const{return h;}
    bool begin(double now){
        if(!configured || callback || now-lastFrame<1./30-.001)return false;
        drawing=nullptr;
        for(auto& slot:slots){
            if(!slot)slot=std::make_unique<Slot>();
            if(slot->busy)continue;
            if(!slot->image.allocate(dma,w,h,DRM_FORMAT_XRGB8888))throw std::runtime_error("Spectator: GPU buffer import failed");
            if(!wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(slot->image.buffer())))wl_buffer_add_listener(slot->image.buffer(),&Slot::listener,slot.get());
            glBindFramebuffer(GL_FRAMEBUFFER,slot->image.framebuffer());
            if(!slot->depth)glGenRenderbuffers(1,&slot->depth);
            glBindRenderbuffer(GL_RENDERBUFFER,slot->depth);
            if(slot->depthWidth!=w || slot->depthHeight!=h){
                glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);
                slot->depthWidth=w;slot->depthHeight=h;
            }
            glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,slot->depth);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){glBindFramebuffer(GL_FRAMEBUFFER,0);throw std::runtime_error("Spectator: incomplete framebuffer");}
            drawing=slot.get();lastFrame=now;return true;
        }
        return false;
    }
    void present(){
        if(!drawing)return;
        glBindFramebuffer(GL_FRAMEBUFFER,0);glFlush();
        drawing->busy=true;
        wl_surface_attach(surface,drawing->image.buffer(),0,0);
        wl_surface_damage(surface,0,0,w,h);
        callback=wl_surface_frame(surface);wl_callback_add_listener(callback,&frameListener,this);
        wl_surface_commit(surface);wl_display_flush(display);drawing=nullptr;++frames;
    }
};
