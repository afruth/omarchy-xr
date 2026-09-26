#include "notification_space.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace notifications::space;
Scene sceneFor(float sweep=0,float curve=0,float zoom=0,int count=3){
    Scene s;s.tanV=std::tan(28*spatial::pi/360);s.tanH=s.tanV*16/9;s.eye={0,0,-zoom};s.depth=5-zoom;
    std::vector<PanelLayout> panels;
    for(int i=0;i<count;i++)panels.push_back({std::to_string(i),float(i*1950),0,1920,1080,curve,100});
    spatial::Workspace workspace;workspace.degrees=sweep;workspace.follow=sweep>0;
    s.monitors(panels,(count*1950-30)/2.f,540,(count*1950-30)/900.f,5,workspace);return s;
}
void placement(){
    for(float sweep:{0.f,120.f,240.f,360.f})for(float curve:{0.f,100.f})for(float zoom:{-8.f,0.f,2.f,3.5f}){
        auto s=sceneFor(sweep,curve,zoom);Floater f;
        for(int i=0;i<240;++i)f.update(s,1.45f,.50f,i/60.);
        assert(f.safe);assert(clear(s,f.position,1.45f,.50f));
        assert(targeting::finite(f.position));
        if(!f.onscreen)assert(f.cueOpacity>.99f);
    }
    auto s=sceneFor(0,0,3.5f,1);Floater f;f.update(s,1.45f,.5f,0);
    assert(!f.onscreen); // close zoom is allowed to leave the card outside the frame
    assert(!clear(s,{0,0,-4},1,.3f)); // directly between eye and monitor is forbidden
    assert(!clear(s,{0,0,-10},1,.3f)); // behind a monitor is not "beside" either
    assert(clear(s,{5,5,-5},1,.3f));
}
void closestBerth(){
    for(bool tall:{false,true}){
        Scene s;
        const float width=tall?900:8000,height=tall?5000:1080;
        std::vector<PanelLayout> panels{{"screen",0,0,width,height}};
        s.monitors(panels,width/2,height/2,width/900,5,{});
        Floater f;f.update(s,1.45f,.5f,0);
        assert(f.safe);
        if(tall){assert(f.position.x>1.2f);assert(f.position.y<.3f);}
        else {assert(std::abs(f.position.x)<.01f);assert(f.position.y>.85f);}
    }
}
void motion(){
    for(float sweep:{0.f,120.f,360.f}){
        auto s=sceneFor(sweep);Floater f;
        for(int i=0;i<120;++i)f.update(s,1.45f,.5f,i/60.);
        auto previous=f.position;int hidden=0;
        for(int i=120;i<1500;++i){
            const float yaw=std::min(75.f,(i-120)*.5f);
            s.view=tracking::conjugate(tracking::orientation(0,0,yaw));
            f.update(s,1.45f,.5f,i/60.);
            assert(length(sub(f.position,previous))<2.21f/60);
            assert(f.speed()<=2.201f);
            if(f.phase()==2){
                assert(length(sub(f.position,f.travelOrigin()))>outerRadius(s)+.5f);
                assert(f.behind);
            }
            if(f.safe && !clear(s,f.position,1.45f,.5f))assert(f.behind);
            if(!f.safe){++hidden;assert(f.opacity==0);}
            previous=f.position;
        }
        assert(f.safe);assert(length(sub(f.position,f.target()))<.03f);
        std::cout<<"sweep "<<sweep<<": unsafe transition frames "<<hidden<<" / 1380\n";
    }
}
void reading(){
    auto s=sceneFor();Floater f;f.update(s,1.45f,.5f,0);
    const auto position=f.position;const auto direction=normalize(sub(position,s.eye));
    const float yaw=-std::atan2(direction.x,-direction.z)*180/spatial::pi;
    const float pitch=-std::asin(direction.y)*180/spatial::pi;
    s.view=tracking::conjugate(tracking::orientation(0,pitch,yaw));
    for(int i=1;i<300;++i)f.update(s,1.45f,.5f,i/60.);
    assert(length(sub(f.position,position))<.001f);assert(f.onscreen);assert(f.cueOpacity==0);
}
void changingLayout(){
    auto s=sceneFor();Floater f;
    for(int i=0;i<1800;++i){
        if(i==120)s.view=tracking::conjugate(tracking::orientation(0,0,50));
        if(i==300){auto changed=sceneFor(120,100,1.5f,5);changed.view=s.view;s=changed;}
        f.update(s,1.45f,.5f,i/60.);
        assert(targeting::finite(f.position));
        if(f.safe && !clear(s,f.position,1.45f,.5f))assert(f.behind);
        if(!f.safe)assert(f.opacity==0);
    }
    assert(f.safe && f.phase()==0);
}
void refreshRates(){
    Vec reference{};
    for(int rate:{30,60,120}){
        auto s=sceneFor();Floater f;
        for(int i=0;i<rate*25;++i){
            if(i==rate*2)s.view=tracking::conjugate(tracking::orientation(0,0,35));
            f.update(s,1.45f,.5f,double(i)/rate);
        }
        assert(f.safe && f.phase()==0);
        if(rate==30)reference=f.position;else assert(length(sub(f.position,reference))<.03f);
    }
}
void indicators(){
    Scene s;
    for(auto position:{Vec{8,0,-4},Vec{-8,0,-4},Vec{0,8,-4},Vec{0,-8,-4},Vec{8,0,4},Vec{-8,0,4}}){
        const auto c=cue(s,position);const auto p=s.camera(c.center);
        assert(std::isfinite(c.angle) && targeting::finite(c.center));
        const float x=p.x/(-p.z*s.tanH),y=p.y/(-p.z*s.tanV);
        assert(std::abs(std::max(std::abs(x),std::abs(y))-.90f)<.001f);
        if(position.x!=0)assert((x>0)==(position.x>0));
        if(position.y!=0)assert((y>0)==(position.y>0));
    }
}
void cylinderAlias(){
    std::vector<PanelLayout> panels{{"a",0,0,1920,1080,40},{"b",1950,120,2560,1440,0}};
    spatial::Workspace workspace;workspace.degrees=120;workspace.follow=true;
    Scene a,b;a.monitors(panels,2240,660,4480/900.f,5,workspace);b.tessellate(panels,Cylinder{2240,660,4480/900.f,5,workspace});
    assert(!a.surfaces.empty() && a.surfaces.size()==b.surfaces.size() && a.ceiling==b.ceiling);
    for(size_t i=0;i<a.surfaces.size();++i)for(int k=0;k<4;++k){
        const auto u=a.surfaces[i][k],v=b.surfaces[i][k];assert(u.x==v.x && u.y==v.y && u.z==v.z);
    }
}
// Inside the canvas ring (maxRadius, lowerBand): the berth is below the view centre and in view, and no
// frame of a 75° turn takes the card beyond the ring reach or behind the view. With the 360° wall fed as
// occluders nothing is clear, so the cheapest in-band point stands in (never overhead).
Scene ringScene(bool wall){
    Scene s;s.tanV=std::tan(28*spatial::pi/360);s.tanH=s.tanV*16/9;s.eye={0,0,0};s.depth=2.1f;s.maxRadius=2.1f;s.lowerBand=true;
    if(!wall)return s;
    const float gap=60;std::vector<PanelLayout> panels;
    for(int i=0;i<30;++i)panels.push_back({std::to_string(i),float(i%10)*1340,float(i/10-1)*780-360,1280,720,0,100});
    spatial::Workspace workspace;workspace.degrees=360;workspace.follow=true;workspace.gap=gap/900;
    s.tessellate(panels,Cylinder{0,0,2*spatial::pi*2.4f-gap/900,2.4f,workspace});
    assert(!s.surfaces.empty());
    return s;
}
void inBand(const Scene& s,const Floater& f){
    assert(centerInView(s,f.position));
    const auto p=s.camera(f.position);const float pitch=std::atan2(p.y,-p.z);
    assert(pitch<=0 && pitch>=-std::atan(s.tanV*.9f));
}
void insideRing(bool wall){
    auto s=ringScene(wall);Floater f;
    for(int i=0;i<240;++i)f.update(s,1.45f,.5f,i/60.);
    if(!wall)assert(f.safe && f.onscreen);
    inBand(s,f);assert(f.position.y<0 && length(f.position)<=2.1f+1e-3f);
    auto previous=f.position;
    for(int i=240;i<1620;++i){
        const float yaw=std::min(75.f,(i-240)*.5f);
        s.view=tracking::conjugate(tracking::orientation(0,0,yaw));
        f.update(s,1.45f,.5f,i/60.);
        assert(targeting::finite(f.position) && !f.behind);
        assert(length(sub(f.position,s.eye))<=2.1f+1e-3f);
        assert(length(sub(f.position,previous))<2.21f/60 && f.speed()<=2.201f);
        previous=f.position;
    }
    if(!wall)assert(f.safe);
    inBand(s,f);
}
int main(){cylinderAlias();placement();closestBerth();motion();reading();changingLayout();refreshRates();indicators();insideRing(false);insideRing(true);std::cout<<"Spatial placement, smooth motion, reading lock, directional cues and the inside-ring band passed\n";}
