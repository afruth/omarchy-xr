#include "curvature.hpp"
#include "layout.hpp"
#include "spacing.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>

bool near(float a,float b) { return std::abs(a-b)<1e-4f; }
int main() {
    using namespace spatial;
    // Explicit angular placement stays fixed as safety radius/zoom change.
    for(float degrees:{0.f,9.f,90.f,180.f,270.f,360.f}) {
        Workspace workspace;workspace.degrees=degrees;
        for(float distance:{2.f,5.f,20.f}) {
            auto p=pose(2,0,2,8,distance,workspace,0);
            assert(std::abs(p.yaw+degrees*pi/180/4)<1e-5);
        }
        std::vector<PanelLayout> ring{{"a",0,0,1920,1080},{"b",1950,0,1920,1080},{"c",3900,0,1920,1080}};
        auto d=safeDistance(ring,2910,540,5820.f/900,5,workspace,30);
        assert(separated(ring,2910,540,5820.f/900,d,workspace,30));
        assert(d<100);
    }

    using namespace spatial;
    Workspace cylinder;cylinder.degrees=180;cylinder.follow=true;
    const auto cylinderPose=pose(2,1,2,8,5,cylinder,100);
    assert(!cylinderPose.spherical && cylinderPose.latitude==0);
    for(float x:{-1.f,0.f,1.f})for(float y:{-.6f,0.f,.6f}){
        auto v=vertex(cylinderPose,x,y);
        assert(near(v.y,1+y));
        const float radius=1/cylinderPose.surfaceBend;
        const float z=v.z-(radius-5);
        assert(std::abs(std::hypot(v.x,z)-radius)<1e-4);
    }
    const auto cylinderBounds=bounds(cylinderPose,2,1.2f);
    for(int i=0;i<=surfaceSegments(2,cylinderPose.surfaceBend);++i)
        for(int j=0;j<=verticalSegments(1.2f,cylinderPose);++j){
            auto v=vertex(cylinderPose,(float(i)/surfaceSegments(2,cylinderPose.surfaceBend)-.5f)*2,
                          (float(j)/verticalSegments(1.2f,cylinderPose)-.5f)*1.2f);
            assert(v.x>=cylinderBounds.low.x && v.x<=cylinderBounds.high.x);
            assert(v.y>=cylinderBounds.low.y && v.y<=cylinderBounds.high.y);
            assert(v.z>=cylinderBounds.low.z && v.z<=cylinderBounds.high.z);
        }
    // Gaps retain their layout arc width, regardless of geometry distance.
    // Old center-only stretching made these 30px gutters hundreds of pixels wide.
    for(float degrees:{30.f,90.f,180.f,270.f,360.f})for(float distance:{2.f,5.f,50.f}){
        Workspace wrap;wrap.follow=true;wrap.degrees=degrees;wrap.gap=30.f/900;
        const float span=4190.f/900,cx=2095.f/900;
        auto left=pose(1720.f/900-cx,0,3440.f/900,span,distance,wrap,0);
        auto right=pose(3830.f/900-cx,0,720.f/900,span,distance,wrap,0);
        const float a=-left.yaw+3440.f/1800*left.surfaceBend;
        const float b=-right.yaw-720.f/1800*right.surfaceBend;
        assert(std::abs((b-a)/left.surfaceBend*900-30)<.005);
        auto upper=pose(0,300.f/900,2,span,distance,wrap,0);
        auto lower=pose(0,-430.f/900,2,span,distance,wrap,0);
        const float topEdge=upper.center.y-600.f/1800;
        const float bottomEdge=lower.center.y+800.f/1800;
        assert(std::abs((topEdge-bottomEdge)*900-30)<.005);
        if(degrees==360){
            const float seam=2*pi/left.surfaceBend*900-4190;
            assert(std::abs(seam-30)<.005);
        }
    }
    std::vector<PanelLayout> closeRow{{"left",0,0,3440,1440},{"right",3470,0,720,1440}};
    Workspace following;following.degrees=90;following.follow=true;following.gap=30.f/900;
    assert(safeDistance(closeRow,2095,720,4190.f/900,5,following,30)==5);
    assert(separated(closeRow,2095,720,4190.f/900,5,following,30));
    // Vertical position and height do not bend or hit a pole, even at 360°.
    Workspace full;full.follow=true;full.degrees=360;
    std::vector<PanelLayout> tall{{"tower",0,-8000,1920,8192}};
    assert(safeDistance(tall,960,-3904,1920.f/900,5,full,30)==5);
    auto high=pose(1,20,2,8,5,full,100);
    assert(near(high.center.y,20));
    assert(near(vertex(high,.5f,10).y-vertex(high,.5f,-10).y,20));
    assert(near(vertex(high,.5f,10).x,vertex(high,.5f,-10).x));
    assert(near(vertex(high,.5f,10).z,vertex(high,.5f,-10).z));
    cylinder.degrees=0;
    assert(pose(2,1,2,8,5,cylinder,100).surfaceBend==0);
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
    for(float workspace:{0.f,50.f,100.f}) for(float curve:{0.f,60.f,100.f}) for(float gap:{1.f,24.f,200.f}) {
        std::vector<PanelLayout> panels{{"a",0,0,1920,1080,curve},{"b",1920+gap,0,800,1280,100-curve},{"c",0,1280+gap,1920,1080,curve}};
        const float span=2720+gap,cx=span/2,cy=(2360+gap)/2;
        const float d=safeDistance(panels,cx,cy,span/900,.3f,workspace,gap);
        assert(separated(panels,cx,cy,span/900,d,workspace,gap));
    }
    // Validate analytic bounds against dense points on strongly bent patches.
    for(float yaw:{-2.f,-.5f,0.f,1.f,2.f}) {
        Pose p{{2,1,-3},yaw,1.2f};auto box=bounds(p,2,1);
        for(int i=0;i<=1000;i++) for(float offset:{0.f,.01f}) {
            auto v=vertex(p,-1+2.f*i/1000,0,offset);
            assert(v.x>=box.low.x-1e-5&&v.x<=box.high.x+1e-5);
            assert(v.z>=box.low.z-1e-5&&v.z<=box.high.z+1e-5);
        }
    }
    char path[]="/tmp/omarchy-xr-layout-XXXXXX";
    int fd=mkstemp(path);assert(fd>=0);close(fd);
    { std::ofstream file(path);file<<"A 0 0 1920 1080\nB 1920 0 1920 1080 75\n"; }
    auto layout=readLayout(path);assert(layout.size()==2&&layout[0].curvature==0&&layout[1].curvature==75);
    assert(layout[0].brightness==100 && layout[1].brightness==100);
    {std::ofstream file(path);file<<"A 0 0 1920 1080 75 45\n";}
    assert(readLayout(path)[0].brightness==45);
    for(auto bad:{"nan","-1","101","25 extra","25 0","25 101","25 nan","25 50 extra"}) {
        {std::ofstream file(path);file<<"A 0 0 1920 1080 "<<bad<<'\n';}
        bool rejected=false;try {readLayout(path);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    }
    unlink(path);
    std::cout<<"Curvature: flat limits, tangent planes, eye reference, continuity, sweep bounds, legacy layouts passed\n";
}
