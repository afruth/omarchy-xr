#include "region_turns.hpp"
#include <cassert>
#include <iostream>
#include <vector>

// One request in flight per lane; a request is answered when the test says so, tagged in frame.width.
struct FakeLane {
    bool held=false, pending=false, answer=false; unsigned sent=0, tag=0;
    void hold(bool h) { held=h; }
    unsigned requests() const { return sent; }
    bool update(CapturedFrame& frame) {
        bool got=false;
        if(pending && answer) { frame.width=tag; pending=false; answer=false; got=true; }
        if(!held && !pending) { pending=true; ++sent; }
        return got;
    }
};
struct Rig {
    FakeLane a, b; RegionTurns<FakeLane> turns; CapturedFrame frame; std::vector<int> order;
    bool step(double now) {
        const unsigned sa=a.sent, sb=b.sent;
        const bool updated=turns.step({&a, &b}, frame, now);
        if(a.sent!=sa) order.push_back(0);
        if(b.sent!=sb) order.push_back(1);
        return updated;
    }
};

// Strict alternation and one request per period (less the 1 ms vblank slack) on a 1 ms loop.
static void alternation() {
    Rig r; r.turns.period=1000./60;
    unsigned tag=0; std::vector<double> at;
    for(double now=0;now<1000;now+=1) {
        for(auto* lane:{&r.a, &r.b}) if(lane->pending) { lane->answer=true; lane->tag=++tag; }
        const size_t before=r.order.size();
        r.step(now);
        assert(r.order.size()-before<=1);
        if(r.order.size()!=before) at.push_back(now);
    }
    assert(at.size()>=60);
    for(size_t i=1;i<at.size();++i) assert(at[i]-at[i-1]>=r.turns.period-1 && at[i]-at[i-1]<=r.turns.period+1);
    for(size_t i=0;i<r.order.size();++i) assert(r.order[i]==int(i%2));
}
// A lane whose answer is late holds its turn: nobody requests until it answers, then the other lane.
static void lateLaneHolds() {
    Rig r; r.turns.period=10;
    r.step(0); assert(r.order==std::vector<int>({0}));
    r.a.answer=true; r.a.tag=1; r.step(10); assert(r.order==std::vector<int>({0, 1}) && r.frame.width==1);
    // Lane 0's turn again, but it answered and is free; lane 1 is late and must not matter.
    r.a.answer=false;
    r.step(20); assert(r.order==std::vector<int>({0, 1, 0}));
    // Now lane 1 holds the turn while its answer is late; lane 0 waits with it.
    for(double now=30;now<80;now+=1) { r.a.answer=true; r.a.tag=9; r.step(now); }
    assert(r.order==std::vector<int>({0, 1, 0}) && r.b.pending);
    r.b.answer=true; r.b.tag=2; r.step(80);
    assert(r.order==std::vector<int>({0, 1, 0, 1}));
}
// Both lanes ready in one tick: the newer request's frame wins, whichever lane it is on.
static void newestWins() {
    Rig r; r.turns.period=10;
    r.step(0); r.step(10);                     // lane 0 asked at 0, lane 1 at 10
    r.a.answer=true; r.a.tag=100; r.b.answer=true; r.b.tag=200;
    assert(r.step(12) && r.frame.width==200 && r.turns.last==1);
    // Lane 0 asked at 20 and lane 1 at 30; lane 1 answers first, then lane 0's older frame is dropped.
    r.step(20); r.step(30);
    r.b.answer=true; r.b.tag=300; assert(r.step(31) && r.frame.width==300);
    r.a.answer=true; r.a.tag=400; assert(!r.step(32) && r.frame.width==300);
    // Lane 1 asks at 50 and lane 0 at 60; both answer in one tick and lane 0, first in order, wins.
    r.step(40); r.step(50); r.a.answer=true; r.a.tag=500; r.step(55); r.step(60);
    assert(r.order.back()==0 && r.frame.width==500);
    r.a.answer=true; r.a.tag=600; r.b.answer=true; r.b.tag=700;
    assert(r.step(62) && r.frame.width==600 && r.turns.last==0);
}

int main() {
    alternation(); lateLaneHolds(); newestWins();
    std::cout << "Region turns: strict alternation, one request per period, a late lane holds its turn, the newest answer wins\n";
}
