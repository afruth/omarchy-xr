#pragma once
#include "capture_cadence.hpp"
#include "window_list.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

// Capture rates per window, the fixed M2 profile of docs/infinite-canvas-plan.md §4.4 (S1b):
// the staged window at 60 Hz with two lanes, up to four near windows at 24, the other visible
// windows at 10, off-screen windows idle, and every visible window at 10 in the overview (6 when
// 10 would exceed the exported-pixel budget; S1b ran 50 x 720p clean at 6, not at 10). The ladder,
// self-calibration and GPU feedback arrive in M5.
namespace governor {
enum class Tier { Focused, Near, Far, Overview, Idle };
struct Input {
    std::string name;
    bool staged=false, visible=false;
    float angularWidthDeg=0;
    int focusHistoryID=0;
    unsigned pixelW=0, pixelH=0;
    bool pinned=false;   // body-locked (§5.1): always a candidate, ranked first, so it holds a Near place
};
struct Decision {
    Tier tier=Tier::Idle;
    unsigned rateHz=0, inFlight=1;
    double phase=0;
    bool ignoreDamage=true;
    windows::Place place=windows::Place::Park;
};
inline constexpr unsigned focusedHz=60, nearHz=24, farHz=10, overviewHz=10, overviewSlowHz=6, nearCount=4, nearKeep=6;
inline constexpr double nearEntry=.5, nearExit=1;

struct Fixed {
    double budgetMpix=300;
    unsigned maxHz=focusedHz;   // canvas.tsv fps: caps every tier (two lanes only above 30 Hz)
    // nearSince: when a window became continuously top-4 (it qualifies for Near 0.5 s later);
    // farSince: when a Near window left the top 6 (it is demoted 1 s later).
    std::map<std::string,double> nearSince, farSince;
    std::set<std::string> near;
    void forget(const std::string& name){ nearSince.erase(name); farSince.erase(name); near.erase(name); }
    static Decision focused(){ return {Tier::Focused,focusedHz,2,0,false,windows::Place::Stage}; }
    static Decision rated(Tier tier,unsigned hz){ return {tier,hz,hz>30 ? 2u : 1u,0,true,windows::Place::Park}; }

    std::vector<Decision> plan(const std::vector<Input>& in,bool zoomedOut,double now){
        std::vector<Decision> out(in.size());
        std::vector<std::size_t> candidates;
        for(std::size_t i=0;i<in.size();++i){
            if(in[i].staged){ out[i]=focused(); forget(in[i].name); }
            else if(!in[i].visible && !in[i].pinned) forget(in[i].name);
            else candidates.push_back(i);
        }
        if(zoomedOut) overview(in,candidates,out);
        else zoomedIn(in,candidates,out,now);
        for(auto& d:out) if(d.rateHz>maxHz){ d.rateHz=maxHz; d.inFlight=maxHz>30 ? d.inFlight : 1; }
        spreadPhases(in,out);
        return out;
    }
    void overview(const std::vector<Input>& in,const std::vector<std::size_t>& candidates,std::vector<Decision>& out){
        double pixels=0;
        for(auto i:candidates) pixels+=double(in[i].pixelW)*in[i].pixelH;
        const unsigned hz=pixels*overviewHz<=budgetMpix*1e6 ? overviewHz : overviewSlowHz;
        for(auto i:candidates) out[i]=rated(Tier::Overview,hz);
        // Pinned windows always count as near (§5.1): body-locked, they stay readable while zoomed out.
        std::vector<std::size_t> pinned;
        for(auto i:candidates) if(in[i].pinned) pinned.push_back(i);
        rank(in,pinned);
        for(std::size_t r=0;r<pinned.size() && r<nearCount;++r) out[pinned[r]]=rated(Tier::Near,nearHz);
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
    void zoomedIn(const std::vector<Input>& in,std::vector<std::size_t> order,std::vector<Decision>& out,double now){
        rank(in,order);
        std::vector<std::size_t> entrants;
        for(std::size_t r=0;r<order.size();++r){
            const auto& name=in[order[r]].name;
            if(near.count(name)){
                if(r<nearKeep){ farSince.erase(name); continue; }
                const double since=farSince.try_emplace(name,now).first->second;
                if(now-since>=nearExit) forget(name);
            }else if(r<nearCount){
                if(now-nearSince.try_emplace(name,now).first->second>=nearEntry) entrants.push_back(r);
            }else nearSince.erase(name);
        }
        // A qualified entrant waits for a free place, so Near never exceeds four.
        for(auto r:entrants) if(near.size()<nearCount) near.insert(in[order[r]].name);
        for(auto i:order) out[i]=rated(near.count(in[i].name) ? Tier::Near : Tier::Far,near.count(in[i].name) ? nearHz : farHz);
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
