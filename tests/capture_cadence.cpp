#include "capture_cadence.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static bool near(double a,double b){ return std::abs(a-b)<1e-9; }

// Fire every lane whose slot is due on a 1 ms loop and return the request times.
static std::vector<double> simulate(const Cadence& c,double from,double to,double step=.001){
    std::vector<double> last(c.lanes,Cadence::never),fired;
    for(double now=from;now<to;now+=step)
        for(unsigned lane=0;lane<c.lanes;++lane){
            const double due=c.due(lane,now,last[lane]);
            if(due<=now){ fired.push_back(due); last[lane]=due; }
        }
    return fired;
}

static void lanes(){
    Cadence c; c.retune(60,2,.25);
    assert(c.lanes==2 && near(c.period,1./60) && near(c.phase,.25));
    // Lane i of K at epoch + phase*period + i*period + n*K*period.
    assert(near(c.nextDue(0,0),.25/60));
    assert(near(c.nextDue(0,-1e-3),.25/60));
    assert(near(c.nextDue(1,-1e-3),1.25/60));
    assert(near(c.nextDue(0,.25/60),2.25/60));   // strictly after
    const auto fired=simulate(c,0,1);
    // One request per period overall, evenly spaced; lane 1's slot 0.75 period before the start
    // is less than a period old, so it fires at once: 61 slots from -0.0125 s to 0.9875 s.
    assert(fired.size()==61 && near(fired[0],-.75/60));
    for(size_t i=1;i<fired.size();++i) assert(near(fired[i]-fired[i-1],1./60));
    Cadence one; one.retune(24,1,0);
    assert(simulate(one,0,1).size()==24);
    c.retune(60,5,1.5);
    assert(c.lanes==2 && near(c.phase,.5));
    c.retune(0,1,0);
    assert(!c.enabled() && std::isinf(c.due(0,1,Cadence::never)));
}

static void stalls(){
    Cadence c; c.retune(10,1,0);
    // Jitter within one period still fires the missed slot, keeping the cadence.
    assert(near(c.due(0,.15,.0),.1));
    // A long stall skips the missed slots: one request, at most one period late, and the slot stays
    // due until it fires.
    assert(near(c.due(0,1.234,.1),1.2) && near(c.due(0,1.25,.1),1.2));
    assert(near(c.due(0,1.25,1.2),1.3));
    // Two lanes after a stall: exactly one is due.
    Cadence two; two.retune(60,2,0);
    assert((two.due(0,10.001,.5)<=10.001)!=(two.due(1,10.001,.5)<=10.001));
    for(double now:{.0,.37,5.01}) assert(c.nextDue(0,now)>now);
    // First request fires at once when its slot is less than one period old.
    assert(c.due(0,.05,Cadence::never)<=.05);
}

static void retuneKeepsSpread(){
    // Three windows at 10 Hz spread over one period; a shared rate change to 24 keeps them apart.
    std::vector<Cadence> w(3);
    for(size_t i=0;i<w.size();++i) w[i].retune(10,1,Cadence::spread(i,w.size()));
    assert(near(Cadence::spread(1,3),1./3) && Cadence::spread(0,0)==0);
    std::vector<double> first;
    for(auto& c:w) first.push_back(c.nextDue(0,1.99999));
    assert(near(first[1]-first[0],.1/3) && near(first[2]-first[1],.1/3));
    for(size_t i=0;i<w.size();++i) w[i].retune(24,1,Cadence::spread(i,w.size()));
    std::vector<double> after;
    for(auto& c:w) after.push_back(c.nextDue(0,1.99999));
    assert(near(after[1]-after[0],1./72) && near(after[2]-after[1],1./72));
    // Retuning keeps the shared epoch.
    assert(near(w[0].epoch,0));
}

int main(){
    lanes();
    stalls();
    retuneKeepsSpread();
    std::cout<<"capture cadence: ok\n";
}
