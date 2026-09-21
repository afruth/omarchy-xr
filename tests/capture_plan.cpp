#include "capture_plan.hpp"
#include "pixels.hpp"
#include <cassert>
#include <iostream>
int main(){
    PanelLayout p{"test",0,0,1800,900,0};
    auto plan=[&](float x,float z){return adaptive::project(p,{{x,0,z},0,0},{},{},800,600,60,0);};
    assert(plan(0,-4).visible);
    assert(plan(4.1,-4).visible); // just outside viewport, within 10% prefetch margin
    assert(!plan(4.6,-4).visible);
    assert(!plan(0,4).visible);
    assert(plan(0,-12).scale<plan(0,-4).scale);
    assert(plan(0,-.15).visible); // screen surrounding viewport isn't rejected by corner tests
    adaptive::Quality quality;
    assert(quality.update(.1,1)==1);
    assert(quality.update(.1,1.2)==1);
    assert(quality.update(.1,1.4)==.125);
    assert(quality.update(.9,1.41)==1);
    const uint32_t pixels[]{0x00102030,0x00304050,0xdeadbeef,0x00506070,0x00708090,0xdeadbeef};
    uint8_t result[4]{};
    copyRgbaScaled(pixels,result,2,2,12,false,true,1,1);
    assert(result[0]==0x40 && result[1]==0x50 && result[2]==0x60 && result[3]==255);
    copyRgbaScaled(pixels,result,2,2,12,true,false,1,1);
    assert(result[0]==0x60 && result[2]==0x40);
    std::cout<<"Adaptive capture: frustum margin, offscreen rejection, density, LOD hysteresis and scaled pixels passed\n";
}
