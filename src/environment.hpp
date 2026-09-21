#pragma once
#include <SDL.h>
#include <SDL_opengl.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <memory>
#include <string>
#include <sstream>

// Owns one resident sky and a staging texture. Decode is off-thread; upload is
// bounded to 128 rows per frame. No per-frame image IO or mesh construction.
class SkyEnvironment {
    using Surface = std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)>;
    struct Decoded { Surface pixels{nullptr,SDL_FreeSurface}; std::string error; };
    std::future<Decoded> loader;
    Surface pixels{nullptr,SDL_FreeSurface};
    std::string configPath, requested, loading, resident;
    std::string lastConfig;
    GLuint texture=0, staging=0, mesh=0;
    int row=0, maxSize=0;
    double nextCheck=0;
    float brightness=.25f, rotation=0;
    void discardStaging() {pixels.reset();if(staging)glDeleteTextures(1,&staging);staging=0;row=0;}
public:
    std::string error;
    bool loadingImage() const {return loader.valid() || bool(pixels);}
    explicit SkyEnvironment(const std::string& path):configPath(path) {glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxSize);}
    void update(double now) {
        if(!configPath.empty() && now>=nextCheck) {
            nextCheck=now+.25;
            std::ifstream file(configPath);std::string line;
            if(std::getline(file,line) && line!=lastConfig) {
                lastConfig=line;
                std::istringstream input(line);float level,angle;std::string path,extra;
                if(input>>level>>angle>>std::quoted(path) && !(input>>extra) && std::isfinite(level) && std::isfinite(angle) && level>=0 && level<=100 && angle>=-180 && angle<=180) {
                    brightness=level/100;rotation=angle;
                    if(path!=requested){requested=path;error.clear();discardStaging();}
                } else error="Invalid environment settings";
            }
        }
        if(loader.valid() && loader.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            auto decoded=loader.get();
            if(loading==requested) {
                if(!decoded.error.empty()){error=decoded.error;resident=requested;}
                else {
                    pixels=std::move(decoded.pixels);row=0;
                    glGenTextures(1,&staging);glBindTexture(GL_TEXTURE_2D,staging);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                    while(glGetError()!=GL_NO_ERROR){}
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,pixels->w,pixels->h,0,GL_RGB,GL_UNSIGNED_BYTE,nullptr);
                    if(glGetError()!=GL_NO_ERROR){error="Could not allocate background texture";resident=requested;discardStaging();}
                }
            }
        }
        if(requested.empty()) {
            if(texture)glDeleteTextures(1,&texture);
            texture=0;resident.clear();discardStaging();return;
        }
        if(!loader.valid() && !pixels && requested!=resident) {
            loading=requested;const auto path=loading;const int limit=maxSize;
            loader=std::async(std::launch::async,[path,limit] {
                Decoded result;
                Surface source(SDL_LoadBMP(path.c_str()),SDL_FreeSurface);
                if(!source){result.error="Could not load background image";return result;}
                if(source->w!=source->h*2 || source->w>limit || source->w>8192){result.error="Background exceeds GPU texture limits";return result;}
                result.pixels.reset(SDL_ConvertSurfaceFormat(source.get(),SDL_PIXELFORMAT_RGB24,0));
                if(!result.pixels)result.error="Could not decode background pixels";
                return result;
            });
        }
        if(pixels) {
            const int count=std::min(128,pixels->h-row);
            glBindTexture(GL_TEXTURE_2D,staging);
            GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,row,pixels->w,count,GL_RGB,GL_UNSIGNED_BYTE,static_cast<char*>(pixels->pixels)+row*pixels->pitch);
            glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
            row+=count;
            if(row==pixels->h){if(texture)glDeleteTextures(1,&texture);texture=staging;staging=0;resident=requested;pixels.reset();error.clear();}
        }
    }
    void draw(const float* view) {
        if(!texture || requested.empty() || brightness<=0)return;
        if(!mesh) {
            mesh=glGenLists(1);glNewList(mesh,GL_COMPILE);
            constexpr int columns=128,rows=64;constexpr float pi=3.14159265358979323846f;
            for(int y=0;y<rows;++y){
                glBegin(GL_QUAD_STRIP);
                for(int x=0;x<=columns;++x)for(int side=0;side<2;++side){
                    const float u=float(x)/columns,v=float(y+side)/rows;
                    const float longitude=(u-.5f)*2*pi,latitude=(.5f-v)*pi;
                    glTexCoord2f(u,v);glVertex3f(std::cos(latitude)*std::sin(longitude),std::sin(latitude),-std::cos(latitude)*std::cos(longitude));
                }
                glEnd();
            }
            glEndList();
        }
        glPushAttrib(GL_ENABLE_BIT|GL_DEPTH_BUFFER_BIT|GL_CURRENT_BIT|GL_TEXTURE_BIT);
        glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
        glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,texture);glColor3f(brightness,brightness,brightness);
        glPushMatrix();glLoadIdentity();glMultMatrixf(view);glRotatef(rotation,0,1,0);glCallList(mesh);glPopMatrix();
        glPopAttrib();
    }
    // Called while the renderer's GL context is still current.
    void release() {if(loader.valid())loader.wait();discardStaging();if(texture)glDeleteTextures(1,&texture);texture=0;if(mesh)glDeleteLists(mesh,1);mesh=0;}
};
