#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

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
//   tracking-v2 <horizonMs 0..30> <restSpeed deg/s> <fullSpeed deg/s> <samples 2..8> <minCutoffHz 0..30> <beta 0..5>
// (tracking-v1 with the first four values is still accepted). A lower horizon, a higher rest speed,
// a lower cutoff or a lower beta all give a steadier image; the opposite gives less lag.
struct Prediction {
    double horizonMs=20;  // cap on extrapolation; 0 shows the measured pose unchanged
    double restSpeed=2;   // at or below this head speed nothing is predicted
    double fullSpeed=20;  // at or above it the whole horizon applies
    int samples=5;        // least-squares velocity window
    double minCutoff=0.7; // Hz: smoothing on a still or shaking head; 0 disables the filter
    double beta=0.25;     // Hz per deg/s above restSpeed: how fast the filter opens for a deliberate turn
};
inline std::optional<Prediction> parsePrediction(const std::string& line) {
    std::istringstream in(line); std::string version,extra; Prediction p;
    if (!(in>>version>>p.horizonMs>>p.restSpeed>>p.fullSpeed>>p.samples)) return std::nullopt;
    if (version=="tracking-v2") { if (!(in>>p.minCutoff>>p.beta)) return std::nullopt; }
    else if (version!="tracking-v1") return std::nullopt;
    if ((in>>extra) || !std::isfinite(p.horizonMs) || !std::isfinite(p.restSpeed) || !std::isfinite(p.fullSpeed)
        || !std::isfinite(p.minCutoff) || !std::isfinite(p.beta)
        || p.horizonMs<0 || p.horizonMs>30 || p.restSpeed<0 || p.fullSpeed<=p.restSpeed || p.fullSpeed>2000
        || p.samples<2 || p.samples>8 || p.minCutoff<0 || p.minCutoff>30 || p.beta<0 || p.beta>5) return std::nullopt;
    return p;
}
struct Camera {
    Quaternion view;
    Prediction prediction;
    double roll=0,pitch=0,yaw=0,neutralYaw=0;
    double timestamp=-1, deviceTimestamp=0, predictionMs=0;
    bool centered=false, predictionActive=false;
    unsigned samples=0;
    // History of stabilised angles: t is host time (for the scanout horizon), td the sample clock
    // (device time when its unit is known, jitter-free for velocity fits).
    struct Sample { double t=0, td=0, roll=0, pitch=0, yaw=0; };
    Sample history[8]{};
    int histCount=0, histNext=0;
    // One-Euro filter per axis on unwrapped angles (Casiez, Roussel, Vogel 2012): a low-pass whose
    // cutoff rises with speed, so a still head is smoothed hard and a turn barely at all. The rise is
    // gated by motion coherence, net displacement over path length in the last 200 ms: about 0.95
    // for a deliberate turn, 0.3-0.5 for a shake with some drift mixed in, near zero for a pure
    // shake. The gate is closed below 0.6 and fully open above 0.9, so a shake stays smoothed
    // however fast it is; and the speed term starts above restSpeed, so a slow drift stays smoothed.
    struct Axis { double raw=0, filtered=0, velocity=0; };
    Axis axes[3]{};
    double settledSpeed=0;   // deg/s of the stabilised output, 4 Hz smoothed: what a dwell watches
    bool filterReady=false;
    double filterTime=-1, cutoffHz=0, coherence=1;
    struct Step { double t=0, d[3]{}; };
    Step steps[32]{};
    int stepCount=0, stepNext=0;
    // Device clock unit, learned from the ratio of host to device intervals over the first samples.
    double deviceScale=0, prevDevice=0, prevHost=-1;
    std::vector<double> ratios;
    bool fresh(double now) const { return timestamp>=0 && now>=timestamp && now-timestamp<=.25; }
    void updateView() {view=conjugate(orientation(roll,pitch,std::remainder(yaw-neutralYaw,360.0))); predictionActive=false; predictionMs=0;}
    void remember(double td) {
        history[histNext]={timestamp,td,roll,pitch,yaw};
        histNext=(histNext+1)%8;
        if(histCount<8)++histCount;
    }
    static double lowpass(double previous, double value, double dt, double cutoffHz) {
        const double tau=1/(2*3.14159265358979323846*cutoffHz);
        const double alpha=1/(1+tau/dt);
        return previous+alpha*(value-previous);
    }
    void resetFilter(const double value[3], double t) {
        for(int i=0;i<3;++i) axes[i]={value[i],value[i],0};
        settledSpeed=0;
        filterReady=true; filterTime=t; stepCount=stepNext=0; cutoffHz=prediction.minCutoff; coherence=1;
    }
    // Returns the stabilised angles for one sample taken at time t (seconds, on the sample clock).
    void stabilise(double value[3], double t) {
        // Unwrap yaw against the previous raw sample so the filter never sees a 360 degree step.
        if(filterReady) value[2]=axes[2].raw+std::remainder(value[2]-axes[2].raw,360.0);
        const double dt=filterReady ? t-filterTime : 0;
        if(!filterReady || dt<=0 || dt>0.25 || prediction.minCutoff<=0) {
            resetFilter(value, t);
            if(prediction.minCutoff<=0) cutoffHz=0;
            return;
        }
        Step& step=steps[stepNext]; step.t=t;
        for(int i=0;i<3;++i) step.d[i]=value[i]-axes[i].raw;
        stepNext=(stepNext+1)%32; if(stepCount<32)++stepCount;
        double net[3]{}, path=0;
        for(int k=0;k<stepCount;++k){
            const Step& s=steps[(stepNext+31-k)%32];
            if(t-s.t>0.2) break;
            for(int i=0;i<3;++i) net[i]+=s.d[i];
            path+=std::sqrt(s.d[0]*s.d[0]+s.d[1]*s.d[1]+s.d[2]*s.d[2]);
        }
        coherence=path>1e-4 ? std::clamp(std::sqrt(net[0]*net[0]+net[1]*net[1]+net[2]*net[2])/path,0.0,1.0) : 1;
        double speed=0;
        for(int i=0;i<3;++i){
            const double rawVelocity=(value[i]-axes[i].raw)/dt;
            axes[i].velocity=lowpass(axes[i].velocity, rawVelocity, dt, 1.0);
            speed+=axes[i].velocity*axes[i].velocity;
        }
        speed=std::sqrt(speed);
        cutoffHz=prediction.minCutoff+prediction.beta*std::max(0.0,speed-prediction.restSpeed)*coherenceGate();
        double moved=0;
        for(int i=0;i<3;++i){
            const double before=axes[i].filtered;
            axes[i].filtered=lowpass(axes[i].filtered, value[i], dt, cutoffHz);
            moved+=(axes[i].filtered-before)*(axes[i].filtered-before);
            axes[i].raw=value[i];
            value[i]=axes[i].filtered;
        }
        // The dwell watches the stabilised output, so a smoothed shake still counts as settled,
        // and it needs a quick estimate: a 4 Hz smoothing settles within a quarter second of a turn.
        settledSpeed=lowpass(settledSpeed, std::sqrt(moved)/dt, dt, 4.0);
        filterTime=t;
    }
    // Angular speed of the stabilised output in deg/s, 4 Hz smoothed. With the filter off it is the
    // one-hertz smoothed raw velocity magnitude.
    double headSpeed() const {
        if(prediction.minCutoff<=0) return std::sqrt(axes[0].velocity*axes[0].velocity+axes[1].velocity*axes[1].velocity+axes[2].velocity*axes[2].velocity);
        return settledSpeed;
    }
    double coherenceGate() const { const double g=std::clamp((coherence-0.6)/0.3,0.0,1.0); return g*g*(3-2*g); }
    // Extrapolate to a scanout time. A stale pose, a still head, a shaking head and a gap in the
    // samples are left as measured.
    bool predict(double targetTime, double now) {
        predictionActive=false; predictionMs=0;
        if(histCount<2 || !fresh(now) || prediction.horizonMs<=0) { updateView(); return false; }
        const Sample& latest=history[(histNext+7)%8];
        const double horizon=std::clamp(targetTime-latest.t, 0.0, prediction.horizonMs/1000);
        // Least-squares slope over the newest unbroken run of samples, relative to the latest. A two-point
        // difference multiplies sensor jitter by horizon/dt, which shows as shimmer on a still head.
        const int wanted=std::min(histCount,std::clamp(prediction.samples,2,8));
        double st=0,stt=0,sr=0,sp=0,sy=0,str=0,stp=0,sty=0,newer=latest.td; int n=0;
        for(int k=0;k<wanted;++k){
            const Sample& s=history[(histNext+7-k)%8];
            if(k && (newer-s.td>0.05 || s.td>=newer)) break;
            const double t=s.td-latest.td,r=s.roll-latest.roll,p=s.pitch-latest.pitch,y=std::remainder(s.yaw-latest.yaw,360.0);
            st+=t;stt+=t*t;sr+=r;sp+=p;sy+=y;str+=t*r;stp+=t*p;sty+=t*y;newer=s.td;++n;
        }
        const double spread=n*stt-st*st;
        if(n<2 || latest.td-newer<0.001 || spread<=0 || horizon<=0) { updateView(); return false; }
        const double vr=(n*str-st*sr)/spread,vp=(n*stp-st*sp)/spread,vy=(n*sty-st*sy)/spread;
        if(std::abs(vr)>2000 || std::abs(vp)>2000 || std::abs(vy)>2000) { updateView(); return false; }
        // Fade prediction in with head speed, so what little velocity noise remains never moves a still
        // image, and scale it by coherence: extrapolating a shake overshoots at every reversal.
        const double x=std::clamp((std::sqrt(vr*vr+vp*vp+vy*vy)-prediction.restSpeed)/(prediction.fullSpeed-prediction.restSpeed),0.0,1.0);
        const double applied=horizon*x*x*(3-2*x)*coherenceGate();
        if(applied<=0) { updateView(); return false; }
        const double relYaw=std::remainder((latest.yaw-neutralYaw)+vy*applied,360.0);
        view=conjugate(orientation(latest.roll+vr*applied, latest.pitch+vp*applied, relYaw));
        predictionActive=true; predictionMs=applied*1000; return true;
    }
    void learnDeviceClock(double device, double stamp) {
        if(deviceScale>0 || device<=0) return;
        if(prevDevice>0 && device>prevDevice && stamp>prevHost && stamp-prevHost<0.05) ratios.push_back((stamp-prevHost)/(device-prevDevice));
        prevDevice=device; prevHost=stamp;
        if(ratios.size()<24) return;
        std::sort(ratios.begin(), ratios.end());
        const double median=ratios[ratios.size()/2];
        double best=1, error=1e9;
        for(double candidate:{1.0,1e-3,1e-6,1e-9}){
            const double e=std::abs(std::log(median/candidate));
            if(e<error){error=e;best=candidate;}
        }
        deviceScale=error<0.7 ? best : -1;   // -1: unit unknown, keep host time
        ratios.clear();
    }
    bool accept(const std::string& packet, double now) {
        std::istringstream in(packet); double stamp,r,p,y,device=0; std::string version,extra;
        if (!(in>>version>>stamp>>r>>p>>y) || (version!="euler-nwu-v1" && version!="euler-nwu-v2")) return false;
        if (version=="euler-nwu-v2" && (!(in>>device) || !std::isfinite(device))) return false;
        if ((in>>extra) || !std::isfinite(stamp) || !std::isfinite(r) || !std::isfinite(p) || !std::isfinite(y)
            || stamp<=timestamp || stamp>now || now-stamp>.25) return false;
        learnDeviceClock(device, stamp);
        const double td=deviceScale>0 && device>0 ? device*deviceScale : stamp;
        double value[3]={r,p,y};
        stabilise(value, td);
        roll=value[0];pitch=value[1];yaw=std::remainder(value[2],360.0);timestamp=stamp;deviceTimestamp=device;++samples;
        if (!centered) {neutralYaw=yaw;centered=true;}
        remember(td);
        updateView(); return true;
    }
    void recenter(double now) {
        if (fresh(now)) {neutralYaw=yaw;centered=true;updateView();}
        else centered=false; // Hold last view until a fresh sample can recenter.
    }
};
}
