#pragma once
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>
#include <drm_fourcc.h>
#include "linux-dmabuf-client.h"

// Reusable compositor destination + GPU-only scaled texture. No CPU readback.
struct GpuCapture {
    int deviceFd=-1;
    gbm_device* device=nullptr;
    gbm_bo* bo=nullptr;
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLImageKHR image=EGL_NO_IMAGE_KHR;
    GLuint nativeTexture=0,texture=0,readFbo=0,writeFbo=0;
    wl_buffer* buffer=nullptr;
    unsigned width=0,height=0,scaledWidth=0,scaledHeight=0,format=0;
    ~GpuCapture(){clear();if(device)gbm_device_destroy(device);if(deviceFd>=0)close(deviceFd);}
    void clearSource(){
        if(buffer)wl_buffer_destroy(buffer);
        buffer=nullptr;
        if(nativeTexture)glDeleteTextures(1,&nativeTexture);
        nativeTexture=0;
        if(readFbo)glDeleteFramebuffers(1,&readFbo);
        readFbo=0;
        if(image!=EGL_NO_IMAGE_KHR){auto destroy=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");if(destroy)destroy(display,image);image=EGL_NO_IMAGE_KHR;}
        if(bo)gbm_bo_destroy(bo);
        bo=nullptr;
    }
    void clear(){
        clearSource();
        if(texture)glDeleteTextures(1,&texture);
        if(writeFbo)glDeleteFramebuffers(1,&writeFbo);
        texture=writeFbo=0;scaledWidth=scaledHeight=0;
    }
    bool init(){
        if(device)return true;
        display=eglGetCurrentDisplay();if(display==EGL_NO_DISPLAY)return false;
        auto query=(PFNEGLQUERYDISPLAYATTRIBEXTPROC)eglGetProcAddress("eglQueryDisplayAttribEXT");
        auto name=(PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
        EGLAttrib id=0;if(!query || !name || !query(display,EGL_DEVICE_EXT,&id))return false;
        const char* node=name((EGLDeviceEXT)id,EGL_DRM_RENDER_NODE_FILE_EXT);
        if(!node)return false;
        deviceFd=open(node,O_RDWR|O_CLOEXEC);if(deviceFd<0)return false;
        device=gbm_create_device(deviceFd);return device;
    }
    bool allocate(zwp_linux_dmabuf_v1* manager,unsigned w,unsigned h,unsigned fmt){
        if(buffer && width==w && height==h && format==fmt)return true;
        clearSource();if(!manager || !init())return false;
        auto create=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
        auto target=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if(!create || !target)return false;
        bo=gbm_bo_create(device,w,h,fmt,GBM_BO_USE_RENDERING|GBM_BO_USE_LINEAR);
        if(!bo || gbm_bo_get_plane_count(bo)!=1){clearSource();return false;}
        const int fd=gbm_bo_get_fd(bo);if(fd<0){clearSource();return false;}
        const auto stride=gbm_bo_get_stride(bo);const auto modifier=gbm_bo_get_modifier(bo);
        // LINEAR is the portable baseline; never advertise an unnegotiated tiled modifier.
        if(modifier!=DRM_FORMAT_MOD_LINEAR && modifier!=DRM_FORMAT_MOD_INVALID){close(fd);clearSource();return false;}
        const EGLint attrs[]{EGL_WIDTH,(EGLint)w,EGL_HEIGHT,(EGLint)h,EGL_LINUX_DRM_FOURCC_EXT,(EGLint)fmt,
            EGL_DMA_BUF_PLANE0_FD_EXT,fd,EGL_DMA_BUF_PLANE0_OFFSET_EXT,0,EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)stride,EGL_NONE};
        image=create(display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,nullptr,attrs);
        if(image==EGL_NO_IMAGE_KHR){close(fd);clearSource();return false;}
        glGenTextures(1,&nativeTexture);glBindTexture(GL_TEXTURE_2D,nativeTexture);
        target(GL_TEXTURE_2D,image);
        glGenFramebuffers(1,&readFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,nativeTexture,0);
        bool complete=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
        if(!complete){close(fd);clearSource();return false;}
        auto params=zwp_linux_dmabuf_v1_create_params(manager);
        zwp_linux_buffer_params_v1_add(params,fd,0,0,stride,modifier>>32,modifier&0xffffffff);
        buffer=zwp_linux_buffer_params_v1_create_immed(params,w,h,fmt,0);
        zwp_linux_buffer_params_v1_destroy(params);close(fd);
        width=w;height=h;format=fmt;
        if(!texture)glGenTextures(1,&texture);
        glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        if(!writeFbo)glGenFramebuffers(1,&writeFbo);
        return true;
    }
    void scale(unsigned w,unsigned h,bool inverted){
        glBindTexture(GL_TEXTURE_2D,texture);
        if(w!=scaledWidth || h!=scaledHeight){
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            scaledWidth=w;scaledHeight=h;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,writeFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        glBlitFramebuffer(0,inverted?height:0,width,inverted?0:height,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_LINEAR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
        glFlush(); // publish the read fence before allowing the compositor to reuse the source
    }
};
