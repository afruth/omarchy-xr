#pragma once
#include "targeting.hpp"
#include <array>
#include <vector>

namespace notifications::space {
using Vec=targeting::Vec;
using targeting::add;using targeting::sub;using targeting::mul;using targeting::dot;
using targeting::cross;using targeting::normalize;using targeting::rotate;
inline float length(Vec v){return std::sqrt(dot(v,v));}
inline Vec limited(Vec v,float maximum){const float n=length(v);return n>maximum?mul(v,maximum/n):v;}
struct Basis {Vec right,up,forward;};
inline Basis facing(Vec center,Vec eye){
    const auto f=normalize(sub(center,eye));
    auto r=normalize(cross(f,{0,1,0}));if(length(r)<.01f)r={1,0,0};
    return {r,normalize(cross(r,f)),f};
}
struct Scene {
    tracking::Quaternion view;
    Vec eye{0,0,0};
    float tanH=.45f,tanV=.25f,ipd=.064f,depth=5;
    std::vector<std::array<Vec,4>> surfaces;
    float ceiling=0;
    void monitors(const std::vector<PanelLayout>& panels,float cx,float cy,float span,float distance,spatial::Workspace workspace){
        surfaces.clear();ceiling=eye.y;
        for(const auto& p:panels){
            const float w=p.width/900,h=p.height/900;
            const auto pose=spatial::pose((p.x+p.width/2-cx)/900,-(p.y+p.height/2-cy)/900,w,span,distance,workspace,p.curvature);
            const int columns=spatial::surfaceSegments(w,pose.surfaceBend),rows=spatial::verticalSegments(h,pose);
            for(int i=0;i<columns;++i)for(int j=0;j<rows;++j){
                const float x0=w*(float(i)/columns-.5f),x1=w*(float(i+1)/columns-.5f);
                const float y0=h*(float(j)/rows-.5f),y1=h*(float(j+1)/rows-.5f);
                std::array<Vec,4> quad{spatial::vertex(pose,x0,y0),spatial::vertex(pose,x1,y0),spatial::vertex(pose,x1,y1),spatial::vertex(pose,x0,y1)};
                for(auto v:quad)ceiling=std::max(ceiling,v.y);
                surfaces.push_back(quad);
            }
        }
    }
    Vec camera(Vec world) const{return rotate(view,sub(world,eye));}
    Vec world(Vec cameraPoint) const{return add(eye,rotate(tracking::conjugate(view),cameraPoint));}
};
// Ray from the rendered view centre, intersecting the rounded card face.
inline float gazeHit(const Scene& scene,Vec center,float width,float height) {
    const auto b=facing(center,scene.eye);
    const auto ray=rotate(tracking::conjugate(scene.view),{0,0,-1});
    const float denom=dot(ray,b.forward);
    if(denom<=.001f)return -1;
    const float distance=dot(sub(center,scene.eye),b.forward)/denom;
    if(distance<=0)return -1;
    const auto hit=sub(add(scene.eye,mul(ray,distance)),center);
    const float x=std::abs(dot(hit,b.right)),y=std::abs(dot(hit,b.up));
    if(x>width/2 || y>height/2)return -1;
    const float radius=std::min(.055f,height*.15f);
    const float dx=std::max(0.f,x-width/2+radius),dy=std::max(0.f,y-height/2+radius);
    return dx*dx+dy*dy<=radius*radius?distance:-1;
}
// Clip each rendered monitor facet against the pyramid from an eye through the
// entire notification (including its bezel). Any surviving area is forbidden,
// regardless of depth: the card must sit beside screens, never cover one.
inline bool intersects(std::array<Vec,4> quad,Vec eye,const std::array<Vec,5>& planes){
    std::array<Vec,16> polygon{},next{};int count=4;
    for(int i=0;i<4;++i)polygon[i]=sub(quad[i],eye);
    for(auto plane:planes){
        int out=0;
        for(int i=0;i<count;++i){
            const auto a=polygon[i],b=polygon[(i+1)%count];const float da=dot(a,plane),db=dot(b,plane);
            if(da>=0)next[out++]=a;
            if((da>=0)!=(db>=0))next[out++]=add(a,mul(sub(b,a),da/(da-db)));
        }
        if(out==0)return false;
        count=out;polygon=next;
    }
    return true;
}
inline bool clear(const Scene& scene,Vec center,float width,float height,float margin=.09f){
    if(!targeting::finite(center) || length(sub(center,scene.eye))<.35f)return false;
    const auto basis=facing(center,scene.eye);
    const auto eyeRight=rotate(tracking::conjugate(scene.view),{1,0,0});
    for(float sign:{-1.f,1.f}){
        const auto eye=add(scene.eye,mul(eyeRight,sign*scene.ipd/2));
        const auto delta=sub(center,eye);const float depth=dot(delta,basis.forward);
        if(depth<.2f)return false;
        const float left=(dot(delta,basis.right)-width/2-margin)/depth,right=(dot(delta,basis.right)+width/2+margin)/depth;
        const float bottom=(dot(delta,basis.up)-height/2-margin)/depth,top=(dot(delta,basis.up)+height/2+margin)/depth;
        const std::array<Vec,5> planes{basis.forward,sub(basis.right,mul(basis.forward,left)),
            sub(mul(basis.forward,right),basis.right),sub(basis.up,mul(basis.forward,bottom)),sub(mul(basis.forward,top),basis.up)};
        for(const auto& surface:scene.surfaces)if(intersects(surface,eye,planes))return false;
    }
    return true;
}
inline bool inFrame(const Scene& scene,Vec center,float width,float height){
    const auto b=facing(center,scene.eye);
    for(float x:{-1.f,1.f})for(float y:{-1.f,1.f}){
        auto p=scene.camera(add(center,add(mul(b.right,x*width/2),mul(b.up,y*height/2))));
        if(p.z>=-.1f || std::abs(p.x)+scene.ipd/2>=-p.z*scene.tanH*.94f || std::abs(p.y)>=-p.z*scene.tanV*.94f)return false;
    }
    return true;
}
inline bool centerInView(const Scene& scene,Vec center){
    const auto p=scene.camera(center);
    return p.z<-.1f && std::abs(p.x)+scene.ipd/2<-p.z*scene.tanH*.94f && std::abs(p.y)<-p.z*scene.tanV*.94f;
}
struct Cue {Vec center,right,up;float angle=0;};
inline Cue cue(const Scene& scene,Vec center){
    const auto p=scene.camera(center);
    // atan2 retains the correct left/right direction for cards behind the head.
    float x=std::atan2(p.x,-p.z)/std::atan(scene.tanH);
    float y=std::atan2(p.y,std::hypot(p.x,p.z))/std::atan(scene.tanV);
    if(std::abs(x)+std::abs(y)<.001f)x=1;
    const float scale=.90f/std::max(std::abs(x),std::abs(y));x*=scale;y*=scale;
    const float depth=std::max(.8f,length(sub(center,scene.eye)));
    const auto inv=tracking::conjugate(scene.view);
    return {scene.world({x*scene.tanH*depth,y*scene.tanV*depth,-depth}),
        rotate(inv,{1,0,0}),rotate(inv,{0,1,0}),std::atan2(y*scene.tanV,x*scene.tanH)};
}
inline float outerRadius(const Scene& scene){
    float radius=0;for(const auto& facet:scene.surfaces)for(auto p:facet)radius=std::max(radius,length(sub(p,scene.eye)));
    return radius;
}
inline Vec orbit(Vec start,Vec axis,float angle){
    return add(add(mul(start,std::cos(angle)),mul(cross(axis,start),std::sin(angle))),mul(axis,dot(axis,start)*(1-std::cos(angle))));
}
class Floater {
    Vec destination{},heading{},observedHeading{},observedEye{},routeEye{},startDirection{},endDirection{},axis{};
    double lastTime=-1,retargetAt=0,movedAt=0;
    bool initialized=false;
    int leg=0;
    float radius=0,routeRadius=0,angle=0,totalAngle=0,velocity=0,lastSpeed=0;
    Vec findPlace(const Scene& scene,float width,float height) const {
        const float depth=std::max(.85f,scene.depth*.97f);
        Vec best{};float score=1e9f;
        // At the monitor's depth, choose the physically closest free berth:
        // above or to either side. Right wins a symmetrical tie. No bottom slots.
        for(int up=0;up<=24;++up)for(int side=0;side<=24;++side)for(float sign:{1.f,-1.f}){
            if(side==0 && sign<0)continue;
            const Vec cameraPoint{sign*depth*std::tan(side*.045f),depth*std::tan(.025f+up*.045f),-depth};
            if(std::abs(cameraPoint.x)<depth*scene.tanH*.25f && cameraPoint.y<depth*scene.tanV*.50f)continue;
            const float cost=dot(cameraPoint,cameraPoint);
            if(cost>=score)continue;
            const auto candidate=scene.world(cameraPoint);
            if(clear(scene,candidate,width,height,.16f)){best=candidate;score=cost;}
        }
        if(score<1e9f)return best;
        return add(scene.eye,{0,std::max(depth,scene.ceiling-scene.eye.y+height+1),-depth});
    }
    void route(const Scene& scene,Vec target,float width,float height){
        destination=target;routeEye=scene.eye;
        startDirection=normalize(sub(position,routeEye));endDirection=normalize(sub(target,routeEye));
        radius=length(sub(position,routeEye));
        routeRadius=std::max({radius,length(sub(target,routeEye)),outerRadius(scene)+std::hypot(width,height)/2+.45f});
        axis=normalize(cross(startDirection,endDirection));
        if(length(axis)<.01f)axis=normalize(cross(startDirection,{0,1,0}));
        if(length(axis)<.01f)axis={1,0,0};
        totalAngle=std::acos(std::clamp(dot(startDirection,endDirection),-1.f,1.f));
        angle=velocity=0;leg=1;
    }
    void spring(float& value,float target,float dt,float scale=1){
        const float acceleration=std::clamp(12.25f*(target-value)-7*velocity,-5.f/scale,5.f/scale);
        velocity=std::clamp(velocity+acceleration*dt,-2.2f/scale,2.2f/scale);
        value+=velocity*dt;
    }
    void advance(float dt,bool hold){
        if(leg && hold){
            velocity*=std::exp(-9*dt);
            if(leg==2){angle+=velocity*dt;position=add(routeEye,mul(orbit(startDirection,axis,angle),routeRadius));}
            else {radius+=velocity*dt;position=add(routeEye,mul(leg==1?startDirection:endDirection,radius));}
            if(std::abs(velocity)*(leg==2?routeRadius:1)<.02f){leg=0;velocity=0;destination=position;}
            return;
        }
        if(leg==1){
            spring(radius,routeRadius,dt);position=add(routeEye,mul(startDirection,radius));
            if(std::abs(radius-routeRadius)<.003f && std::abs(velocity)<.02f){radius=routeRadius;velocity=0;leg=2;}
        }else if(leg==2){
            spring(angle,totalAngle,dt,routeRadius);
            position=add(routeEye,mul(orbit(startDirection,axis,angle),routeRadius));
            if(std::abs(angle-totalAngle)*routeRadius<.003f && std::abs(velocity)*routeRadius<.02f){angle=totalAngle;velocity=0;leg=3;}
        }else if(leg==3){
            const float endRadius=length(sub(destination,routeEye));
            spring(radius,endRadius,dt);position=add(routeEye,mul(endDirection,radius));
            if(std::abs(radius-endRadius)<.003f && std::abs(velocity)<.02f){position=destination;velocity=0;leg=0;}
        }
    }
public:
    Vec position{};
    bool safe=false,onscreen=false,behind=false;
    float cueOpacity=0,opacity=0;
    void reset(){initialized=false;lastTime=-1;velocity=0;leg=0;opacity=cueOpacity=0;}
    Vec target()const{return destination;}
    float speed()const{return lastSpeed;}
    int phase()const{return leg;}
    float travelRadius()const{return routeRadius;}
    Vec travelOrigin()const{return routeEye;}
    void update(const Scene& scene,float width,float height,double now,bool cardReading=false){
        const float dt=lastTime<0?0:float(std::clamp(now-lastTime,0.,.1));lastTime=now;
        const auto forward=rotate(tracking::conjugate(scene.view),{0,0,-1});
        if(!initialized){position=findPlace(scene,width,height);destination=position;heading=observedHeading=forward;observedEye=scene.eye;retargetAt=now+.3;movedAt=now;initialized=true;}
        if(dot(forward,observedHeading)<std::cos(.025f) || length(sub(scene.eye,observedEye))>.03f){observedHeading=forward;observedEye=scene.eye;movedAt=now;}
        const auto p=scene.camera(position);
        const bool reading=cardReading || (p.z<0 && std::abs(p.x)<(-p.z)*.045f && std::abs(p.y)<(-p.z)*.045f);
        const bool turned=dot(forward,heading)<std::cos(.20f);
        const float targetDepth=-scene.camera(destination).z;
        const bool displaced=std::abs(targetDepth-scene.depth*.97f)>std::max(.4f,scene.depth*.18f);
        const bool settled=now-movedAt>.28;
        const bool destinationBlocked=!clear(scene,destination,width,height,.12f);
        const bool routeInvalid=leg && (length(sub(scene.eye,routeEye))>.25f || routeRadius<outerRadius(scene)+std::hypot(width,height)/2+.35f || destinationBlocked);
        if(now>=retargetAt && settled && (routeInvalid || (!leg && (destinationBlocked || (!reading && (turned || displaced)))))){
            auto target=findPlace(scene,width,height);
            if(length(sub(target,position))>.10f)route(scene,target,width,height);
            heading=forward;retargetAt=now+.4;
        }
        const auto previous=position;
        const int steps=std::max(1,int(std::ceil(dt*120)));const float h=dt/steps;
        const bool hold=reading && settled && clear(scene,position,width,height,.16f);
        for(int i=0;i<steps;++i)advance(h,hold);
        if(hold && !leg)heading=forward;
        lastSpeed=dt>0?length(sub(position,previous))/dt:0;
        const bool beside=clear(scene,position,width,height,.08f);
        behind=length(sub(position,scene.eye))>outerRadius(scene)+std::hypot(width,height)/2+.10f;
        safe=beside || behind;
        // A close-up card may extend past the view edges while its centre is
        // directly under the user's gaze. Do not point away from that card.
        onscreen=beside && centerInView(scene,position);
        // Retreat/orbit/approach go behind the furthest monitor. The normal
        // depth buffer occludes the card during that flight. Abrupt changes to
        // camera/layout are gated as well: never draw a card in front of a screen.
        opacity=safe?std::min(1.f,opacity+dt*4):0;
        const float wanted=onscreen?0:1;
        cueOpacity+=std::clamp(wanted-cueOpacity,-dt*5,dt*5);
    }
};
}
