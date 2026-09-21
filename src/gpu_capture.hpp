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
#include "capture_scale.hpp"

// Two compositor destinations, the panel texture, and two scratch images.
// The source the viewer is showing is not handed back for the next copy.
struct GpuCapture {
    struct Slot {
        gbm_bo* bo=nullptr;
        EGLImageKHR image=EGL_NO_IMAGE_KHR;
        GLuint nativeTexture=0,readFbo=0;
        wl_buffer* buffer=nullptr;
        unsigned width=0,height=0,format=0;
        bool busy=false;
    };
    int deviceFd=-1;
    gbm_device* device=nullptr;
    EGLDisplay display=EGL_NO_DISPLAY;
    Slot slots[2]{};
    int shownSlot=-1,captureSlot=-1;
    bool invertY=false;
    GLuint texture=0,scratch[2]{},scratchFbo[2]{},writeFbo=0;
    unsigned scaledWidth=0,scaledHeight=0,scratchWidth[2]{},scratchHeight[2]{};
    ~GpuCapture(){clear();if(device)gbm_device_destroy(device);if(deviceFd>=0)close(deviceFd);}
    void destroySlot(Slot& slot){
        if(slot.buffer)wl_buffer_destroy(slot.buffer);
        slot.buffer=nullptr;
        if(slot.nativeTexture)glDeleteTextures(1,&slot.nativeTexture);
        slot.nativeTexture=0;
        if(slot.readFbo)glDeleteFramebuffers(1,&slot.readFbo);
        slot.readFbo=0;
        if(slot.image!=EGL_NO_IMAGE_KHR){auto destroy=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");if(destroy)destroy(display,slot.image);slot.image=EGL_NO_IMAGE_KHR;}
        if(slot.bo)gbm_bo_destroy(slot.bo);
        slot.bo=nullptr;slot.busy=false;slot.width=slot.height=slot.format=0;
    }
    void clearSource(){
        for(auto& slot:slots)destroySlot(slot);
        shownSlot=captureSlot=-1;
    }
    void clear(){
        clearSource();
        if(texture)glDeleteTextures(1,&texture);
        if(scratch[0])glDeleteTextures(1,&scratch[0]);
        if(scratch[1])glDeleteTextures(1,&scratch[1]);
        if(scratchFbo[0])glDeleteFramebuffers(1,&scratchFbo[0]);
        if(scratchFbo[1])glDeleteFramebuffers(1,&scratchFbo[1]);
        if(writeFbo)glDeleteFramebuffers(1,&writeFbo);
        texture=scratch[0]=scratch[1]=scratchFbo[0]=scratchFbo[1]=writeFbo=0;
        scaledWidth=scaledHeight=scratchWidth[0]=scratchWidth[1]=scratchHeight[0]=scratchHeight[1]=0;
    }
    bool retained() const { return shownSlot>=0 && slots[shownSlot].nativeTexture; }
    bool createFailed=false;
    wl_buffer* buffer() const { return captureSlot>=0 ? slots[captureSlot].buffer : nullptr; }
    GLuint framebuffer() const { return captureSlot>=0 ? slots[captureSlot].readFbo : 0; }
    unsigned width() const { return shownSlot>=0 ? slots[shownSlot].width : 0; }
    unsigned height() const { return shownSlot>=0 ? slots[shownSlot].height : 0; }
    unsigned capturedWidth() const { const int slot=capturePresentSlot(captureSlot,shownSlot,false); return slot>=0 ? slots[slot].width : 0; }
    unsigned capturedHeight() const { const int slot=capturePresentSlot(captureSlot,shownSlot,false); return slot>=0 ? slots[slot].height : 0; }
    void noteRelease(wl_buffer* released){
        for(auto& slot:slots) if(slot.buffer==released) slot.busy=false;
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
    bool makeSlot(Slot& slot,zwp_linux_dmabuf_v1* manager,unsigned w,unsigned h,unsigned fmt){
        auto create=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
        auto target=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if(!create || !target)return false;
        slot.bo=gbm_bo_create(device,w,h,fmt,GBM_BO_USE_RENDERING|GBM_BO_USE_LINEAR);
        if(!slot.bo || gbm_bo_get_plane_count(slot.bo)!=1){destroySlot(slot);return false;}
        const int fd=gbm_bo_get_fd(slot.bo);if(fd<0){destroySlot(slot);return false;}
        const auto stride=gbm_bo_get_stride(slot.bo);const auto modifier=gbm_bo_get_modifier(slot.bo);
        // LINEAR is the portable baseline; never advertise an unnegotiated tiled modifier.
        if(modifier!=DRM_FORMAT_MOD_LINEAR && modifier!=DRM_FORMAT_MOD_INVALID){close(fd);destroySlot(slot);return false;}
        const EGLint attrs[]{EGL_WIDTH,(EGLint)w,EGL_HEIGHT,(EGLint)h,EGL_LINUX_DRM_FOURCC_EXT,(EGLint)fmt,
            EGL_DMA_BUF_PLANE0_FD_EXT,fd,EGL_DMA_BUF_PLANE0_OFFSET_EXT,0,EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)stride,EGL_NONE};
        slot.image=create(display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,nullptr,attrs);
        if(slot.image==EGL_NO_IMAGE_KHR){close(fd);destroySlot(slot);return false;}
        glGenTextures(1,&slot.nativeTexture);glBindTexture(GL_TEXTURE_2D,slot.nativeTexture);
        target(GL_TEXTURE_2D,slot.image);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1,&slot.readFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,slot.readFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,slot.nativeTexture,0);
        bool complete=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
        if(!complete){close(fd);destroySlot(slot);return false;}
        auto params=zwp_linux_dmabuf_v1_create_params(manager);
        zwp_linux_buffer_params_v1_add(params,fd,0,0,stride,modifier>>32,modifier&0xffffffff);
        slot.buffer=zwp_linux_buffer_params_v1_create_immed(params,w,h,fmt,0);
        zwp_linux_buffer_params_v1_destroy(params);close(fd);
        slot.width=w;slot.height=h;slot.format=fmt;slot.busy=false;
        return true;
    }
    bool allocate(zwp_linux_dmabuf_v1* manager,unsigned w,unsigned h,unsigned fmt){
        createFailed=false;
        if(!manager || !init()){createFailed=true;return false;}
        const int chosen=captureAllocateSlot(shownSlot,slots[0].busy,slots[1].busy);
        if(chosen<0) return false;
        auto& slot=slots[chosen];
        if(slot.buffer && (slot.width!=w || slot.height!=h || slot.format!=fmt)) destroySlot(slot);
        if(!slot.buffer && !makeSlot(slot,manager,w,h,fmt)){createFailed=true;return false;}
        captureSlot=chosen;return true;
    }
    void markBusy(){ if(captureSlot>=0) slots[captureSlot].busy=true; }
    void ensureTexture(GLuint& tex,unsigned& currentW,unsigned& currentH,unsigned w,unsigned h){
        if(!tex)glGenTextures(1,&tex);
        glBindTexture(GL_TEXTURE_2D,tex);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        if(currentW!=w || currentH!=h){
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            currentW=w;currentH=h;
        }
    }
    void blit(GLuint srcFbo,unsigned sw,unsigned sh,GLuint& dstFbo,GLuint dstTex,unsigned dw,unsigned dh,bool inverted){
        if(!dstFbo)glGenFramebuffers(1,&dstFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,srcFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,dstFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,dstTex,0);
        glBlitFramebuffer(0,inverted?int(sh):0,int(sw),inverted?0:int(sh),0,0,int(dw),int(dh),GL_COLOR_BUFFER_BIT,GL_LINEAR);
    }
    // Returns the texture to display. A 1:1 upright import is the source itself.
    GLuint present(unsigned w,unsigned h,bool inverted,bool rebake=false){
        if(!rebake)invertY=inverted;
        const int source=capturePresentSlot(captureSlot,shownSlot,rebake);
        if(source<0 || !slots[source].nativeTexture) return 0;
        const unsigned sw=slots[source].width,sh=slots[source].height;
        if(!inverted && w==sw && h==sh){
            if(!rebake){shownSlot=source;captureSlot=-1;}
            glFlush();
            return slots[source].nativeTexture;
        }
        auto passes=scalePasses(sw,sh,w,h);
        GLuint srcFbo=slots[source].readFbo;unsigned cw=sw,ch=sh;bool flip=inverted;
        for(size_t i=0;i<passes.size();++i){
            const int scratchIndex=scalePassScratch(static_cast<unsigned>(i),static_cast<unsigned>(passes.size()));
            GLuint& dest=scratchIndex<0?texture:scratch[scratchIndex];
            unsigned& destW=scratchIndex<0?scaledWidth:scratchWidth[scratchIndex];
            unsigned& destH=scratchIndex<0?scaledHeight:scratchHeight[scratchIndex];
            ensureTexture(dest,destW,destH,passes[i].width,passes[i].height);
            GLuint& destFbo=scratchIndex<0?writeFbo:scratchFbo[scratchIndex];
            blit(srcFbo,cw,ch,destFbo,dest,passes[i].width,passes[i].height,flip);
            flip=false;srcFbo=destFbo;cw=passes[i].width;ch=passes[i].height;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
        glFlush();
        if(!rebake){shownSlot=source;captureSlot=-1;}
        return texture;
    }
};
