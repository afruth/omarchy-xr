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
int main(){placement();closestBerth();motion();reading();changingLayout();refreshRates();indicators();std::cout<<"Spatial placement, smooth motion, reading lock and directional cues passed\n";}
