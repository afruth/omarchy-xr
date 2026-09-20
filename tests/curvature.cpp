#include "curvature.hpp"
#include "layout.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>

bool near(float a,float b) { return std::abs(a-b)<1e-4f; }
int main() {
    using namespace spatial;
    auto flat=pose(2,1,2,6,5,0,0);
    auto f=vertex(flat,.8f,.3f);
    assert(near(f.x,2.8f)&&near(f.y,1.3f)&&near(f.z,-5));
    auto wrapped=pose(2,1,2,6,5,100,0);
    assert(near(std::hypot(wrapped.center.x,wrapped.center.z),5));
    assert(near(wrapped.surfaceBend,0));
    auto a=vertex(wrapped,-1,0),b=vertex(wrapped,1,0),mid=vertex(wrapped,0,0);
    assert(near((a.x+b.x)/2,mid.x)&&near((a.z+b.z)/2,mid.z)); // flat at any workspace curvature
    auto curved=pose(0,0,2,6,5,0,100);
    a=vertex(curved,-1,0);b=vertex(curved,1,0);mid=vertex(curved,0,0);
    assert(a.z>mid.z&&b.z>mid.z&&near(a.x,-b.x));
    assert(near(std::hypot(a.x,a.z),5)); // camera-centered radius at full surface bend
    auto translated=pose(0,0,2,6,5,0,100,{3,4,7});
    auto moved=vertex(translated,1,0);
    assert(near(moved.x,b.x+3)&&near(moved.y,b.y+4)&&near(moved.z,b.z+7));
    auto tiny=vertex(pose(2,1,2,6,5,.00001f,.00001f),.8f,.3f);
    assert(near(tiny.x,f.x)&&near(tiny.z,f.z));
    auto wide=pose(500,0,100,1000,.3f,100,100);
    assert(std::abs(wide.yaw)<=5*pi/6+.001f);
    assert(wide.surfaceBend*100<=8*pi/9+.001f);
    char path[]="/tmp/omarchy-xr-layout-XXXXXX";
    int fd=mkstemp(path);assert(fd>=0);close(fd);
    { std::ofstream file(path);file<<"A 0 0 1920 1080\nB 1920 0 1920 1080 75\n"; }
    auto layout=readLayout(path);assert(layout.size()==2&&layout[0].curvature==0&&layout[1].curvature==75);
    for(auto bad:{"nan","-1","101","25 extra"}) {
        {std::ofstream file(path);file<<"A 0 0 1920 1080 "<<bad<<'\n';}
        bool rejected=false;try {readLayout(path);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    }
    unlink(path);
    std::cout<<"Curvature: flat limits, tangent planes, eye reference, continuity, sweep bounds, legacy layouts passed\n";
}
