#pragma once
#include "curvature.hpp"
#include "layout.hpp"
#include "tracking.hpp"
#include <optional>
#include <limits>

// Head-directed targeting, not eye tracking. Coordinates are renderer world
// units; UV uses top-left origin. Query is independent of actions and selection.
namespace targeting {
using Vec=spatial::Point;
inline Vec add(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec sub(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec mul(Vec a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline bool finite(Vec a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline Vec normalize(Vec a){const float n=std::sqrt(dot(a,a));return n>1e-8f?mul(a,1/n):Vec{0,0,0};}
inline Vec rotate(tracking::Quaternion q,Vec v){auto m=tracking::matrix(q);return {m[0]*v.x+m[4]*v.y+m[8]*v.z,m[1]*v.x+m[5]*v.y+m[9]*v.z,m[2]*v.x+m[6]*v.y+m[10]*v.z};}
// Exactly matches GL's headView * Rx(mousePitch) * Ry(mouseYaw).
inline tracking::Quaternion viewRotation(tracking::Quaternion head,float pitch,float yaw){
    const float p=pitch*spatial::pi/360,y=yaw*spatial::pi/360;
    return tracking::multiply(head,tracking::multiply({std::cos(p),std::sin(p),0,0},{std::cos(y),0,std::sin(y),0}));
}
struct Ray {Vec origin,direction;};
inline Ray viewRay(tracking::Quaternion view,Vec pan){return {mul(pan,-1),rotate(tracking::conjugate(view),{0,0,-1})};}
struct Hit {
    std::string output; // Stable compositor output identity; never a vector index.
    Vec point,normal;
    float distance=0,u=0,v=0,pixelX=0,pixelY=0;
};
struct TriangleHit {float distance,b,c;};
inline std::optional<TriangleHit> triangle(Ray ray,Vec a,Vec b,Vec c){
    const auto ab=sub(b,a),ac=sub(c,a),p=cross(ray.direction,ac);
    const float det=dot(ab,p);if(std::abs(det)<1e-8f)return {};
    const float inv=1/det;const auto t=sub(ray.origin,a);
    const float beta=dot(t,p)*inv;if(beta<0 || beta>1)return {};
    const auto q=cross(t,ab);const float gamma=dot(ray.direction,q)*inv;
    if(gamma<0 || beta+gamma>1)return {};
    const float distance=dot(ac,q)*inv;if(distance<.1f)return {}; // renderer near plane
    return TriangleHit{distance,beta,gamma};
}
inline std::optional<Hit> intersect(Ray ray,const PanelLayout& panel,const spatial::Pose& pose){
    if(!finite(ray.origin) || !finite(ray.direction) || dot(ray.direction,ray.direction)<1e-12f)return {};
    ray.direction=normalize(ray.direction);
    const float w=panel.width/900,h=panel.height/900;
    const int segments=spatial::surfaceSegments(w,pose.surfaceBend);
    std::optional<Hit> best;
    const int rows=spatial::verticalSegments(h,pose);
    for(int i=0;i<segments;++i)for(int j=0;j<rows;++j){
        const float v0=float(j)/rows,v1=float(j+1)/rows;
        const float u0=float(i)/segments,u1=float(i+1)/segments;
        const auto a=spatial::vertex(pose,(u0-.5f)*w,(v0-.5f)*h),b=spatial::vertex(pose,(u0-.5f)*w,(v1-.5f)*h);
        const auto c=spatial::vertex(pose,(u1-.5f)*w,(v0-.5f)*h),d=spatial::vertex(pose,(u1-.5f)*w,(v1-.5f)*h);
        auto test=[&](Vec x,Vec y,Vec z,float xu,float xv,float yu,float yv,float zu,float zv){
            auto hit=triangle(ray,x,y,z);if(!hit || (best && best->distance<=hit->distance))return;
            const float alpha=1-hit->b-hit->c;
            const float u=alpha*xu+hit->b*yu+hit->c*zu,v=alpha*xv+hit->b*yv+hit->c*zv;
            auto normal=normalize(cross(sub(y,x),sub(z,x)));
            if(dot(normal,ray.direction)>0)normal=mul(normal,-1); // renderer is double sided
            best=Hit{panel.output,add(ray.origin,mul(ray.direction,hit->distance)),normal,hit->distance,u,v,u*panel.width,v*panel.height};
        };
        test(a,b,c,u0,1-v0,u0,1-v1,u1,1-v0);test(c,b,d,u1,1-v0,u0,1-v1,u1,1-v1);
    }
    return best;
}
inline std::optional<Hit> query(Ray ray,const std::vector<PanelLayout>& panels,float cx,float cy,float span,float distance,spatial::Workspace workspace){
    ray.direction=normalize(ray.direction);
    std::optional<Hit> best;
    for(const auto& p:panels){
        const auto pose=spatial::pose((p.x+p.width/2-cx)/900,-(p.y+p.height/2-cy)/900,p.width/900,span,distance,workspace,p.curvature);
        auto hit=intersect(ray,p,pose);
        if(hit && (!best || hit->distance<best->distance-1e-5f))best=hit;
    }
    return best;
}
// Selection outlives a transient ray hit; identities survive layout reordering.
struct Selection {
    std::string output;
    void observe(const std::optional<Hit>& hit){if(hit)output=hit->output;}
    void validate(const std::vector<PanelLayout>& panels){
        if(std::none_of(panels.begin(),panels.end(),[&](const auto& p){return p.output==output;}))output.clear();
    }
};
enum class Transition {None,Enter,Move,Switch,Leave};
struct Tracker {
    std::optional<Hit> current;
    Transition transition=Transition::None;
    void update(std::optional<Hit> hit,bool fresh){
        if(!fresh || !hit){transition=current?Transition::Leave:Transition::None;current.reset();return;}
        transition=!current?Transition::Enter:current->output!=hit->output?Transition::Switch:Transition::Move;
        current=*hit;
    }
};
}
