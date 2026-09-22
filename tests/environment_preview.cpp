// Opt-in background-only visual/performance probe; never connects to XR outputs.
#include "environment.hpp"
#include <algorithm>
#include <iostream>
#include <vector>
#include <unistd.h>

namespace {
constexpr int width=1920,height=1080;
const float identity[]={1.f,0.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f,0.f,1.f};
void projection() {
    glViewport(0,0,width,height);glClearColor(0,0,0,1);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    const double t=.1*std::tan(14*3.141592653589793/180);
    glFrustum(-t*width/height,t*width/height,-t,t,.1,20000);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
}
void capture(const std::filesystem::path& path) {
    std::vector<unsigned char> pixels(width*height*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    std::ofstream out(path,std::ios::binary);out<<"P6\n"<<width<<' '<<height<<"\n255\n";
    for(int row=height-1;row>=0;--row)out.write(reinterpret_cast<const char*>(pixels.data()+row*width*3),width*3);
}
void measure(SkyEnvironment& sky,const theme::Rgb& accent,const std::string& name) {
    GLuint query=0;glGenQueries(1,&query);
    std::vector<double> times;
    for(int i=0;i<240;++i){
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glBeginQuery(GL_TIME_ELAPSED,query);sky.draw(identity,accent);glEndQuery(GL_TIME_ELAPSED);
        GLuint64 ns=0;glGetQueryObjectui64v(query,GL_QUERY_RESULT,&ns);
        if(i>=40)times.push_back(double(ns)/1e6);
    }
    std::sort(times.begin(),times.end());glDeleteQueries(1,&query);
    std::cout<<name<<": median "<<times[times.size()/2]<<" ms; p95 "<<times[times.size()*95/100]<<" ms per 1920x1080 eye, 28-degree vertical FOV\n";
}
void run(const std::filesystem::path& output,const std::string& panorama,const std::string& config) {
    SkyEnvironment sky(config);theme::Accent theme;theme.update(0);double now=0;
    projection();
    for(const auto& name:{std::string("black"),std::string("tron"),std::string("panorama")}){
        if(name=="panorama" && panorama.empty())continue;
        const std::string asset=name=="tron"?"builtin:tron":name=="panorama"?panorama:"";
        {std::ofstream file(config);file<<100<<" 0 "<<std::quoted(asset)<<(name=="tron"?" 1":"")<<'\n';}
        sky.update(now+=1);
        for(int i=0;sky.loadingImage() && i<10000;++i){SDL_Delay(1);sky.update(now+=.01);}
        if(sky.loadingImage() || !sky.error.empty())throw std::runtime_error(sky.error.empty()?"Image load timed out":sky.error);
        measure(sky,theme.rgb,name);capture(output/(name+".ppm"));
        if(name=="tron"){
            const theme::Rgb alternate{.76f,.60f,1.f};
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);sky.draw(identity,alternate);capture(output/"tron-purple.ppm");
        }
    }
    sky.release();
}
}
int main(int argc,char** argv) {
    if(argc<2 || argc>3){std::cerr<<"Usage: environment-preview OUTPUT_DIRECTORY [PANORAMA_BMP]\n";return 2;}
    if(SDL_Init(SDL_INIT_VIDEO)!=0){std::cerr<<SDL_GetError()<<'\n';return 1;}
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    auto window=SDL_CreateWindow("XR environment probe",0,0,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    auto context=window?SDL_GL_CreateContext(window):nullptr;
    if(!context){std::cerr<<SDL_GetError()<<'\n';SDL_Quit();return 1;}
    std::cout<<"GPU: "<<glGetString(GL_RENDERER)<<'\n';
    char folder[]="/tmp/xr-environment-probe-XXXXXX";
    int result=0;
    try{
        if(!mkdtemp(folder))throw std::runtime_error("Could not create temporary state");
        std::filesystem::create_directories(argv[1]);
        run(argv[1],argc==3?std::filesystem::absolute(argv[2]).string():"",std::string(folder)+"/environment.tsv");
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("OpenGL error during probe");
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    std::filesystem::remove_all(folder);
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return result;
}
