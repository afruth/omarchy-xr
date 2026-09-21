#pragma once
#include "spacing.hpp"
#include "targeting.hpp"
#include <optional>

namespace navigation {
// Calibrating heading must not change the rendered view on the first frame.
// Carry the old view in navigation, then ease it to the new target normally.
inline tracking::Quaternion preserveView(tracking::Quaternion previousView,tracking::Quaternion newBase){
    return tracking::multiply(tracking::conjugate(newBase),previousView);
}
inline float ease(float value,float target,float dt) {
    return value+(target-value)*(-std::expm1(-std::clamp(dt,0.f,.1f)/.10f));
}
inline float easeDistance(float value,float target,float dt) {
    return std::exp(ease(std::log(value),std::log(target),dt));
}
inline size_t centerPanel(const std::vector<PanelLayout>& panels,float cx,float cy) {
    size_t best=0;float closest=INFINITY;
    for(size_t i=0;i<panels.size();++i) {
        const auto& p=panels[i];const float dx=p.x+p.width/2-cx,dy=p.y+p.height/2-cy;
        const float score=dx*dx+dy*dy;
        if(score<closest){closest=score;best=i;}
    }
    return best;
}
inline float heightDistance(float height,float fov) {
    return height/900.f/2/std::tan(fov*spatial::pi/360.f)*1.04f;
}
inline targeting::Vec fitPanForHeight(const PanelLayout& p,const spatial::Pose& pose,tracking::Quaternion view,float fov) {
    // Fit vertical projected bounds in the user's current viewing frame.
    // Keep head calibration and rotation unchanged; move the workspace smoothly.
    float depth=.1f;const float tangent=std::tan(fov*spatial::pi/360)/1.04f;
    const int segments=spatial::surfaceSegments(p.width/900,pose.surfaceBend);
    const int rows=spatial::verticalSegments(p.height/900,pose);
    for(int i=0;i<=segments;++i)for(int j=0;j<=rows;++j){
        const float y=(float(j)/rows-.5f)*p.height/900;
        const auto relative=targeting::sub(spatial::vertex(pose,(float(i)/segments-.5f)*p.width/900,y),pose.center);
        const auto eye=targeting::rotate(view,relative);
        depth=std::max(depth,std::abs(eye.y)/tangent+eye.z+.01f);
    }
    const auto desiredCenter=targeting::rotate(tracking::conjugate(view),{0,0,-depth});
    return targeting::sub(desiredCenter,pose.center);
}
inline tracking::Quaternion easeRotation(tracking::Quaternion value,tracking::Quaternion target,float dt){
    if(value.w*target.w+value.x*target.x+value.y*target.y+value.z*target.z<0)
        target={-target.w,-target.x,-target.y,-target.z};
    const float a=-std::expm1(-std::clamp(dt,0.f,.1f)/.10f);
    tracking::Quaternion q{value.w+(target.w-value.w)*a,value.x+(target.x-value.x)*a,
        value.y+(target.y-value.y)*a,value.z+(target.z-value.z)*a};
    const double n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
    return {q.w/n,q.x/n,q.y/n,q.z/n};
}
// Navigation may rotate around gravity only. Never bake tracked head tilt into
// the workspace: returning the head upright must restore an upright horizon.
inline tracking::Quaternion headingOnly(tracking::Quaternion view){
    const auto forward=targeting::rotate(tracking::conjugate(view),{0,0,-1});
    const double heading=std::hypot(forward.x,forward.z)>1e-6 ? std::atan2(forward.x,-forward.z) : 0;
    return {std::cos(heading/2),0,std::sin(heading/2),0};
}
inline tracking::Quaternion frontOrientation(const spatial::Pose& pose){
    const tracking::Quaternion yaw{std::cos(pose.yaw/2),0,-std::sin(pose.yaw/2),0};
    const tracking::Quaternion pitch{std::cos(pose.latitude/2),-std::sin(pose.latitude/2),0,0};
    return tracking::multiply(pitch,yaw);
}
// Perpendicular depth, not radial distance: lateral panning must not zoom out.
inline float viewingDistance(const spatial::Pose& pose,targeting::Vec pan){
    const auto offset=targeting::add(pose.center,pan);
    return std::max(.3f,-targeting::rotate(frontOrientation(pose),offset).z);
}
inline float overviewDepth(const std::vector<PanelLayout>& panels,float cx,float cy,float span,
                           float distance,spatial::Workspace workspace,float fov,float aspect){
    const float tangent=std::tan(fov*spatial::pi/360.f)/1.12f;
    float depth=1;
    for(const auto& p:panels){
        const auto pose=spatial::pose((p.x+p.width/2-cx)/900,-(p.y+p.height/2-cy)/900,p.width/900,span,distance,workspace,p.curvature);
        const auto b=spatial::bounds(pose,p.width/900,p.height/900);
        depth=std::max(depth,distance+b.high.z+std::max(
            std::max(std::abs(b.low.y),std::abs(b.high.y))/tangent,
            std::max(std::abs(b.low.x),std::abs(b.high.x))/(tangent*aspect)));
    }
    return depth;
}
inline float zoomDepth(float depth,float amount,float maximum){
    return std::clamp(depth*std::exp(std::clamp(-amount,-20.f,20.f)),.3f,std::max(.3f,maximum));
}
struct FrontFocus {tracking::Quaternion rotation;targeting::Vec pan;};
inline FrontFocus frontFocus(const spatial::Pose& pose,tracking::Quaternion anchor,float depth){
    const auto front=frontOrientation(pose);
    return {tracking::multiply(tracking::conjugate(headingOnly(anchor)),front),
        targeting::sub(targeting::rotate(tracking::conjugate(front),{0,0,-depth}),pose.center)};
}
inline float frontHeightDistance(const PanelLayout& p,const spatial::Pose& pose,float fov){
    const auto front=frontOrientation(pose);
    const auto pan=fitPanForHeight(p,pose,front,fov);
    return -targeting::rotate(front,targeting::add(pose.center,pan)).z;
}
// Surface-space pan stays at the same depth and faces the local tangent.
inline FrontFocus panFocus(const spatial::Pose& pose,tracking::Quaternion anchor,float depth,float x,float y){
    auto local=pose;local.center=spatial::vertex(pose,x,y);
    local.yaw-=x*pose.surfaceBend;
    if(pose.spherical)local.latitude+=y*pose.surfaceBend;
    return frontFocus(local,anchor,depth);
}
// Head-directed hit UV (top-left) to panFocus meters from the panel center.
inline targeting::Vec gazeFocus(const PanelLayout& panel,const targeting::Hit& hit){
    return {(hit.u-.5f)*panel.width/900,(.5f-hit.v)*panel.height/900,0};
}
inline float visibleArc(float depth,float halfFov,float bend){
    if(bend<=1e-6f)return depth*std::tan(halfFov);
    const float r=1/bend,a=r-depth;
    const float disc=r*r-a*a*std::sin(halfFov)*std::sin(halfFov);
    if(disc<=0)return r*spatial::pi/2;
    const float t=-a*std::cos(halfFov)+std::sqrt(disc);
    return r*std::atan2(t*std::sin(halfFov),a+t*std::cos(halfFov));
}
struct PanLimits {float x,y;};
inline PanLimits panLimits(const PanelLayout& p,const spatial::Pose& pose,float depth,float fov,float aspect){
    const float half=fov*spatial::pi/360;
    const float vx=visibleArc(depth,std::atan(std::tan(half)*aspect),pose.surfaceBend);
    const float vy=visibleArc(depth,half,pose.spherical?pose.surfaceBend:0);
    const float buffer=64.f/900;
    auto limit=[&](float size,float view){return size>view ? size-view+buffer : 0.f;};
    return {limit(p.width/1800,vx),limit(p.height/1800,vy)};
}
inline targeting::Vec applyPanLimits(const PanelLayout& p,const spatial::Pose& pose,float depth,float fov,float aspect,targeting::Vec focus,bool onPanelGaze){
    if(onPanelGaze)return focus;
    const auto limits=panLimits(p,pose,depth,fov,aspect);
    return {std::clamp(focus.x,-limits.x,limits.x),std::clamp(focus.y,-limits.y,limits.y),0};
}
inline bool lockZoomGaze(std::optional<targeting::Hit>& locked,const std::optional<targeting::Hit>& current){
    if(locked)return true;
    if(!current)return false;
    locked=current;
    return true;
}
// Conservative projected bounds include both workspace and monitor curvature.
inline bool fits(const std::vector<PanelLayout>& panels,float cx,float cy,float span,
                 float distance,spatial::Workspace workspace,float fov,float aspect) {
    const float vertical=std::tan(fov*spatial::pi/360.f)/1.06f;
    for(const auto& p:panels) {
        const auto pose=spatial::pose((p.x+p.width/2-cx)/900,-(p.y+p.height/2-cy)/900,p.width/900,span,distance,workspace,p.curvature);
        const auto b=spatial::bounds(pose,p.width/900,p.height/900);
        const float near=-b.high.z;
        if(near<=.1f || std::max(std::abs(b.low.x),std::abs(b.high.x))>near*vertical*aspect ||
           std::max(std::abs(b.low.y),std::abs(b.high.y))>near*vertical)return false;
    }
    return true;
}
}
