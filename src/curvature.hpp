#pragma once
#include <algorithm>
#include <cmath>

namespace spatial {
constexpr float pi = 3.14159265358979323846f;
inline int surfaceSegments(float width,float bend) {
    return bend==0 ? 1 : std::clamp(int(std::ceil(std::abs(width*bend)*180/pi)),8,180);
}
struct Point { float x, y, z; };
struct Workspace {
    float amount=0, degrees=-1;
    bool follow=false;
    float gap=24.f/900;
    Workspace(float legacy=0):amount(legacy){}
};
struct Pose { Point center; float yaw; float surfaceBend; float latitude=0; bool spherical=false; };
inline int verticalSegments(float height,const Pose& p){
    return p.spherical ? std::clamp(int(std::ceil(height*p.surfaceBend*180/pi/3)),1,60) : 1;
}

// Curvature is inverse radius. Stable limits avoid cancellation near zero.
inline float bendX(float x, float k) {
    const float angle = x*k;
    return std::abs(angle)<1e-4f ? x : std::sin(angle)/k;
}
inline float bendZ(float x, float k) {
    const float half = x*k/2;
    return k == 0 ? 0 : 2*std::sin(half)*std::sin(half)/k;
}
inline float workspaceBend(float span,float distance,Workspace workspace){
    const float radius=std::max(distance,span/(5*pi/3));
    // A closed ring needs one gutter at the seam as well as those in the layout.
    const float wrapSpan=span+(workspace.follow && workspace.degrees==360 ? workspace.gap : 0);
    return workspace.degrees>=0 ? workspace.degrees*pi/180/std::max(wrapSpan,.001f) : workspace.amount/100/radius;
}
// Horizontal cylindrical arrangement, referenced to the neutral camera position.
// 100% uses the camera distance as radius, unless the sweep limit requires more.
inline Pose pose(float x, float y, float width, float span, float distance,
                 Workspace workspace, float surfacePercent, Point eye = {0,0,0}) {
    const float k=workspaceBend(span,distance,workspace);
    // Explicit sweep fixes angular placement. Grow the ring radius to keep flat
    // tangent panels separated; zoom never changes the requested angle.
    const float stretch=workspace.degrees>0 ? std::max(1.f,distance*k) : 1.f;
    const Point center{eye.x+bendX(x,k)*stretch, eye.y+y, eye.z-distance+bendZ(x,k)*stretch};
    if(workspace.follow && k>0){
        // The same arc-length scale must place centers AND tessellate surfaces.
        // Stretching only centers turns small gutters into large empty wedges.
        return {{eye.x+bendX(x,k),eye.y+y,eye.z-distance+bendZ(x,k)},-x*k,k};
    }
    if(workspace.follow)return {center,-x*k,0};
    const float range = std::sqrt(std::pow(center.x-eye.x,2)+std::pow(center.y-eye.y,2)+std::pow(center.z-eye.z,2));
    const float surfaceK = surfacePercent/100 * std::min(1/std::max(range,.1f), (8*pi/9)/width); // <= 160 degrees
    return {center, -x*k, surfaceK};
}
// Bent screen keeps its center/tangent and horizontal arc length. Borders,
// texture and placeholder artwork all use the same surface function.
inline Point vertex(const Pose& p, float x, float y, float offset = 0) {
    if(p.spherical){
        const float r=1/p.surfaceBend, lon=-p.yaw, lat=p.latitude;
        const float a=lon+x/r,b=lat+y/r;
        return {p.center.x+r*(std::sin(a)*std::cos(b)-std::sin(lon)*std::cos(lat))-offset*std::sin(a)*std::cos(b),
                p.center.y+r*(std::sin(b)-std::sin(lat))-offset*std::sin(b),
                p.center.z+r*(std::cos(lon)*std::cos(lat)-std::cos(a)*std::cos(b))+offset*std::cos(a)*std::cos(b)};
    }
    const float lx=bendX(x,p.surfaceBend), lz=bendZ(x,p.surfaceBend)+offset;
    const float c=std::cos(p.yaw),s=std::sin(p.yaw);
    return {p.center.x+c*lx+s*lz,p.center.y+y,p.center.z-s*lx+c*lz};
}
}
