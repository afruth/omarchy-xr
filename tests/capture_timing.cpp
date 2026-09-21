// Opt-in live protocol probe: source must contain continuous motion.
#include "capture.hpp"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc,char** argv){
    if(argc!=4)return 2;
    if(SDL_Init(SDL_INIT_VIDEO))return 1;
    auto* window=SDL_CreateWindow("XR capture timing",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    if(!window || !SDL_GL_CreateContext(window))return 1;
    DesktopCapture capture;capture.setFrameRate(60);capture.setIncludeCursor(std::string(argv[2])=="cursor");
    if(!capture.connect() || !capture.select(argv[1])){std::cerr<<capture.error();return 1;}
    capture.setDemand(true,480,270);
    using Clock=std::chrono::steady_clock;
    auto start=Clock::now(),deadline=start,last=start;
    std::vector<double> gaps;CapturedFrame frame;unsigned frames=0;
    auto update=[&]{
        if(capture.update(frame)){
            const auto now=Clock::now();if(frames)gaps.push_back(std::chrono::duration<double,std::milli>(now-last).count());
            last=now;++frames;
        }
    };
    while(Clock::now()-start<std::chrono::seconds(4)){
        update();
        if(!capture.error().empty()){std::cerr<<capture.error();return 1;}
        deadline+=std::chrono::microseconds(16667);
        while(Clock::now()<deadline){
            if(std::string(argv[3])=="serviced")update();
            std::this_thread::sleep_until(std::min(deadline,Clock::now()+std::chrono::milliseconds(2)));
        }
    }
    std::sort(gaps.begin(),gaps.end());
    std::cout<<capture.transport()<<" "<<argv[2]<<" "<<argv[3]<<": "<<frames<<" frames / 4s, gap p95 "<<(gaps.empty()?0:gaps[gaps.size()*95/100])<<" ms\n";
}
