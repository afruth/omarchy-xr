#pragma once
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <string>
#include <cmath>
#include <ctime>
#include <unistd.h>

// A tiny atomic mailbox on the user's local state directory. Hyprland writes
// cumulative motion, so coalescing input events never drops swipe distance.
// The heartbeat releases compositor bindings on normal exit or renderer crash.
class LiveControls {
    std::string path,session;
    double previousZoom=0,previousPanX=0,previousPanY=0;
    unsigned long long panSerial=0,panId=0;
    void updatePan(){
        panX=panY=0;panStarted=false;panActive=false;
        std::ifstream file(path+".pan");std::string owner,extra;
        unsigned long long seq,id;double x,y;int active;long stamp;
        if(!(file>>owner>>seq>>id>>x>>y>>active>>stamp) || file>>extra || owner!=session ||
           !std::isfinite(x) || !std::isfinite(y) || std::abs(x)>1e9 || std::abs(y)>1e9 ||
           (active!=0 && active!=1) || std::time(nullptr)-stamp>2 || stamp>std::time(nullptr))return;
        panActive=active;
        if(seq==panSerial)return;
        panStarted=id!=panId;
        if(panStarted){previousPanX=previousPanY=0;panId=id;}
        panX=std::clamp(x-previousPanX,-1000.,1000.);panY=std::clamp(y-previousPanY,-1000.,1000.);
        previousPanX=x;previousPanY=y;panSerial=seq;
    }
    unsigned long long serial=0,fitSerial=0,hoverSerial=0;
    std::time_t heartbeat=0;
public:
    double zoom=0,panX=0,panY=0;
    bool panStarted=false,panActive=false;
    int fit=0;
    explicit LiveControls(const std::string& pose):path(pose.empty()?"":pose+".controls"),session(std::to_string(getpid())) {
        if(!path.empty()){unlink(path.c_str());unlink((path+".pan").c_str());update();}
    }
    ~LiveControls(){if(!path.empty()){unlink((path+".pan").c_str());unlink((path+".active").c_str());unlink(path.c_str());unlink((path+".hover").c_str());unlink((path+".pointer").c_str());}}
    void publishHover(bool enabled,const std::string& output,float u,float v) {
        if(path.empty())return;
        const auto temp=path+".hover.tmp";
        {std::ofstream file(temp);file<<session<<' '<<++hoverSerial<<' '<<(enabled?1:0)<<' '<<(output.empty()?"-":output)<<' '<<std::setprecision(9)<<u<<' '<<v<<'\n';}
        std::rename(temp.c_str(),(path+".hover").c_str());
    }
    void update() {
        zoom=0;fit=0;if(path.empty())return;
        updatePan();
        const auto now=std::time(nullptr);
        if(now!=heartbeat) {
            const auto temp=path+".active.tmp";
            {std::ofstream file(temp);file<<session<<' '<<now<<'\n';}
            std::rename(temp.c_str(),(path+".active").c_str());heartbeat=now;
        }
        std::ifstream file(path);
        std::string owner,extra;unsigned long long nextSerial,nextFit;double total;int mode;
        if(!(file>>owner>>nextSerial>>total>>nextFit>>mode) || file>>extra || owner!=session ||
           !std::isfinite(total) || std::abs(total)>1e9 || mode<0 || mode>5 || nextSerial==serial)return;
        if(nextFit!=fitSerial){fit=mode;fitSerial=nextFit;}
        else zoom=std::clamp(total-previousZoom,-4.,4.);
        serial=nextSerial;previousZoom=total;
    }
};
