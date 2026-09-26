#include "capture_governor.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <set>

using namespace governor;

static std::string name(int i){ return "0x"+std::to_string(1000+i); }
static Input window(int i,bool staged,bool visible,float deg,unsigned w,unsigned h){ return {name(i),staged,visible,deg,i,w,h}; }

// 1 staged 1080p, 10 visible 720p with distinct angular sizes, 39 off-screen: the S1b set.
static std::vector<Input> desk(){
    std::vector<Input> in;
    in.push_back(window(0,true,true,40,1920,1080));
    for(int i=1;i<=10;++i) in.push_back(window(i,false,true,float(i%5==0 ? 30 : 5+i),1280,720));
    for(int i=11;i<50;++i) in.push_back(window(i,false,false,0,1280,720));
    return in;
}
// A staged 1080p window and n-1 other visible 1080p windows, largest first.
static std::vector<Input> small(int n){
    std::vector<Input> in;
    in.push_back(window(0,true,true,50,1920,1080));
    for(int i=1;i<n;++i) in.push_back(window(i,false,true,float(40-i),1920,1080));
    return in;
}
static double sum(const std::vector<Input>& in,const std::vector<Decision>& out){
    double s=0;
    for(size_t i=0;i<in.size();++i) s+=Ladder::cost(in[i],out[i].rateHz);
    return s;
}
static void lanesMatch(const std::vector<Decision>& out){ for(const auto& d:out) assert(d.inFlight==(d.rateHz>30 ? 2u : 1u)); }
static std::vector<unsigned> rates(const std::vector<Decision>& out){
    std::vector<unsigned> r;
    for(const auto& d:out) r.push_back(d.rateHz);
    return r;
}
static unsigned count(const std::vector<Decision>& out,Tier tier,unsigned hz){
    unsigned n=0;
    for(const auto& d:out) n+=d.tier==tier && d.rateHz==hz;
    return n;
}
static void phasesSpread(const std::vector<Decision>& out,unsigned hz){
    std::vector<double> phases;
    for(size_t i=0;i<out.size();++i) if(out[i].rateHz==hz && out[i].tier!=Tier::Focused) phases.push_back(out[i].phase);
    std::sort(phases.begin(),phases.end());
    for(size_t k=0;k<phases.size();++k) assert(std::abs(phases[k]-double(k)/phases.size())<1e-12);
}

static void helpers(){
    assert(Ladder::stepBelow(70)==60 && Ladder::stepBelow(59.9)==40 && Ladder::stepBelow(28.2)==24 && Ladder::stepBelow(19.7)==15);
    assert(Ladder::stepBelow(6)==6 && Ladder::stepBelow(5.9)==0);
    assert(Ladder::cost(window(0,false,true,1,1920,1080),60)==124416000);
}

// §4.4/S1c with 1080p windows after the 0.6 s Near entry; the S1b set lands at 60 / 30 x 4 / 10 x 6.
static void s1cTable(){
    const std::vector<std::pair<int,std::vector<unsigned>>> table={
        {2,{60,40}}, {4,{60,24,24,24}}, {6,{60,15,15,15,15,10}}, {11,{60,10,10,10,10,6,6,6,6,6,6}}};
    for(const auto& [n,want]:table){
        Ladder g; const auto in=small(n);
        g.plan(in,false,0); const auto out=g.plan(in,false,.6);
        assert(rates(out)==want && sum(in,out)<=300e6 && std::abs(g.usedMpix-sum(in,out)/1e6)<1e-6);
        lanesMatch(out);
        for(size_t i=1;i<out.size();++i) assert(out[i].tier==(i<=4 ? Tier::Near : Tier::Far));
    }
    Ladder g; const auto in=desk();
    g.plan(in,false,0); const auto out=g.plan(in,false,.6);
    assert(count(out,Tier::Focused,60)==1 && count(out,Tier::Near,30)==4 && count(out,Tier::Far,10)==6 && count(out,Tier::Idle,0)==39);
    assert(std::abs(sum(in,out)/1e6-290.304)<1e-3);
    lanesMatch(out);
}

static void tiers(){
    Ladder g; auto in=desk();
    auto out=g.plan(in,false,0);
    // Near needs 0.5 s in the top 4; until then every visible window is Far.
    for(size_t i=1;i<=10;++i) assert(out[i].tier==Tier::Far && out[i].rateHz==10);
    out=g.plan(in,false,.6);
    std::set<std::string> nearNames;
    for(size_t i=0;i<out.size();++i) switch(out[i].tier){
        case Tier::Focused: assert(out[i].rateHz==60 && out[i].inFlight==2 && out[i].phase==0 && !out[i].ignoreDamage && out[i].place==windows::Place::Stage); break;
        case Tier::Near: nearNames.insert(in[i].name); assert(out[i].rateHz==30 && out[i].ignoreDamage && out[i].place==windows::Place::Park); break;
        case Tier::Far: assert(out[i].rateHz==10 && out[i].ignoreDamage && out[i].place==windows::Place::Park); break;
        case Tier::Idle: assert(out[i].rateHz==0); break;
        case Tier::Overview: assert(false);
    }
    // Largest angular size wins: 5 and 10 at 30 deg, then 9 at 14 and 8 at 13.
    assert(nearNames==(std::set<std::string>{name(5),name(10),name(9),name(8)}));
    phasesSpread(out,30); phasesSpread(out,10);
    // MRU breaks an angular-size tie: equal sizes, the lower focus_history_id wins.
    Ladder tie; std::vector<Input> same;
    for(int i=0;i<6;++i) same.push_back({name(i),false,true,20,5-i,800,600});
    tie.plan(same,false,0); out=tie.plan(same,false,1);
    for(int i=0;i<6;++i) assert((out[i].tier==Tier::Near)==(i>=2));
}

static void hysteresis(){
    Ladder g; auto in=desk();
    g.plan(in,false,0); auto out=g.plan(in,false,.6);
    assert(out[8].tier==Tier::Near && out[7].tier==Tier::Far);
    // Window 8 (14 deg) and 7 (12 deg) swap places every 200 ms: 8 stays Near, 7 never qualifies.
    double t=.6;
    for(int step=0;step<30;++step){
        t+=.2;
        const bool swapped=step%2==0;
        in[8].angularWidthDeg=swapped ? 12 : 14; in[7].angularWidthDeg=swapped ? 14 : 12;
        out=g.plan(in,false,t);
        assert(out[8].tier==Tier::Near && out[7].tier==Tier::Far);
    }
    // Outside the top 6 it leaves after 1 s, not before.
    in[8].angularWidthDeg=1;
    out=g.plan(in,false,t+=.1); assert(out[8].tier==Tier::Near);
    out=g.plan(in,false,t+=.8); assert(out[8].tier==Tier::Near);
    out=g.plan(in,false,t+=.3); assert(out[8].tier==Tier::Far);
    // Window 7 qualified meanwhile and takes the free place; Near never exceeds four.
    unsigned nearN=0; for(auto& d:out) nearN+=d.tier==Tier::Near; assert(nearN==4 && out[7].tier==Tier::Near);
    // Turning invisible leaves Near at once; staging is immediate.
    in[5].visible=false; in[4].staged=true; in[0].staged=false;
    out=g.plan(in,false,t+=.01);
    assert(out[5].tier==Tier::Idle && out[4].tier==Tier::Focused && out[4].rateHz==60);
    assert(out[0].tier==Tier::Far);   // the old staged window must earn Near like any other
    in[4].visible=false;
    assert(g.plan(in,false,t+=.01)[4].tier==Tier::Focused);   // staged stays 60 when invisible
}

// Overview: min(10, budget / Σ pixels) on the ladder; with a staged 1080p the lowest-ranked windows idle.
static void overviewTable(){
    for(const auto& [n,hz]:std::vector<std::pair<int,unsigned>>{{30,10},{40,6},{50,6}}){
        Ladder g; std::vector<Input> in;
        for(int i=0;i<n;++i) in.push_back(window(i,false,true,3,1280,720));
        const auto out=g.plan(in,true,0);
        for(auto& d:out) assert(d.tier==Tier::Overview && d.rateHz==hz && d.inFlight==1 && d.ignoreDamage && d.place==windows::Place::Park);
        phasesSpread(out,hz);
        assert(sum(in,out)<=300e6);
    }
    Ladder g; std::vector<Input> in;
    in.push_back(window(0,true,true,5,1920,1080));
    for(int i=1;i<50;++i) in.push_back(window(i,false,true,float(100-i),1280,720));
    auto out=g.plan(in,true,0);
    assert(out[0].tier==Tier::Focused && out[0].rateHz==60 && count(out,Tier::Overview,6)==31 && count(out,Tier::Idle,0)==18);
    for(int i=1;i<50;++i) assert((out[i].rateHz==6)==(i<=31));   // idled from the bottom of the rank
    assert(sum(in,out)<=300e6);
    // Pinned windows in the overview: Near at the park pull ceiling, never a sliver; off-screen too.
    auto pinned=desk(); pinned[3].pinned=true; pinned[30].pinned=true;
    Ladder p; out=p.plan(pinned,true,0);
    for(int i:{3,30}) assert(out[i].tier==Tier::Near && out[i].rateHz==parkCapHz && out[i].place==windows::Place::Park && out[i].ignoreDamage);
    assert(out[0].tier==Tier::Focused && out[1].tier==Tier::Overview && out[1].rateHz==10 && out[31].tier==Tier::Idle);
    for(int i=2;i<=10;++i) pinned[i].pinned=true;
    unsigned nearN=0; for(const auto& d:p.plan(pinned,true,1)) nearN+=d.tier==Tier::Near;
    assert(nearN==nearCount);
}

// Random canvases never exceed the effective budget (beyond what the always-60 Hz staged window costs
// alone) or the VRAM cap, and every visible window runs at 6 Hz or more or is idle.
static void neverExceeded(){
    std::mt19937 rng(42);
    Ladder g; double t=0;
    for(int round=0;round<400;++round){
        std::vector<Input> in;
        const int n=1+int(rng()%80), offset=int(rng()%40);
        for(int i=0;i<n;++i){
            const unsigned w=200+rng()%3640, h=150+rng()%2000;
            in.push_back({name(offset+i),false,rng()%4!=0,float(rng()%60),int(rng()%50),w,h,rng()%15==0,w/2,h/2});
        }
        if(rng()%3) in[0].staged=true;
        if(rng()%10==0) g.budget.setMpix=50+rng()%600;
        if(rng()%10==0) g.maxHz=std::vector<unsigned>{60,40,30,20,10}[rng()%5];
        const bool zoomedOut=rng()%3==0;
        t+=.05+(rng()%100)/100.0;
        const auto out=g.plan(in,zoomedOut,t);
        double staged=0; for(size_t i=0;i<in.size();++i) if(in[i].staged) staged+=Ladder::cost(in[i],out[i].rateHz);
        assert(sum(in,out)<=std::max(g.budget.effective()*1e6,staged)+1e-3 && g.vramBytes<=vramCapBytes);
        for(size_t i=0;i<in.size();++i){
            if(in[i].staged) continue;
            assert(out[i].rateHz==0 || out[i].rateHz>=floorHz);
            assert((out[i].rateHz==0)==(out[i].tier==Tier::Idle));
            assert(out[i].rateHz<=g.maxHz && out[i].inFlight==(out[i].rateHz>30 ? 2u : 1u));
            assert(!(in[i].pinned && out[i].place==windows::Place::Sliver));
        }
    }
}

// Rates drop at once and rise only after 2 s with nothing lowered and the sum under 90 % of the budget.
static void raiseHysteresis(){
    Ladder g; auto in=small(5);
    g.plan(in,false,0); auto out=g.plan(in,false,.6);
    assert(rates(out)==(std::vector<unsigned>{60,20,20,20,20}));
    // A sixth window (smallest, Far) lowers the near windows in the same tick.
    auto more=in; more.push_back(window(5,false,true,1,1920,1080));
    out=g.plan(more,false,10);
    assert(rates(out)==(std::vector<unsigned>{60,15,15,15,15,10}));
    // Closed again 0.5 s later: no raise before 2 s after the lowering, the raise at 2 s.
    out=g.plan(in,false,10.5); assert(out[1].rateHz==15);
    out=g.plan(in,false,11.9); assert(out[1].rateHz==15);
    out=g.plan(in,false,12); assert(rates(out)==(std::vector<unsigned>{60,20,20,20,20}));
    // Open/close every 0.7 s for 30 s: no raise within 2 s of the previous one, never above the wanted step.
    std::map<std::string,double> raisedAt;
    std::map<std::string,unsigned> last;
    for(double t=20;t<50;t+=.7){
        const bool open=int((t-20)/.7+.5)%2==0;
        const auto& now=open ? more : in;
        out=g.plan(now,false,t);
        assert(sum(now,out)<=300e6);
        for(size_t i=1;i<=4;++i){
            assert(out[i].rateHz<=(open ? 15u : 20u) && out[i].rateHz>=15);
            if(last.count(now[i].name) && out[i].rateHz>last[now[i].name]){
                assert(!raisedAt.count(now[i].name) || t-raisedAt[now[i].name]>=2);
                raisedAt[now[i].name]=t;
            }
            last[now[i].name]=out[i].rateHz;
        }
    }
}

// Above 30 Hz a window is a live sliver (2 lanes, damage-driven); place changes at most once per 2 s.
static void placeHysteresis(){
    Ladder g; auto in=small(2);
    g.plan(in,false,0); auto out=g.plan(in,false,.6);
    // Pulled until Lua's `.windows` row confirms the move, so a copy never waits on a parked window's damage.
    assert(out[1].rateHz==40 && out[1].place==windows::Place::Sliver && out[1].inFlight==2 && out[1].ignoreDamage);
    in[1].shownAsSliver=true; out=g.plan(in,false,.7);
    assert(out[1].place==windows::Place::Sliver && !out[1].ignoreDamage);
    assert(out[0].place==windows::Place::Stage);
    // Three more windows qualify for Near 0.5 s after they appear: the rate drops at once, the place waits.
    for(int i=2;i<5;++i) in.push_back(window(i,false,true,float(30-i),1920,1080));
    out=g.plan(in,false,1.0); assert(out[1].rateHz==40);
    out=g.plan(in,false,1.6);
    assert(out[1].rateHz==20 && out[1].inFlight==1 && out[1].place==windows::Place::Sliver && !out[1].ignoreDamage);
    out=g.plan(in,false,2.5); assert(out[1].place==windows::Place::Sliver);
    out=g.plan(in,false,2.6); assert(out[1].place==windows::Place::Park && out[1].ignoreDamage);
    for(size_t i=2;i<5;++i) assert(out[i].place==windows::Place::Park);
    // Staged and pinned windows are never slivers.
    Ladder pin; auto two=small(2); two[1].pinned=true;
    pin.plan(two,false,0); out=pin.plan(two,false,.6);
    assert(out[1].tier==Tier::Near && out[1].rateHz==30 && out[1].place==windows::Place::Park);
    // A rate toggling 40/30 every 0.5 s never flips the place twice within 2 s.
    Ladder flip; in=small(2);
    double movedAt=-10; auto place=windows::Place::Park;
    unsigned moves=0;
    for(int k=0;k<40;++k){
        const double t=k*.5;
        flip.maxHz=k%2 ? 30 : 40;
        out=flip.plan(in,false,t);
        assert(out[0].place==windows::Place::Stage);
        if(out[1].place!=place){ assert(t-movedAt>=placeHold); movedAt=t; place=out[1].place; ++moves; }
    }
    assert(moves>=1);
}

static void calibration(){
    Budget b; b.refreshHz=60;
    // One slow second (a view change starting many sessions) does not shrink; the second in a row does.
    for(int k=0;k<=10;++k) b.readySample(45,k*.1);
    assert(b.effective()==300 && b.readyP50Ms==45 && b.readyOver==1);
    for(int k=11;k<=20;++k) b.readySample(33.3,k*.1);
    assert(b.effective()==300 && b.readyOver==0);
    for(int k=21;k<=40;++k) b.readySample(45,k*.1);
    assert(std::abs(b.effective()-270)<1e-9 && b.readyOver==2);
    double t=4;
    for(int s=0;s<20;++s) for(int k=0;k<10;++k) b.readySample(45,t+=.1);   // every further second shrinks
    assert(std::abs(b.effective()-150)<1e-9);
    // Under 2.25 frames: nothing for 5 s, then 2 % per second back to the setting and never above it.
    const double start=t, floor=b.effective();
    while(t<start+4.5) b.readySample(15,t+=.1);
    assert(b.effective()==floor);
    while(t<start+10.05) b.readySample(15,t+=.1);
    assert(b.effective()>floor);
    double previous=b.effective();
    for(int s=0;s<60;++s){
        for(int k=0;k<10;++k) b.readySample(15,t+=.1);
        assert(b.effective()<=300 && b.effective()<=previous*1.02+1e-9);
        previous=b.effective();
    }
    assert(b.effective()==300 && b.calibration==1);
    // The healthy export latency (two output frames, 33.3 ms at 60 Hz) never shrinks; between 2.25 and
    // 2.5 frames nothing changes either way; 120 Hz halves both thresholds.
    Budget healthy; for(int k=0;k<=100;++k) healthy.readySample(33.4,k*.1);
    assert(healthy.calibration==1);
    Budget mid; mid.calibration=.9; for(int k=0;k<=100;++k) mid.readySample(40,k*.1);
    assert(mid.calibration==.9);
    Budget grow; grow.calibration=.9; for(int k=0;k<=100;++k) grow.readySample(34,k*.1);
    assert(grow.calibration>.9);
    Budget fast; fast.refreshHz=120; for(int k=0;k<=20;++k) fast.readySample(22,k*.1);
    assert(std::abs(fast.calibration-.9)<1e-12);
    // Fewer than 8 samples in a second do not decide yet.
    Budget sparse; for(int k=0;k<=5;++k) sparse.readySample(45,k*.3);
    assert(sparse.calibration==1 && sparse.readyP50Ms<0);
}

static void gpuFeedback(){
    const double period=1000/60.;
    Budget b; double t=0;
    for(int k=0;k<30;++k) b.gpuSample(11,period,t+=1/60.);   // 66 %
    assert(b.gpuSteps==1 && !b.panic);
    while(t<.8) b.gpuSample(11,period,t+=1/60.);
    assert(b.gpuSteps==2 && std::abs(b.effective()-300*.75*.75)<1e-9);
    for(int k=0;k<600;++k) b.gpuSample(11,period,t+=1/60.);
    assert(b.gpuSteps==5);   // floor: 300 x 0.75^5 ≈ 71 Mpix/s
    // Past 85 %: panic, every non-focused window at 6 Hz while the staged one keeps 60.
    Budget hot; t=0;
    for(int k=0;k<30;++k) hot.gpuSample(15,period,t+=1/60.);
    assert(hot.panic && hot.gpuSteps>=1);
    Ladder g; g.budget=hot; auto in=small(3);
    g.plan(in,false,t); auto out=g.plan(in,false,t+.6);
    assert(out[0].rateHz==60 && out[1].rateHz==6 && out[2].rateHz==6 && out[1].tier==Tier::Near);
    // Calm (5 ms, 30 %): once the p80 falls, one step back per 2 s, panic cleared with the first.
    const double calm=t;
    while(t<calm+.45) hot.gpuSample(5,period,t+=1/60.);
    const int before=hot.gpuSteps;
    const double since=hot.gpuCalmSince;
    assert(before>=2 && hot.panic && since>0);
    while(t<since+1.95) hot.gpuSample(5,period,t+=1/60.);
    assert(hot.gpuSteps==before && hot.panic);
    while(t<since+2.05) hot.gpuSample(5,period,t+=1/60.);
    assert(hot.gpuSteps==before-1 && !hot.panic);
    while(t<since+4.05) hot.gpuSample(5,period,t+=1/60.);
    assert(hot.gpuSteps==before-2);
}

// 60 x 1080p zoomed out under a generous pixel budget: 16.6 MB each on one lane; past 512 MB the rest idle.
static void vramCap(){
    Ladder g; g.budget.setMpix=2000;
    std::vector<Input> in;
    for(int i=0;i<60;++i) in.push_back(window(i,false,true,float(100-i),1920,1080));
    auto out=g.plan(in,true,0);
    const double each=2*4*1920*1080.+4-thumbnailBytes;
    unsigned live=0; for(const auto& d:out) live+=d.rateHz>0;
    assert(live==unsigned((vramCapBytes-60*thumbnailBytes)/each) && live==31 && g.vramBytes<=vramCapBytes);
    for(size_t i=1;i<out.size();++i) assert(out[i].rateHz<=out[i-1].rateHz);   // idled from the bottom
    // The staged window counts its region lanes (4 lanes x 2 slots) and is never idled.
    in[59].staged=true;
    out=g.plan(in,true,1);
    assert(out[59].tier==Tier::Focused && out[59].rateHz==60 && g.vramBytes<=vramCapBytes);
    const double staged=4*2*4*1920*1080.+4;
    unsigned liveStaged=0; for(size_t i=0;i<59;++i) liveStaged+=out[i].rateHz>0;
    assert(liveStaged==unsigned((vramCapBytes-59*thumbnailBytes-staged)/each));
}

// canvas.tsv fps caps every tier; at 30 Hz or less the staged window runs one lane.
static void maxHzCap(){
    Ladder g; g.maxHz=20; auto in=desk();
    g.plan(in,false,0); auto out=g.plan(in,false,.6);
    for(auto& d:out) assert(d.rateHz<=20);
    assert(out[0].tier==Tier::Focused && out[0].rateHz==20 && out[0].inFlight==1);
    assert(out[8].tier==Tier::Near && out[8].rateHz==20 && out[1].tier==Tier::Far && out[1].rateHz==10);
    g.maxHz=40; out=g.plan(in,false,.7);
    assert(out[0].rateHz==40 && out[0].inFlight==2 && out[8].rateHz==40 && out[8].inFlight==2 && out[1].rateHz==10);
    assert(sum(in,out)<=300e6);
}

// Pinned windows rank first: Near even when smallest or off-screen, and Near still never exceeds four.
static void pinned(){
    Ladder g; auto in=desk();
    in[1].pinned=true;                                  // 6 deg, the smallest visible window
    in[20].pinned=true;                                 // off-screen
    g.plan(in,false,0); auto out=g.plan(in,false,.6);
    unsigned nearN=0;
    for(const auto& d:out) nearN+=d.tier==Tier::Near;
    assert(out[1].tier==Tier::Near && out[20].tier==Tier::Near && nearN==4 && out[0].tier==Tier::Focused);
    // Near entry lowered the far windows (10 → 6) in the same tick, so the near raise waits 2 s.
    assert(out[1].rateHz==10 && out[2].rateHz==6);
    out=g.plan(in,false,2.6);
    assert(out[1].rateHz==parkCapHz && out[1].place==windows::Place::Park && out[5].rateHz==40 && out[5].place==windows::Place::Sliver);
    for(int i=2;i<=10;++i) in[i].pinned=true;
    g.plan(in,false,3); out=g.plan(in,false,4);
    nearN=0; for(const auto& d:out) nearN+=d.tier==Tier::Near;
    assert(nearN==4);
}

// Closed windows leave every map.
static void forgetting(){
    Ladder g; auto in=small(3);
    g.plan(in,false,0); g.plan(in,false,.6);
    assert(g.granted.count(name(1)) && g.slivers.count(name(1)) && g.movedAt.count(name(1)));
    in.pop_back(); in.pop_back();
    g.plan(in,false,.7);
    for(int i:{1,2}) assert(!g.granted.count(name(i)) && !g.near.count(name(i)) && !g.slivers.count(name(i)) && !g.movedAt.count(name(i)));
}

static void speed(){
    Ladder g; std::vector<Input> in;
    for(int i=0;i<512;++i) in.push_back(window(i,i==0,i<120,float((i*37)%90),1280,720));
    double best=INFINITY;
    for(int run=0;run<20;++run){
        const auto start=std::chrono::steady_clock::now();
        auto out=g.plan(in,false,run*.05);
        best=std::min(best,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        assert(out.size()==in.size());
    }
    std::cout<<"  plan 512 windows: "<<best<<" ms\n";
#if defined(__SANITIZE_ADDRESS__) || !defined(__OPTIMIZE__)
    assert(best<5); // unoptimised and sanitizer builds
#else
    assert(best<.1);
#endif
}

int main(){
    helpers();
    s1cTable();
    tiers();
    pinned();
    hysteresis();
    overviewTable();
    neverExceeded();
    raiseHysteresis();
    placeHysteresis();
    calibration();
    gpuFeedback();
    vramCap();
    maxHzCap();
    forgetting();
    speed();
    std::cout<<"capture governor: ok\n";
}
