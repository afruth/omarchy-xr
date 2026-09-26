// Opt-in live probe for WindowCaptureHub/WindowCapture (docs/infinite-canvas-plan.md §3.2, §4.4).
// Captures Hyprland windows by address on a hidden GL window and prints, once per second, each
// window's distinct frames per second, requests per second, request->ready p50 and transport.
//
// usage: window-capture-probe [--seconds N] [--hide S] [--pull] [--inflight K] [--rate HZ] ADDRESS...
// --pull, --inflight and --rate apply to the addresses that follow them; ADDRESS is the hex
// address from `hyprctl clients -j`. Windows sharing a rate get evenly spread phases. --hide S
// makes every window idle from S to S+1 seconds and prints the thumbnail it leaves.
#include "window_capture.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
struct Target {
    std::uint64_t address=0;
    unsigned rate=60, inFlight=1;
    bool pull=false;
    std::unique_ptr<WindowCapture> capture;
    CapturedFrame frame;
    unsigned framesMark=0, requestsMark=0, failuresMark=0;
    std::vector<double> waits;
};

double p50(std::vector<double> v){
    if(v.empty())return -1;
    std::nth_element(v.begin(),v.begin()+long(v.size()/2),v.end());
    return v[v.size()/2];
}

bool parse(int argc,char** argv,double& seconds,double& hide,std::vector<Target>& targets){
    Target next;
    for(int i=1;i<argc;++i){
        const std::string a=argv[i];
        if(a=="--seconds" && i+1<argc)seconds=std::atof(argv[++i]);
        else if(a=="--hide" && i+1<argc)hide=std::atof(argv[++i]);
        else if(a=="--pull")next.pull=true;
        else if(a=="--inflight" && i+1<argc)next.inFlight=unsigned(std::clamp(std::atoi(argv[++i]),1,2));
        else if(a=="--rate" && i+1<argc)next.rate=unsigned(std::clamp(std::atoi(argv[++i]),1,240));
        else if(a.rfind("--",0)==0)return false;
        else{
            Target t; t.rate=next.rate; t.inFlight=next.inFlight; t.pull=next.pull;
            t.address=std::stoull(a,nullptr,16);
            targets.push_back(std::move(t));
        }
    }
    return !targets.empty() && seconds>0;
}

void report(double t,std::vector<Target>& targets,double span){
    std::printf("{\"t\":%.1f,\"windows\":[",t);
    for(size_t i=0;i<targets.size();++i){
        auto& w=targets[i];
        const auto& c=*w.capture;
        std::printf("%s{\"addr\":\"%lx\",\"fps\":%.1f,\"requests\":%.1f,\"fail\":%u,\"wait_p50\":%.1f,\"import\":%.2f,\"transport\":\"%s\",\"lanes\":%u,\"phase\":%.3f,\"alive\":%s,\"size\":\"%ux%u\"}",
            i?",":"",(unsigned long)w.address,(c.frames()-w.framesMark)/span,(c.requests()-w.requestsMark)/span,c.failures()-w.failuresMark,
            p50(w.waits),c.importLatencyMs(),c.transport(),c.lanes(),c.phase(),c.alive()?"true":"false",w.frame.sourceWidth,w.frame.sourceHeight);
        w.framesMark=c.frames();w.requestsMark=c.requests();w.failuresMark=c.failures();w.waits.clear();
    }
    std::printf("]}\n");std::fflush(stdout);
}

void configure(std::vector<Target>& targets,WindowCaptureHub& hub){
    // Rates, lanes and pulls stay fixed for the run; demand follows --hide in the loop.
    std::map<unsigned,std::pair<size_t,size_t>> phases;   // rate -> (next index, count)
    for(auto& w:targets)++phases[w.rate].second;
    for(auto& w:targets){
        w.capture=hub.open(w.address);
        auto& [index,count]=phases[w.rate];
        w.capture->setFrameRate(w.rate,w.inFlight,Cadence::spread(index++,count));
        w.capture->setIgnoreDamage(w.pull);
    }
}
}

int main(int argc,char** argv){
    double seconds=10,hide=-1;
    std::vector<Target> targets;
    if(!parse(argc,argv,seconds,hide,targets)){
        std::fprintf(stderr,"usage: %s [--seconds N] [--hide S] [--pull] [--inflight K] [--rate HZ] ADDRESS...\n",argv[0]);
        return 2;
    }
    SDL_SetHint(SDL_HINT_VIDEODRIVER,"wayland");
    if(SDL_Init(SDL_INIT_VIDEO)!=0){std::fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    auto* window=SDL_CreateWindow("omarchy-xr-window-probe",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    auto context=window?SDL_GL_CreateContext(window):nullptr;
    if(!context){std::fprintf(stderr,"GL context: %s\n",SDL_GetError());return 1;}
    WindowCaptureHub hub;
    std::string error;
    if(!hub.connect(error)){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
    std::fprintf(stderr,"toplevel export v%u, shared device %s\n",hub.version(),hub.device()?"yes":"no");
    configure(targets,hub);
    double reportAt=hub.now()+1,last=hub.now(),totalFrames=0;
    const double start=hub.now();
    while(hub.now()-start<seconds){
        for(SDL_Event e;SDL_PollEvent(&e);){}
        hub.pump();
        const double t=hub.now()-start;
        const bool visible=hide<0 || t<hide || t>=hide+1;
        for(auto& w:targets){
            w.capture->setDemand(visible,16384,16384);
            if(!w.capture->update(w.frame))continue;
            if(!visible)std::printf("{\"t\":%.2f,\"addr\":\"%lx\",\"thumbnail\":\"%ux%u\",\"texture\":%u}\n",t,(unsigned long)w.address,w.frame.width,w.frame.height,w.frame.texture);
            else if(w.capture->requestToReadyMs()>=0)w.waits.push_back(w.capture->requestToReadyMs());
        }
        if(!hub.error().empty()){std::fprintf(stderr,"%s\n",hub.error().c_str());return 1;}
        if(hub.now()>=reportAt){report(hub.now()-start,targets,hub.now()-last);last=hub.now();reportAt+=1;}
        SDL_Delay(1);
    }
    std::printf("{\"summary\":true,\"seconds\":%.1f,\"windows\":[",hub.now()-start);
    for(size_t i=0;i<targets.size();++i){
        auto& c=*targets[i].capture;totalFrames+=c.frames();
        std::printf("%s{\"addr\":\"%lx\",\"avg_fps\":%.1f,\"requests\":%u,\"failures\":%u,\"transport\":\"%s\",\"alive\":%s}",i?",":"",
            (unsigned long)targets[i].address,c.frames()/(hub.now()-start),c.requests(),c.failures(),c.transport(),c.alive()?"true":"false");
    }
    std::printf("]}\n");
    targets.clear();
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    return totalFrames>0 ? 0 : 1;
}
