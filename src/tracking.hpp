#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
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
// Tunable stabilisation, live-loaded from tracking.tsv beside the layout:
//   tracking-v1 <horizonMs 0..30> <restSpeed deg/s> <fullSpeed deg/s> <samples 2..8>
// A lower horizon or a higher rest speed gives a steadier image; the opposite gives less lag.
struct Prediction {
    double horizonMs=20;  // cap on extrapolation; 0 shows the measured pose unchanged
    double restSpeed=2;   // at or below this head speed nothing is predicted
    double fullSpeed=20;  // at or above it the whole horizon applies
    int samples=5;        // least-squares velocity window
};
inline std::optional<Prediction> parsePrediction(const std::string& line) {
    std::istringstream in(line); std::string version,extra; Prediction p;
    if (!(in>>version>>p.horizonMs>>p.restSpeed>>p.fullSpeed>>p.samples) || version!="tracking-v1" || (in>>extra)
        || !std::isfinite(p.horizonMs) || !std::isfinite(p.restSpeed) || !std::isfinite(p.fullSpeed)
        || p.horizonMs<0 || p.horizonMs>30 || p.restSpeed<0 || p.fullSpeed<=p.restSpeed || p.fullSpeed>2000
        || p.samples<2 || p.samples>8) return std::nullopt;
    return p;
}
struct Camera {
    Quaternion view;
    Prediction prediction;
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
    // Extrapolate to a scanout time. A stale pose, a still head and a gap in the samples are left as measured.
    bool predict(double targetTime, double now) {
        predictionActive=false; predictionMs=0;
        if(histCount<2 || !fresh(now) || prediction.horizonMs<=0) { updateView(); return false; }
        const Sample& latest=history[(histNext+7)%8];
        const double horizon=std::clamp(targetTime-latest.t, 0.0, prediction.horizonMs/1000);
        // Least-squares slope over the newest unbroken run of samples, relative to the latest. A two-point
        // difference multiplies sensor jitter by horizon/dt, which shows as shimmer on a still head.
        const int wanted=std::min(histCount,std::clamp(prediction.samples,2,8));
        double st=0,stt=0,sr=0,sp=0,sy=0,str=0,stp=0,sty=0,newer=latest.t; int n=0;
        for(int k=0;k<wanted;++k){
            const Sample& s=history[(histNext+7-k)%8];
            if(k && (newer-s.t>0.05 || s.t>=newer)) break;
            const double t=s.t-latest.t,r=s.roll-latest.roll,p=s.pitch-latest.pitch,y=std::remainder(s.yaw-latest.yaw,360.0);
            st+=t;stt+=t*t;sr+=r;sp+=p;sy+=y;str+=t*r;stp+=t*p;sty+=t*y;newer=s.t;++n;
        }
        const double spread=n*stt-st*st;
        if(n<2 || latest.t-newer<0.001 || spread<=0 || horizon<=0) { updateView(); return false; }
        const double vr=(n*str-st*sr)/spread,vp=(n*stp-st*sp)/spread,vy=(n*sty-st*sy)/spread;
        if(std::abs(vr)>2000 || std::abs(vp)>2000 || std::abs(vy)>2000) { updateView(); return false; }
        // Fade prediction in with head speed, so what little velocity noise remains never moves a still image.
        const double x=std::clamp((std::sqrt(vr*vr+vp*vp+vy*vy)-prediction.restSpeed)/(prediction.fullSpeed-prediction.restSpeed),0.0,1.0);
        const double applied=horizon*x*x*(3-2*x);
        if(applied<=0) { updateView(); return false; }
        const double relYaw=std::remainder((latest.yaw-neutralYaw)+vy*applied,360.0);
        view=conjugate(orientation(latest.roll+vr*applied, latest.pitch+vp*applied, relYaw));
        predictionActive=true; predictionMs=applied*1000; return true;
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
