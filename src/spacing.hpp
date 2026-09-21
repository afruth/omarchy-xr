#pragma once
#include "curvature.hpp"
#include "layout.hpp"
#include <limits>

namespace spatial {
struct Bounds { Point low, high; };
// Exact bounds of the continuous cylindrical patch, also bounding all triangles
// and the 0..0.01 depth layers used by the renderer. No vertex-sampling guess.
inline Bounds bounds(const Pose& p,float width,float height) {
    if(p.spherical){
        Bounds b{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
        const int nx=surfaceSegments(width,p.surfaceBend),ny=verticalSegments(height,p);
        for(int i=0;i<=nx;++i)for(int j=0;j<=ny;++j)for(float offset:{0.f,.01f}){
            auto v=vertex(p,(float(i)/nx-.5f)*width,(float(j)/ny-.5f)*height,offset);
            b.low.x=std::min(b.low.x,v.x);b.high.x=std::max(b.high.x,v.x);
            b.low.y=std::min(b.low.y,v.y);b.high.y=std::max(b.high.y,v.y);
            b.low.z=std::min(b.low.z,v.z);b.high.z=std::max(b.high.z,v.z);
        }
        return b;
    }
    Bounds b{{INFINITY,p.center.y-height/2,INFINITY},{-INFINITY,p.center.y+height/2,-INFINITY}};
    auto include=[&](float x) {
        for(float z:{0.f,.01f}) {
            auto v=vertex(p,x,0,z);
            b.low.x=std::min(b.low.x,v.x);b.high.x=std::max(b.high.x,v.x);
            b.low.z=std::min(b.low.z,v.z);b.high.z=std::max(b.high.z,v.z);
        }
    };
    include(-width/2);include(width/2);
    if(p.surfaceBend>0) {
        const float low=-width/2*p.surfaceBend-p.yaw,high=width/2*p.surfaceBend-p.yaw;
        for(int n=int(std::ceil(low/(pi/2)));n<=int(std::floor(high/(pi/2)));++n)
            include((n*pi/2+p.yaw)/p.surfaceBend);
    }
    return b;
}
inline float separation(const Bounds& a,const Bounds& b) {
    auto gap=[](float al,float ah,float bl,float bh){return std::max({bl-ah,al-bh,0.f});};
    const float x=gap(a.low.x,a.high.x,b.low.x,b.high.x),y=gap(a.low.y,a.high.y,b.low.y,b.high.y),z=gap(a.low.z,a.high.z,b.low.z,b.high.z);
    return std::sqrt(x*x+y*y+z*z);
}
inline bool separated(const std::vector<PanelLayout>& panels,float cx,float cy,float span,float distance,spatial::Workspace workspace,float gap) {
    if(workspace.follow){
        const float k=workspaceBend(span,distance,workspace);
        // Shared-cylinder patches occupy disjoint rectangles in their continuous
        // surface coordinates. World-axis AABBs overlap even for disjoint arcs;
        // using those boxes here used to inflate the ring and its gutters.
        auto intervalGap=[](float a,float aw,float b,float bw){return std::max({b-a-aw,a-b-bw,0.f});};
        for(size_t i=0;i<panels.size();++i)for(size_t j=0;j<i;++j){
            const auto& a=panels[i];const auto& b=panels[j];
            float dx=intervalGap(a.x,a.width,b.x,b.width);
            const float dy=intervalGap(a.y,a.height,b.y,b.height);
            if(k>0){
                const float period=2*pi/k*900;
                dx=std::min({dx,intervalGap(a.x,a.width,b.x-period,b.width),intervalGap(a.x,a.width,b.x+period,b.width)});
            }
            if(std::hypot(dx,dy)+.01f<gap)return false;
        }
        return true;
    }
    std::vector<Bounds> boxes;
    for(const auto& p:panels) {
        const float w=p.width/900,h=p.height/900;
        auto box=bounds(pose((p.x+p.width/2-cx)/900,-(p.y+p.height/2-cy)/900,w,span,distance,workspace,p.curvature),w,h);
        for(const auto& other:boxes) if(separation(box,other)+1e-5f<gap/900) return false;
        boxes.push_back(box);
    }
    return true;
}
inline float safeDistance(const std::vector<PanelLayout>& panels,float cx,float cy,float span,float desired,spatial::Workspace workspace,float gap) {
    workspace.gap=gap/900;
    if(workspace.follow && workspace.degrees>=0){
        if(!separated(panels,cx,cy,span,desired,workspace,gap))
            throw std::runtime_error("Workspace wrap would overlap monitors. Reduce wrap or rearrange the layout.");
        return desired;
    }
    // Legacy curvature may enlarge the radius; explicit following never stretches gutters.
    float distance=desired;
    for(int i=0;i<100 && distance<=10000;++i) {
        if(separated(panels,cx,cy,span,distance,workspace,gap)) return distance;
        distance=distance*1.15f+.01f;
    }
    throw std::runtime_error("Cannot maintain the curved-panel gutter. Increase layout spacing or reduce curvature.");
}
}
