#pragma once
#include <cmath>
#include <string>

namespace notifications {
// A full left-right-centre (or right-left-centre) gesture, only after an alert
// has been visible and the head has settled. Raw angles avoid prediction and
// the camera's intentional shake smoothing. Camera/navigation transforms never enter here.
class HeadShake {
    std::string identity;
    double previousTime=-1, previousYaw=0, settledAt=-1, anchor=0, pitch=0, roll=0, started=0;
    double shownAt=0, cooldown=0;
    int direction=0, stage=0;
    bool armed=false;
    void resetMotion() {stage=0;direction=0;armed=false;settledAt=-1;}
public:
    bool busy(double now) const {return stage>0 || now<cooldown;}
    bool update(const std::string& key,double yaw,double nextPitch,double nextRoll,double time,bool fresh) {
        if(key!=identity) {identity=key;shownAt=time;resetMotion();previousTime=-1;}
        if(key.empty() || !fresh || !std::isfinite(yaw+nextPitch+nextRoll+time)) {resetMotion();previousTime=-1;return false;}
        const double dt=time-previousTime,delta=std::remainder(yaw-previousYaw,360.0);
        if(dt==0) return false;
        previousTime=time;previousYaw=yaw;
        if(dt<=0 || dt>.25 || time<cooldown || std::abs(delta)>25) {resetMotion();return false;}
        if(!armed) {
            if(std::abs(delta/dt)>18) {settledAt=-1;return false;}
            if(settledAt<0) settledAt=time;
            if(time-shownAt<.4 || time-settledAt<.18) return false;
            anchor=yaw;pitch=nextPitch;roll=nextRoll;armed=true;
        }
        if(std::abs(nextPitch-pitch)>10 || std::abs(std::remainder(nextRoll-roll,360.0))>10) {resetMotion();return false;}
        const double offset=std::remainder(yaw-anchor,360.0);
        if(std::abs(offset)>35 || (stage && time-started>1.2)) {resetMotion();return false;}
        if(stage==0 && std::abs(offset)>=9) {
            if(std::abs(delta/dt)<35) {resetMotion();return false;}
            direction=offset>0?1:-1;started=time;stage=1;
        } else if(stage==1 && offset*direction<=-9) stage=2;
        else if(stage==2 && std::abs(offset)<=4 && time-started>=.22) {
            cooldown=time+1.5;resetMotion();return true;
        }
        return false;
    }
};
}
