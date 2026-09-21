#include "environment.hpp"
#include <cassert>
#include <array>
#include <iostream>
#include <unistd.h>

int main() {
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    auto window=SDL_CreateWindow("Environment test",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    assert(window);auto context=SDL_GL_CreateContext(window);assert(context);
    char folder[]="/tmp/xr-environment-XXXXXX";assert(mkdtemp(folder));
    const auto config=std::string(folder)+"/environment.tsv",bitmap=std::string(folder)+"/sky.bmp";
    auto surface=SDL_CreateRGBSurfaceWithFormat(0,512,256,24,SDL_PIXELFORMAT_RGB24);assert(surface);
    SDL_FillRect(surface,nullptr,SDL_MapRGB(surface->format,255,0,0));
    SDL_Rect middle{128,0,256,256};SDL_FillRect(surface,&middle,SDL_MapRGB(surface->format,0,255,0));
    assert(SDL_SaveBMP(surface,bitmap.c_str())==0);SDL_FreeSurface(surface);
    auto settings=[&](int brightness,int rotation,const std::string& path){
        {std::ofstream file(config+".tmp");file<<brightness<<' '<<rotation<<' '<<std::quoted(path)<<'\n';}
        std::filesystem::rename(config+".tmp",config);
    };
    settings(100,0,bitmap);
    SkyEnvironment sky(config);
    double now=0;
    sky.update(now);
    for(int i=0;sky.loadingImage() && i<500;++i){SDL_Delay(2);sky.update(now+=.3);}
    assert(!sky.loadingImage() && sky.error.empty());
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    auto sample=[&](bool flipped){
        glViewport(0,0,64,64);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-.1,.1,flipped?.1:-.1,flipped?-.1:.1,.1,100);
        glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(100,100,100);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
        sky.draw(identity);
        assert(glIsEnabled(GL_DEPTH_TEST));GLboolean depth;glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);assert(depth);
        std::array<unsigned char,3> color{};glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(32,32,1,1,GL_RGB,GL_UNSIGNED_BYTE,color.data());
        assert(glGetError()==GL_NO_ERROR);return color;
    };
    auto color=sample(false);assert(color[1]>240 && color[0]<10);
    assert(sample(true)==color); // OBS's inverted projection matches the main view.
    settings(50,0,bitmap);sky.update(now+=1);color=sample(false);assert(color[1]>120 && color[1]<135);
    settings(100,180,bitmap);sky.update(now+=1);color=sample(false);assert(color[0]>240 && color[1]<10);
    settings(0,0,bitmap);sky.update(now+=1);color=sample(false);assert(color[0]==0 && color[1]==0);
    settings(100,0,bitmap+".missing");sky.update(now+=1);
    for(int i=0;sky.loadingImage() && i<500;++i){SDL_Delay(2);sky.update(now+=.3);}
    assert(!sky.error.empty());
    settings(100,0,"");sky.update(now+=1);color=sample(false);assert(color[0]==0 && color[1]==0);
    sky.release();SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    std::filesystem::remove_all(folder);
    std::cout<<"Environment: decode, live brightness/rotation, mono projection, GL state, errors and disable passed\n";
}
