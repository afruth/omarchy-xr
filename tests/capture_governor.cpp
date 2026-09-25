#include "capture_governor.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>

using namespace governor;

static std::string name(int i){ return "0x"+std::to_string(1000+i); }

// 1 staged, 10 visible with distinct angular sizes, 39 off-screen.
static std::vector<Input> desk(){
    std::vector<Input> in;
    in.push_back({name(0),true,true,40,0,1920,1080});
    for(int i=1;i<=10;++i) in.push_back({name(i),false,true,float(i%5==0 ? 30 : 5+i),i,1280,720});
    for(int i=11;i<50;++i) in.push_back({name(i),false,false,0,i,1280,720});
    return in;
}

static void phasesSpread(const std::vector<Decision>& out,unsigned hz){
    std::vector<double> phases;
    for(size_t i=0;i<out.size();++i) if(out[i].rateHz==hz && out[i].tier!=Tier::Focused) phases.push_back(out[i].phase);
    std::sort(phases.begin(),phases.end());
    for(size_t k=0;k<phases.size();++k) assert(std::abs(phases[k]-double(k)/phases.size())<1e-12);
}

static void tiers(){
    Fixed g; auto in=desk();
    auto out=g.plan(in,false,0);
    // Near needs 0.5 s in the top 4; until then every visible window is Far.
    for(size_t i=1;i<=10;++i) assert(out[i].tier==Tier::Far);
    out=g.plan(in,false,.6);
    unsigned focused=0,nearN=0,far=0,idle=0;
    std::set<std::string> nearNames;
    for(size_t i=0;i<out.size();++i) switch(out[i].tier){
        case Tier::Focused: ++focused; assert(out[i].rateHz==60 && out[i].inFlight==2 && out[i].phase==0 && !out[i].ignoreDamage && out[i].place==windows::Place::Stage); break;
        case Tier::Near: ++nearN; nearNames.insert(in[i].name); assert(out[i].rateHz==24 && out[i].inFlight==1 && out[i].ignoreDamage && out[i].place==windows::Place::Park); break;
        case Tier::Far: ++far; assert(out[i].rateHz==10 && out[i].inFlight==1 && out[i].ignoreDamage); break;
        case Tier::Idle: ++idle; assert(out[i].rateHz==0); break;
        case Tier::Overview: assert(false);
    }
    assert(focused==1 && nearN==4 && far==6 && idle==39);
    // Largest angular size wins: 5 and 10 at 30 deg, then 9 at 14 and 8 at 13.
    assert(nearNames==(std::set<std::string>{name(5),name(10),name(9),name(8)}));
    phasesSpread(out,24); phasesSpread(out,10);
    // MRU breaks an angular-size tie: equal sizes, the lower focus_history_id wins.
    Fixed tie; std::vector<Input> same;
    for(int i=0;i<6;++i) same.push_back({name(i),false,true,20,5-i,800,600});
    tie.plan(same,false,0); out=tie.plan(same,false,1);
    for(int i=0;i<6;++i) assert((out[i].tier==Tier::Near)==(i>=2));
}

static void hysteresis(){
    Fixed g; auto in=desk();
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

static void overview(){
    Fixed g;
    std::vector<Input> in;
    for(int i=0;i<30;++i) in.push_back({name(i),false,true,3,i,1280,720});
    auto out=g.plan(in,true,0);
    for(auto& d:out) assert(d.tier==Tier::Overview && d.rateHz==10 && d.inFlight==1 && d.ignoreDamage);
    phasesSpread(out,10);
    for(int i=30;i<50;++i) in.push_back({name(i),false,true,3,i,1280,720});
    out=g.plan(in,true,1);   // 50 x 720p x 10 Hz = 460 Mpix/s > 300
    for(auto& d:out) assert(d.tier==Tier::Overview && d.rateHz==6);
    phasesSpread(out,6);
    in[0].staged=true; in[3].visible=false;
    out=g.plan(in,true,2);
    assert(out[0].tier==Tier::Focused && out[3].tier==Tier::Idle && out[1].tier==Tier::Overview);
}

// canvas.tsv fps caps every tier; at 30 Hz or less the staged window runs one lane.
static void rateCap(){
    Fixed g; g.maxHz=20; auto in=desk();
    g.plan(in,false,0); auto out=g.plan(in,false,.6);
    for(auto& d:out) assert(d.rateHz<=20);
    assert(out[0].tier==Tier::Focused && out[0].rateHz==20 && out[0].inFlight==1);
    assert(out[8].tier==Tier::Near && out[8].rateHz==20 && out[1].tier==Tier::Far && out[1].rateHz==10);
    g.maxHz=40; out=g.plan(in,false,.7);
    assert(out[0].rateHz==40 && out[0].inFlight==2 && out[8].rateHz==24);
}

static void speed(){
    Fixed g; std::vector<Input> in;
    for(int i=0;i<512;++i) in.push_back({name(i),i==0,i<120,float((i*37)%90),i,1280,720});
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

// Pinned windows rank first: Near even when smallest or off-screen, and Near still never exceeds four.
static void pinned(){
    Fixed g; auto in=desk();
    in[1].pinned=true;                                  // 6 deg, the smallest visible window
    in[20].pinned=true;                                 // off-screen
    g.plan(in,false,0); auto out=g.plan(in,false,.6);
    unsigned nearN=0;
    for(const auto& d:out) nearN+=d.tier==Tier::Near;
    assert(out[1].tier==Tier::Near && out[20].tier==Tier::Near && nearN==4 && out[0].tier==Tier::Focused);
    for(int i=2;i<=10;++i) in[i].pinned=true;
    g.plan(in,false,1); out=g.plan(in,false,2);
    nearN=0; for(const auto& d:out) nearN+=d.tier==Tier::Near;
    assert(nearN==4);
}

// Zoomed out, pinned windows still count as near (off-screen too); the others keep the overview rate.
static void pinnedZoomedOut(){
    Fixed g; auto in=desk();
    in[3].pinned=true; in[30].pinned=true;
    const auto out=g.plan(in,true,0);
    assert(out[3].tier==Tier::Near && out[3].rateHz==nearHz && out[30].tier==Tier::Near && out[30].rateHz==nearHz);
    assert(out[0].tier==Tier::Focused && out[1].tier==Tier::Overview && out[1].rateHz==overviewHz && out[31].tier==Tier::Idle);
    for(int i=2;i<=10;++i) in[i].pinned=true;
    unsigned nearN=0; for(const auto& d:g.plan(in,true,1)) nearN+=d.tier==Tier::Near;
    assert(nearN==nearCount);
}

int main(){
    tiers();
    pinned();
    pinnedZoomedOut();
    hysteresis();
    overview();
    rateCap();
    speed();
    std::cout<<"capture governor: ok\n";
}
