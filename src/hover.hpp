#pragma once
#include "targeting.hpp"

namespace interaction {
struct Content {float width,height;};
inline Content content(float width,float height,unsigned pixelsWide,unsigned pixelsHigh){
    if(!pixelsWide || !pixelsHigh)return {width,height};
    float w=width,h=w*pixelsHigh/pixelsWide;
    if(h>height){h=height;w=h*pixelsWide/pixelsHigh;}
    return {w,h};
}
// Map logical panel UV through the actual letterboxed content rectangle.
inline std::optional<std::pair<float,float>> contentUV(const targeting::Hit& hit,float w,float h,Content c){
    float u=((hit.u-.5f)*w)/c.width+.5f,v=((hit.v-.5f)*h)/c.height+.5f;
    if(u<0 || u>1 || v<0 || v>1)return {};
    return std::pair{u,v};
}
inline float pixelDensity(const spatial::Pose& pose,float localX,float localY,Content c,unsigned pixelsHigh,
                          tracking::Quaternion view,targeting::Vec pan,int viewportHeight,float fov){
    if(!pixelsHigh)return 0;
    const float step=c.height/pixelsHigh*8;
    const auto a=targeting::rotate(view,targeting::add(spatial::vertex(pose,localX,localY-step),pan));
    const auto b=targeting::rotate(view,targeting::add(spatial::vertex(pose,localX,localY+step),pan));
    if(a.z>=-.1f || b.z>=-.1f)return 0;
    const float focal=viewportHeight/(2*std::tan(fov*spatial::pi/360));
    return std::hypot(a.x/-a.z-b.x/-b.z,a.y/-a.z-b.y/-b.z)*focal/16;
}
struct Hover {
    std::string output;
    bool near=false;
    float u=.5f,v=.5f;
    // Hysteresis prevents mode chatter; smooth in content space and never
    // interpolate across displays (which could focus unrelated windows).
    // A 2 ms time constant settles >95% within 6 ms without prediction.
    void update(const std::string& name,float density,float nextU,float nextV,float dt){
        if(name!=output){output=name;near=false;u=nextU;v=nextV;}
        near=density>=(near?.62f:.78f);
        const float alpha=-std::expm1(-std::clamp(dt,0.f,.1f)/.002f);
        u+=(nextU-u)*alpha;v+=(nextV-v)*alpha;
    }
    void clear(){output.clear();near=false;}
};
}
