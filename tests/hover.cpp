#include "hover.hpp"
#include <cassert>
#include <iostream>
int main(){
    using namespace interaction;
    Hover hover;
    hover.update("a",.5,.5,.5,.016);assert(!hover.near);
    hover.update("a",.8,.8,.5,.016);assert(hover.near && hover.u>.5 && hover.u<.8);
    hover.update("a",.7,.8,.5,.016);assert(hover.near); // hysteresis
    hover.update("a",.6,.8,.5,.016);assert(!hover.near);
    hover.update("b",.7,.1,.2,.016);assert(!hover.near && hover.u==.1f); // no cross-monitor interpolation
    hover.update("b",1,.1,.2,.016);assert(hover.near);hover.clear();assert(!hover.near && hover.output.empty());
    // A 120 Hz step settles within one frame with the low-latency filter.
    Hover fast;fast.update("a",1,.2,.5,1.f/120);
    for(int i=0;i<1;++i)fast.update("a",1,.8,.5,1.f/120);
    assert(std::abs(fast.u-.8f)<.6f*.05f);
    const auto c=content(2,2,1920,1080);assert(std::abs(c.height-1.125)<1e-5);
    targeting::Hit hit;hit.u=.75;hit.v=.5;
    auto uv=contentUV(hit,2,2,c);assert(uv && uv->first==.75f && uv->second==.5f);
    hit.v=.1;assert(!contentUV(hit,2,2,c)); // letterbox does not move cursor
    spatial::Pose pose{{0,0,-2.5},0,0};
    const auto native=content(1920.f/900,1080.f/900,1920,1080);
    float near=pixelDensity(pose,0,0,native,1080,{}, {},1080,28);
    pose.center.z=-8;
    float far=pixelDensity(pose,0,0,native,1080,{}, {},1080,28);
    assert(near>.78 && far<.62);
    pose.center.z=1;assert(pixelDensity(pose,0,0,native,1080,{}, {},1080,28)==0);
    std::cout<<"Hover: readable scale, near/far hysteresis, smoothing and content mapping passed\n";
}
