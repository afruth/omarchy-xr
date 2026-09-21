#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>

namespace tracking {
struct Quaternion { double w=1,x=0,y=0,z=0; };
inline Quaternion conjugate(Quaternion q) { return {q.w,-q.x,-q.y,-q.z}; }
inline Quaternion multiply(Quaternion a, Quaternion b) {
    return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
        a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
// Gen1/Gen2 camera convention from the SDK demo: yaw left-positive,
// pitch down-positive, roll around forward. Recenter heading only, preserving
// gravity. Do not reinterpret the device quaternion's body frame as the camera.
inline Quaternion orientation(double roll, double pitch, double yaw) {
    constexpr double halfRadians=3.14159265358979323846/360;
    const double y=yaw*halfRadians,p=-pitch*halfRadians,r=-roll*halfRadians;
    return multiply(multiply({std::cos(y),0,std::sin(y),0},
        {std::cos(p),std::sin(p),0,0}),{std::cos(r),0,0,std::sin(r)});
}
inline std::array<float,16> matrix(Quaternion q) {
    const double w=q.w,x=q.x,y=q.y,z=q.z;
    return {float(1-2*(y*y+z*z)),float(2*(x*y+w*z)),float(2*(x*z-w*y)),0,
        float(2*(x*y-w*z)),float(1-2*(x*x+z*z)),float(2*(y*z+w*x)),0,
        float(2*(x*z+w*y)),float(2*(y*z-w*x)),float(1-2*(x*x+y*y)),0,
        0,0,0,1};
}
struct Camera {
    Quaternion view;
    double roll=0,pitch=0,yaw=0,neutralYaw=0;
    double timestamp=-1, deviceTimestamp=0, predictionMs=0;
    bool centered=false, predictionActive=false;
    unsigned samples=0;
    struct Sample { double t=0, roll=0, pitch=0, yaw=0; };
    Sample history[8]{};
    int histCount=0, histNext=0;
    bool fresh(double now) const { return timestamp>=0 && now>=timestamp && now-timestamp<=.25; }
    void updateView() {view=conjugate(orientation(roll,pitch,std::remainder(yaw-neutralYaw,360.0))); predictionActive=false; predictionMs=0;}
    void remember() {
        history[histNext]={timestamp,roll,pitch,yaw};
        histNext=(histNext+1)%8;
        if(histCount<8)++histCount;
    }
    // Extrapolate to a scanout time. The horizon never exceeds 30 ms, and a stale pose is left as measured.
    bool predict(double targetTime, double now) {
        predictionActive=false; predictionMs=0;
        if(histCount<2 || !fresh(now)) { updateView(); return false; }
        const Sample& latest=history[(histNext+7)%8];
        const Sample& previous=history[(histNext+6)%8];
        const double dt=latest.t-previous.t;
        const double horizon=std::clamp(targetTime-latest.t, 0.0, 0.030);
        if(dt<0.001 || dt>0.05 || horizon<=0) { updateView(); return false; }
        const double vr=(latest.roll-previous.roll)/dt;
        const double vp=(latest.pitch-previous.pitch)/dt;
        const double vy=std::remainder(latest.yaw-previous.yaw,360.0)/dt;
        if(std::abs(vr)>2000 || std::abs(vp)>2000 || std::abs(vy)>2000) { updateView(); return false; }
        const double relYaw=std::remainder((latest.yaw-neutralYaw)+vy*horizon,360.0);
        view=conjugate(orientation(latest.roll+vr*horizon, latest.pitch+vp*horizon, relYaw));
        predictionActive=true; predictionMs=horizon*1000; return true;
    }
    bool accept(const std::string& packet, double now) {
        std::istringstream in(packet); double stamp,r,p,y,device=0; std::string version,extra;
        if (!(in>>version>>stamp>>r>>p>>y) || (version!="euler-nwu-v1" && version!="euler-nwu-v2")) return false;
        if (version=="euler-nwu-v2" && (!(in>>device) || !std::isfinite(device))) return false;
        if ((in>>extra) || !std::isfinite(stamp) || !std::isfinite(r) || !std::isfinite(p) || !std::isfinite(y)
            || stamp<=timestamp || stamp>now || now-stamp>.25) return false;
        roll=r;pitch=p;yaw=y;timestamp=stamp;deviceTimestamp=device;++samples;
        if (!centered) {neutralYaw=yaw;centered=true;}
        remember();
        updateView(); return true;
    }
    void recenter(double now) {
        if (fresh(now)) {neutralYaw=yaw;centered=true;updateView();}
        else centered=false; // Hold last view until a fresh sample can recenter.
    }
};
}
