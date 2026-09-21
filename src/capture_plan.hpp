#pragma once
#include "targeting.hpp"
#include <vector>

namespace adaptive {
using targeting::Vec;
struct Plan {bool visible=false;float scale=1;};
inline bool intersects(std::vector<Vec> polygon,float tx,float ty,float eye){
    // Clip each triangle against the shared stereo frustum plus a 10% margin.
    for(int plane=0;plane<5 && !polygon.empty();++plane){
        auto side=[&](Vec v){switch(plane){case 0:return -v.z-.1f;case 1:return -v.z*tx+eye+v.x;
            case 2:return -v.z*tx+eye-v.x;case 3:return -v.z*ty+v.y;default:return -v.z*ty-v.y;}};
        std::vector<Vec> result;Vec previous=polygon.back();float a=side(previous);
        for(auto current:polygon){float b=side(current);if((a>=0)!=(b>=0))result.push_back(targeting::add(previous,targeting::mul(targeting::sub(current,previous),a/(a-b))));
            if(b>=0)result.push_back(current);
            previous=current;a=b;}
        polygon=std::move(result);
    }
    return !polygon.empty();
}
inline Plan project(const PanelLayout& p,const spatial::Pose& pose,tracking::Quaternion view,Vec pan,
                    int eyeWidth,int height,float fov,float halfIpd){
    Plan result;float density=0;
    const float tangent=std::tan(fov*spatial::pi/360),focal=height/(2*tangent);
    const int n=spatial::surfaceSegments(p.width/900,pose.surfaceBend);
    auto at=[&](int i,float y){return targeting::rotate(view,targeting::add(spatial::vertex(pose,(float(i)/n-.5f)*p.width/900,y),pan));};
    auto pixels=[&](Vec a,Vec b,float native){
        if(a.z>=-.1f || b.z>=-.1f)return 1.f;
        return focal*std::hypot(a.x/-a.z-b.x/-b.z,a.y/-a.z-b.y/-b.z)/native;
    };
    const int rows=spatial::verticalSegments(p.height/900,pose);
    for(int i=0;i<n;++i)for(int j=0;j<rows;++j){
        const float y0=(float(j)/rows-.5f)*p.height/900,y1=(float(j+1)/rows-.5f)*p.height/900;
        auto a=at(i,y0),b=at(i,y1),c=at(i+1,y0),d=at(i+1,y1);
        if(!intersects({a,b,c},tangent*eyeWidth/height*1.1f,tangent*1.1f,halfIpd) &&
           !intersects({c,b,d},tangent*eyeWidth/height*1.1f,tangent*1.1f,halfIpd))continue;
        result.visible=true;
        density=std::max({density,pixels(a,b,p.height/rows),pixels(c,d,p.height/rows),pixels(a,c,p.width/n),pixels(b,d,p.width/n)});
    }
    result.scale=std::clamp(density*1.25f,1.f/32,1.f);return result; // 25% sampling headroom
}
struct Quality {
    float scale=1;double lowerSince=-1;
    float update(float desired,double now){
        float next=1;
        for(float bucket:{1.f,.75f,.5f,.375f,.25f,.1875f,.125f,.09375f,.0625f,.03125f})
            if(bucket>=desired)next=bucket;
        if(next>scale){scale=next;lowerSince=-1;}
        else if(next<scale){if(lowerSince<0)lowerSince=now;if(now-lowerSince>=.35){scale=next;lowerSince=-1;}}
        else lowerSince=-1;
        return scale;
    }
};
}
