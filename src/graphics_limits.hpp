#pragma once
#include "gpu_capture.hpp"
#include <xf86drmMode.h>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace graphics_limits {
struct Context {
    int fd=-1;
    gbm_device* device=nullptr;
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLContext context=EGL_NO_CONTEXT;
    ~Context(){
        if(display!=EGL_NO_DISPLAY){
            eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
            if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
            eglTerminate(display);
        }
        if(device)gbm_device_destroy(device);
        if(fd>=0)close(fd);
    }
};
inline void report(){
    std::cout<<"{\"gpus\":[";
    bool first=true;
    if(std::filesystem::exists("/dev/dri"))for(const auto& entry:std::filesystem::directory_iterator("/dev/dri")){
        if(!entry.path().filename().string().starts_with("renderD"))continue;
        if(!first)std::cout<<",";
        first=false;
        const auto node=entry.path().string();
        std::cout<<"{\"node\":"<<std::quoted(node);
        try{
            Context c;c.fd=open(node.c_str(),O_RDWR|O_CLOEXEC);
            if(c.fd<0 || !(c.device=gbm_create_device(c.fd)))throw std::runtime_error("Cannot open GPU");
            auto platform=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
            if(!platform)throw std::runtime_error("EGL platform API unavailable");
            c.display=platform(EGL_PLATFORM_GBM_KHR,c.device,nullptr);
            if(c.display==EGL_NO_DISPLAY || !eglInitialize(c.display,nullptr,nullptr) || !eglBindAPI(EGL_OPENGL_API))throw std::runtime_error("Cannot initialize GPU EGL");
            const EGLint attrs[]{EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};
            EGLConfig config;EGLint count=0;
            if(!eglChooseConfig(c.display,attrs,&config,1,&count) || !count)throw std::runtime_error("No OpenGL configuration");
            c.context=eglCreateContext(c.display,config,EGL_NO_CONTEXT,nullptr);
            if(c.context==EGL_NO_CONTEXT || !eglMakeCurrent(c.display,EGL_NO_SURFACE,EGL_NO_SURFACE,c.context))throw std::runtime_error("Cannot activate GPU probe context");
            GLint texture=0,renderbuffer=0,viewport[2]{};
            glGetIntegerv(GL_MAX_TEXTURE_SIZE,&texture);
            glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&renderbuffer);
            glGetIntegerv(GL_MAX_VIEWPORT_DIMS,viewport);
            if(glGetError()!=GL_NO_ERROR || texture<=0 || renderbuffer<=0 || viewport[0]<=0 || viewport[1]<=0)throw std::runtime_error("Invalid GPU limits");
            std::cout<<",\"renderer\":"<<std::quoted(reinterpret_cast<const char*>(glGetString(GL_RENDERER)))
                <<",\"texture\":"<<texture<<",\"renderbuffer\":"<<renderbuffer
                <<",\"viewportWidth\":"<<viewport[0]<<",\"viewportHeight\":"<<viewport[1];
        }catch(const std::exception& e){std::cout<<",\"error\":"<<std::quoted(e.what());}
        std::cout<<"}";
    }
    std::cout<<"],\"cards\":[";first=true;
    if(std::filesystem::exists("/dev/dri"))for(const auto& entry:std::filesystem::directory_iterator("/dev/dri")){
        if(!entry.path().filename().string().starts_with("card"))continue;
        int fd=open(entry.path().c_str(),O_RDWR|O_CLOEXEC);if(fd<0)continue;
        auto* resources=drmModeGetResources(fd);
        if(resources){
            if(!first)std::cout<<",";
            first=false;
            std::cout<<"{\"node\":"<<std::quoted(entry.path().string())<<",\"maxWidth\":"<<resources->max_width<<",\"maxHeight\":"<<resources->max_height<<"}";
            drmModeFreeResources(resources);
        }
        close(fd);
    }
    std::cout<<"]}\n";
}
}
