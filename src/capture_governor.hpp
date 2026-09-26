#pragma once
#include "capture_cadence.hpp"
#include "window_list.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

// Capture rates per window: the pixel-budget ladder of docs/infinite-canvas-plan.md §4.4 (S1c).
// The staged window runs 60 Hz on two lanes and is counted first; the other visible windows share
// the rest of the exported-pixel budget tier by tier (Near, then Far or Overview), each tier at one
// ladder step, the highest that fits with a 6 Hz floor kept for the tier below. When even 6 Hz
// does not fit, the lowest-ranked windows go idle: the budget is never exceeded. Lowering is
// immediate, raising waits 2 s under 90 % of the budget, and a window rated above 30 Hz becomes a
// live sliver (park pull tops out at ≈ 30 Hz) with at most one place change per 2 s.
namespace governor {
enum class Tier { Focused, Near, Far, Overview, Idle };
struct Input {
    std::string name;
    bool staged=false, visible=false;
    float angularWidthDeg=0;
    int focusHistoryID=0;
    unsigned pixelW=0, pixelH=0;
    bool pinned=false;   // body-locked (§5.1): always a candidate, ranked first, so it holds a Near place
    unsigned demandW=1, demandH=1;   // presented size, for the VRAM estimate
    bool shownAsSliver=false;   // Lua's `.windows` row confirms the move: only then is capture damage-driven
};
struct Decision {
    Tier tier=Tier::Idle;
    unsigned rateHz=0, inFlight=1;
    double phase=0;
    bool ignoreDamage=true;
    windows::Place place=windows::Place::Park;
};
inline constexpr unsigned steps[]={60,40,30,24,20,15,10,6};
// Pinned windows never move place, so the park pull ceiling caps them.
inline constexpr unsigned focusedHz=60, nearCapHz=40, farCapHz=10, overviewCapHz=10, parkCapHz=30, sliverAboveHz=30, floorHz=6,
    nearCount=4, nearKeep=6;
inline constexpr double nearEntry=.5, nearExit=1, raiseAfter=2.0, raiseBelow=.9, placeHold=2.0;
inline constexpr double vramCapBytes=double(512u<<20), thumbnailBytes=256*256*4;
// Request→ready thresholds in output frames. A healthy export completes on the second output frame
// (33.2-33.3 ms p50 at 60 Hz in every M5 live run, 207-290 Mpix/s); an overloaded compositor shows
// 45-106 ms (S1b/S1c). Shrinking at 2 frames sat on the healthy value and ratcheted the budget down.
// A view change that starts dozens of sessions at once (Overview of 42 windows) lifts p50 over the
// threshold for one evaluation only, so shrinking waits for the second evaluation in a row.
inline constexpr double readyShrinkFrames=2.5, readyGrowFrames=2.25;
inline constexpr int readyOverEvaluations=2;

inline double percentile(std::vector<double> v,unsigned num,unsigned den){
    std::sort(v.begin(),v.end());
    return v[(v.size()-1)*num/den];
}

// The effective budget: the Studio setting, shrunk by request→ready self-calibration (never above
// the setting) and by GPU pressure (the SpectatorGovernor recipe, 25 % per step). Times are passed in.
struct Budget {
    double setMpix=300, calibration=1;
    int gpuSteps=0;
    bool panic=false;       // GPU p80 over 85 %: every non-focused window at 6 Hz
    double refreshHz=60;    // canvas output refresh: one output frame for the calibration thresholds
    double readyP50Ms=-1;   // last evaluated request→ready p50, for stats
    std::vector<double> ready, gpu;
    double readySince=-1, calmSince=-1, gpuStepAt=-1e9, gpuCalmSince=-1;
    int readyOver=0;        // evaluations in a row over the shrink threshold
    std::size_t gpuNext=0;
    static constexpr std::size_t gpuWindow=30;
    double effective() const { return setMpix*calibration*std::pow(.75,gpuSteps); }

    // Every second with >= 8 samples: a p50 over 2.5 output frames from the second such second on shrinks
    // by 10 % per second (floor 0.5); under 2.25 frames for 5 s grows by 2 % per second back to the setting.
    void readySample(double ms,double now){
        if(readySince<0) readySince=now;
        ready.push_back(ms);
        if(now-readySince<1 || ready.size()<8) return;
        readyP50Ms=percentile(ready,1,2);
        const double frameMs=1000/refreshHz;
        const bool over=readyP50Ms>readyShrinkFrames*frameMs;
        readyOver=over ? readyOver+1 : 0;
        if(over){ if(readyOver>=readyOverEvaluations) calibration=std::max(.5,calibration*.9); calmSince=-1; }
        else if(readyP50Ms<readyGrowFrames*frameMs){
            if(calmSince<0) calmSince=readySince;
            if(now-calmSince>=5) calibration=std::min(1.0,calibration*1.02);
        }else calmSince=-1;
        ready.clear(); readySince=now;
    }
    // p80 of the last 30 frame GPU times over the render period: > 60 % one step per 350 ms (at most
    // five), > 85 % panic; one step back per 2 s under 45 %.
    void gpuSample(double frameGpuMs,double periodMs,double now){
        if(periodMs<=0) return;
        if(gpu.size()<gpuWindow) gpu.push_back(frameGpuMs); else gpu[gpuNext]=frameGpuMs;
        gpuNext=(gpuNext+1)%gpuWindow;
        if(gpu.size()<10) return;
        const double load=percentile(gpu,4,5)/periodMs;
        if(load>.60){
            gpuCalmSince=-1;
            if(load>.85) panic=true;
            if(now-gpuStepAt>=.35 && gpuSteps<5){ ++gpuSteps; gpuStepAt=now; }
        }else if(load<.45 && (gpuSteps>0 || panic)){
            if(gpuCalmSince<0) gpuCalmSince=now;
            else if(now-gpuCalmSince>=2){ gpuSteps=std::max(0,gpuSteps-1); panic=false; gpuCalmSince=now; }
        }else gpuCalmSince=-1;
    }
};

struct Ladder {
    struct Slot { std::size_t i; double px; unsigned cap; Tier tier; };
    Budget budget;
    unsigned maxHz=focusedHz;   // canvas.tsv fps: caps every tier (two lanes only above 30 Hz)
    // nearSince: when a window became continuously top-4 (it qualifies for Near 0.5 s later);
    // farSince: when a Near window left the top 6 (it is demoted 1 s later).
    std::map<std::string,double> nearSince, farSince;
    std::set<std::string> near;
    std::map<std::string,unsigned> granted;   // the rate in force; raises wait for raiseAfter
    std::map<std::string,double> movedAt;     // last park ↔ sliver change
    std::set<std::string> slivers;
    double hotSince=-1e9;
    double usedMpix=0, vramBytes=0;   // outputs for stats

    void leaveNear(const std::string& name){ nearSince.erase(name); farSince.erase(name); near.erase(name); }
    void forget(const std::string& name){ leaveNear(name); granted.erase(name); movedAt.erase(name); slivers.erase(name); }
    static double cost(const Input& w,unsigned hz){ return double(w.pixelW)*w.pixelH*hz; }
    static unsigned stepBelow(double hz){
        for(unsigned s:steps) if(s<=hz) return s;
        return 0;
    }
    static double tierCost(const std::vector<Slot>& tier,unsigned hz){
        double sum=0;
        for(const auto& s:tier) sum+=s.px*std::min(hz,s.cap);
        return sum;
    }
    // The tier's uniform step: the highest ladder step within its caps that fits room; 0 when not even the floor does.
    static unsigned fit(const std::vector<Slot>& tier,double room){
        unsigned top=0;
        for(const auto& s:tier) top=std::max(top,s.cap);
        for(unsigned hz:steps) if((hz<=top || hz==floorHz) && tierCost(tier,hz)<=room) return hz;
        return 0;
    }

    std::vector<Decision> plan(const std::vector<Input>& in,bool zoomedOut,double now){
        std::vector<Decision> out(in.size());
        prune(in);
        std::vector<std::size_t> candidates;
        double fixed=0;
        const unsigned stagedHz=std::min(focusedHz,maxHz);
        for(std::size_t i=0;i<in.size();++i){
            if(in[i].staged){ fixed+=cost(in[i],stagedHz); leaveNear(in[i].name); granted.erase(in[i].name); }
            else if(!in[i].visible && !in[i].pinned){ leaveNear(in[i].name); granted.erase(in[i].name); }
            else candidates.push_back(i);
        }
        rank(in,candidates);
        std::vector<Slot> upper, lower;
        if(zoomedOut) overview(in,candidates,upper,lower);
        else zoomedIn(in,candidates,upper,lower,now);
        std::vector<unsigned> rate(in.size(),0);
        waterFill(upper,lower,fixed,rate);
        hold(in,upper,lower,fixed,rate,now);
        capVram(in,upper,lower,stagedHz,rate);
        finish(in,upper,lower,stagedHz,rate,now,out);
        spreadPhases(in,out);
        usedMpix=0;
        for(std::size_t i=0;i<in.size();++i) usedMpix+=cost(in[i],out[i].rateHz)/1e6;
        return out;
    }
    // Windows gone from the input (closed) leave every map; cheap unless something is stale.
    void prune(const std::vector<Input>& in){
        if(granted.size()<=in.size() && movedAt.size()<=in.size() && slivers.size()<=in.size() && nearSince.size()<=in.size()) return;
        std::set<std::string> names, stale;
        for(const auto& w:in) names.insert(w.name);
        for(const auto& [n,_]:granted) if(!names.count(n)) stale.insert(n);
        for(const auto& [n,_]:movedAt) if(!names.count(n)) stale.insert(n);
        for(const auto& [n,_]:nearSince) if(!names.count(n)) stale.insert(n);
        for(const auto& n:slivers) if(!names.count(n)) stale.insert(n);
        for(const auto& n:stale) forget(n);
    }
    unsigned cap(unsigned tierCap) const { return std::min({tierCap,maxHz,budget.panic ? floorHz : tierCap}); }
    Slot slot(const std::vector<Input>& in,std::size_t i,Tier tier) const {
        const unsigned tierCap=tier==Tier::Near ? (in[i].pinned ? parkCapHz : nearCapHz) : tier==Tier::Far ? farCapHz : overviewCapHz;
        return {i,double(in[i].pixelW)*in[i].pixelH,cap(tierCap),tier};
    }
    // Zoomed out: pinned windows stay Near (§5.1, body-locked and readable), the rest is Overview.
    void overview(const std::vector<Input>& in,const std::vector<std::size_t>& ranked,std::vector<Slot>& upper,std::vector<Slot>& lower){
        for(auto i:ranked){
            if(in[i].pinned && upper.size()<nearCount) upper.push_back(slot(in,i,Tier::Near));
            else lower.push_back(slot(in,i,Tier::Overview));
        }
        nearSince.clear(); farSince.clear(); near.clear();
    }
    // Pinned first, then by angular size, then most recent use; ties by name keep the order deterministic.
    static void rank(const std::vector<Input>& in,std::vector<std::size_t>& order){
        std::sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){
            if(in[a].pinned!=in[b].pinned) return in[a].pinned;
            if(in[a].angularWidthDeg!=in[b].angularWidthDeg) return in[a].angularWidthDeg>in[b].angularWidthDeg;
            if(in[a].focusHistoryID!=in[b].focusHistoryID) return in[a].focusHistoryID<in[b].focusHistoryID;
            return in[a].name<in[b].name;
        });
    }
    void zoomedIn(const std::vector<Input>& in,const std::vector<std::size_t>& order,std::vector<Slot>& upper,std::vector<Slot>& lower,double now){
        std::vector<std::size_t> entrants;
        for(std::size_t r=0;r<order.size();++r){
            const auto& name=in[order[r]].name;
            if(near.count(name)){
                if(r<nearKeep){ farSince.erase(name); continue; }
                const double since=farSince.try_emplace(name,now).first->second;
                if(now-since>=nearExit) leaveNear(name);
            }else if(r<nearCount){
                if(now-nearSince.try_emplace(name,now).first->second>=nearEntry) entrants.push_back(r);
            }else nearSince.erase(name);
        }
        // A qualified entrant waits for a free place, so Near never exceeds four.
        for(auto r:entrants) if(near.size()<nearCount) near.insert(in[order[r]].name);
        for(auto i:order){
            if(near.count(in[i].name)) upper.push_back(slot(in,i,Tier::Near));
            else lower.push_back(slot(in,i,Tier::Far));
        }
    }
    // Wanted rates: Near at the highest step that leaves the floor for the lower tier, then the lower
    // tier at the highest step that fits; when the floor does not fit, trim from the bottom of the rank.
    void waterFill(const std::vector<Slot>& upper,const std::vector<Slot>& lower,double fixed,std::vector<unsigned>& rate) const {
        const double total=budget.effective()*1e6;
        const unsigned up=fit(upper,total-fixed-tierCost(lower,floorHz)), upHz=up ? up : floorHz;
        double used=fixed+tierCost(upper,upHz);
        const unsigned lo=fit(lower,total-used), loHz=lo ? lo : floorHz;
        for(const auto& s:upper) rate[s.i]=std::min(upHz,s.cap);
        for(const auto& s:lower) rate[s.i]=std::min(loHz,s.cap);
        used+=tierCost(lower,loHz);
        for(const auto* tier:{&lower,&upper})
            for(auto s=tier->rbegin();s!=tier->rend() && used>total;++s){ used-=s->px*rate[s->i]; rate[s->i]=0; }
    }
    // Lower at once; raise only after raiseAfter seconds with nothing lowered and the sum under
    // raiseBelow of the budget. A window with no rate in force (new, back in view, idled) takes its wanted rate.
    void hold(const std::vector<Input>& in,const std::vector<Slot>& upper,const std::vector<Slot>& lower,double fixed,std::vector<unsigned>& rate,double now){
        bool lowered=false;
        for(const auto* tier:{&upper,&lower})
            for(const auto& s:*tier){
                const auto g=granted.find(in[s.i].name);
                lowered|=g!=granted.end() && rate[s.i]<g->second;
            }
        if(lowered) hotSince=now;
        const bool raise=now-hotSince>=raiseAfter;
        double sum=fixed;
        for(const auto* tier:{&upper,&lower})
            for(const auto& s:*tier){
                const auto& name=in[s.i].name;
                if(!rate[s.i]){ granted.erase(name); continue; }
                const auto g=granted.find(name);
                if(g!=granted.end() && rate[s.i]>g->second && !raise) rate[s.i]=g->second;
                granted[name]=rate[s.i];
                sum+=s.px*rate[s.i];
            }
        if(sum>=raiseBelow*budget.effective()*1e6) hotSince=now;
    }
    static double lanes(unsigned hz){ return hz>30 ? 2 : 1; }
    static double textureBytes(const Input& w){ return 4.0*std::min(w.demandW,w.pixelW)*std::min(w.demandH,w.pixelH); }
    // VRAM estimate (§4.4): lanes × 2 GpuCapture slots × 4 B × native pixels plus the presented texture
    // (the staged window adds its two region lanes); idle windows keep a thumbnail, counted up front for
    // every window. In rank order, windows past the cap go idle; the staged window never does.
    void capVram(const std::vector<Input>& in,const std::vector<Slot>& upper,const std::vector<Slot>& lower,unsigned stagedHz,std::vector<unsigned>& rate){
        double sum=0;
        for(const auto& w:in)
            sum+=w.staged ? (lanes(stagedHz)+2)*2*4*double(w.pixelW)*w.pixelH+textureBytes(w) : thumbnailBytes;
        for(const auto* tier:{&upper,&lower})
            for(const auto& s:*tier){
                if(!rate[s.i]) continue;
                const double bytes=lanes(rate[s.i])*2*4*s.px+textureBytes(in[s.i])-thumbnailBytes;
                if(sum+bytes<=vramCapBytes){ sum+=bytes; continue; }
                rate[s.i]=0; granted.erase(in[s.i].name);
            }
        vramBytes=sum;
    }
    // Sliver above 30 Hz (never staged or pinned), else park; one change per placeHold seconds.
    windows::Place place(const Input& w,unsigned hz,double now){
        const bool want=!w.pinned && hz>sliverAboveHz, is=slivers.count(w.name)>0;
        if(want!=is){
            const auto moved=movedAt.find(w.name);
            if(moved==movedAt.end() || now-moved->second>=placeHold){
                if(want) slivers.insert(w.name); else slivers.erase(w.name);
                movedAt[w.name]=now;
            }
        }
        return slivers.count(w.name) ? windows::Place::Sliver : windows::Place::Park;
    }
    // Two lanes above 30 Hz; damage-driven where Hyprland shows the window (stage, a confirmed sliver), pulled
    // otherwise: a copy waiting for damage on a still-parked window would stall until Lua moves it.
    void finish(const std::vector<Input>& in,const std::vector<Slot>& upper,const std::vector<Slot>& lower,unsigned stagedHz,
                const std::vector<unsigned>& rate,double now,std::vector<Decision>& out){
        std::vector<Tier> tier(in.size(),Tier::Idle);
        for(const auto* t:{&upper,&lower}) for(const auto& s:*t) if(rate[s.i]) tier[s.i]=s.tier;
        for(std::size_t i=0;i<in.size();++i){
            if(in[i].staged){ slivers.erase(in[i].name); out[i]={Tier::Focused,stagedHz,stagedHz>30 ? 2u : 1u,0,false,windows::Place::Stage}; continue; }
            const auto p=place(in[i],rate[i],now);
            out[i]={tier[i],rate[i],rate[i]>30 ? 2u : 1u,0,!(p==windows::Place::Sliver && in[i].shownAsSliver),p};
        }
    }
    // Windows sharing a rate, sorted by name, get evenly spread phases (S1b: 42 ms render spikes without).
    static void spreadPhases(const std::vector<Input>& in,std::vector<Decision>& out){
        std::map<unsigned,std::vector<std::size_t>> byRate;
        for(std::size_t i=0;i<out.size();++i)
            if(out[i].tier!=Tier::Focused && out[i].rateHz) byRate[out[i].rateHz].push_back(i);
        for(auto& [hz,group]:byRate){
            std::sort(group.begin(),group.end(),[&](std::size_t a,std::size_t b){ return in[a].name<in[b].name; });
            for(std::size_t k=0;k<group.size();++k) out[group[k]].phase=Cadence::spread(k,group.size());
        }
    }
};
}
