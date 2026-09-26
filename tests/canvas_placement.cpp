#include "canvas_placement.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <set>
using namespace canvas;
static const Ring ring{};
static const Metrics m=metrics(ring, Fov{});
static constexpr float gap=60;

static void assertApart(const std::vector<Placed>& placed) {
    for(size_t i=0;i<placed.size();++i) {
        const auto& r=placed[i].rect;
        assert(r.x>=0 && r.x<ring.period());
        for(size_t j=i+1;j<placed.size();++j) assert(!overlaps(r, placed[j].rect, gap, ring.period()));
    }
}
static std::vector<Placed> sequence(unsigned seed, int count) {
    std::mt19937 rng(seed);
    std::vector<Placed> taken;
    for(int i=0;i<count;++i) {
        const float w=std::uniform_real_distribution<float>(200, 1400)(rng), h=std::uniform_real_distribution<float>(150, 780)(rng);
        const float x=std::uniform_real_distribution<float>(0, ring.period())(rng), y=std::uniform_real_distribution<float>(-3000, 3000)(rng);
        if(auto r=placeNew(w, h, x, y, taken, ring, gap)) { assert(r->w==w && r->h==h); taken.push_back({"0x"+std::to_string(i), *r}); }
    }
    return taken;
}
static void placement() {
    assert(overlaps({0,0,100,100}, {150,0,100,100}, 60, 0) && !overlaps({0,0,100,100}, {160,0,100,100}, 60, 0));
    assert(overlaps({ring.period()-50,0,100,100}, {20,0,100,100}, 0, ring.period()));      // across the seam
    assert(overlaps({20,0,100,100}, {ring.period()-50,0,100,100}, 0, ring.period()));
    assert(overlaps({5,0,100,100}, {5+3*ring.period(),0,100,100}, 0, ring.period()));
    assert(!overlaps({0,0,100,100}, {0,200,100,100}, 60, ring.period()));
    const auto s=snap({11,-29,7,9}); assert(s.x==20 && s.y==-20 && s.w==7 && s.h==9);
    const auto placed=sequence(5, 500);
    assert(placed.size()>=60);
    assertApart(placed);
    const auto again=sequence(5, 500);
    assert(again.size()==placed.size());
    for(size_t i=0;i<placed.size();++i) {
        const auto &a=placed[i].rect, &b=again[i].rect;
        assert(placed[i].name==again[i].name && a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h);
    }
    const auto first=placeNew(1000, 600, ring.period()-100, 0, {}, ring, gap);
    assert(first && first->x>=0 && first->x<ring.period());
    assert(placeNew(1000, 600, 0, 0, {}, ring, gap)->w==1000);
    assert(placeNew(20000, 600, 0, 0, {}, ring, gap)->w==ring.period()/2);
    const Rect blocker{-500,-500,1000,1000};
    const auto tall=placeNew(800, 4000, 0, 0, {{"a",blocker}}, ring, gap);   // beside the blocker at eye height
    assert(tall && tall->cy()==0 && !overlaps(*tall, blocker, gap, ring.period()));
    // A full band around the ring: the spiral steps above or below it (M8: no row band).
    const std::vector<Placed> band{{"a",{0,-2000,ring.period()/2,4000}}, {"b",{ring.period()/2+100,-2000,ring.period()/2-200,4000}}};
    const auto beyond=placeNew(800, 4000, 0, 900, band, ring, gap);
    assert(beyond && freeAt(*beyond, band, gap, ring.period()) && (beyond->y>=2000+gap || beyond->y+beyond->h<=-2000-gap));
    // M8: any height, starting at the look point.
    const auto high=placeNew(800, 600, 0, 5000, {}, ring, gap);
    assert(high && std::abs(high->cy()-5000)<=20);
    // The nearest free spiral cell around a blocker at the start (brute force over rings 1..2).
    const std::vector<Placed> one{{"a",blocker}};
    const auto next=placeNew(400, 300, 0, 0, one, ring, gap);
    assert(next && freeAt(*next, one, gap, ring.period()));
    const float nx=ring.wrap(next->cx()), d2=nx*nx+next->cy()*next->cy();
    for(int i=-2;i<=2;++i) for(int j=-2;j<=2;++j) {
        Rect c=snap({-200+i*(400+gap), -150+j*(300+gap), 400, 300});
        if(freeAt(c, one, gap, ring.period())) assert(d2<=c.cx()*c.cx()+c.cy()*c.cy()+1);
    }
    // Blocked left and right at eye height: down (j=+1) before up.
    const std::vector<Placed> sides{{"a",blocker}, {"l",{-1000-460-gap,-500,1000,1000}}, {"r",{500+460+gap,-500,1000,1000}}};
    const auto below=placeNew(400, 300, 0, 0, sides, ring, gap);
    assert(below && std::abs(ring.wrap(below->cx()))<=10 && below->cy()>0);
    const auto cells=spiralCells(2, 400+gap, 300+gap, ring.period());
    assert(cells.size()==24 && cells[0]==std::make_pair(0,1) && cells[1]==std::make_pair(0,-1) && cells[2]==std::make_pair(1,0));
    // Equal steps: ties by eye height, then right, then down.
    const std::vector<std::pair<int,int>> square{{1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1}};
    assert(spiralCells(1, 100, 100, ring.period())==square && spiralCells(3, 5000, 100, ring.period()).size()==7*3-1);
}
static void dialogs() {
    const Rect parent{1000, -400, 1600, 800};
    std::vector<Placed> taken{{"parent", parent, 7}};
    const auto dialog=placeNew(600, 300, 5000, 0, taken, ring, gap, parent);
    assert(dialog && !overlaps(*dialog, parent, gap, ring.period()));
    // Adjacent: within one step of the parent's centre in each axis.
    assert(std::abs(dialog->cx()-parent.cx())<=parent.w/2+600+gap+20 && std::abs(dialog->cy()-parent.cy())<=300+gap+20+parent.h/2);
    assert(std::abs(dialog->cx()-parent.cx())<5000/2);
    const auto empty=placeNew(600, 300, 5000, 0, {}, ring, gap, parent);
    assert(empty && std::abs(empty->cx()-parent.cx())<=10 && std::abs(empty->cy()-parent.cy())<=10);
}
static void arranging() {
    std::mt19937 rng(9);
    std::vector<Placed> windows;
    std::vector<std::string> mru;
    static const char* classes[]={"foot", "firefox", "code", "slack", "obsidian"};
    for(int i=0;i<200;++i) {
        const float w=std::uniform_real_distribution<float>(120, 400)(rng), h=std::uniform_real_distribution<float>(100, 360)(rng);
        windows.push_back({"0x"+std::to_string(1000+i), {float(rng()%20000), float(rng()%3000)-1500, w, h}, 0, classes[rng()%5]});
        if(i%3==0) mru.push_back(windows.back().name);
    }
    windows[0].rect.h=1200; // taller than a row: centred on its row
    std::vector<Placed> out; double ms=INFINITY;
    for(int run=0;run<3;++run) {
        const auto start=std::chrono::steady_clock::now();
        out=arrange(windows, 4000, 0, ring, m, gap, mru);
        ms=std::min(ms, std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    }
    assert(out.size()==windows.size());
    assertApart(out);
    // A block around the look point: within the arc, on contiguous grid rows around the gaze row.
    const float arcW=arrangeArc(windows, m, ring, gap);
    assert(arcW>=m.viewW && arcW<ring.period());
    std::set<int> rows;
    for(const auto& w:out) {
        assert(std::abs(ring.wrap(w.rect.cx()-4000))<=arcW/2+w.rect.w/2+1);
        rows.insert(rowFor(w.rect.cy(), m));
        const auto src=std::find_if(windows.begin(), windows.end(), [&](const Placed& p) { return p.name==w.name; });
        assert(src!=windows.end() && src->rect.w==w.rect.w && src->rect.h==w.rect.h && src->cls==w.cls);
    }
    assert(rows.count(0) && *rows.rbegin()-*rows.begin()+1==int(rows.size()) && rows.size()>1);
    // The most recent window's group leads at the arc's left edge.
    assert(out[0].cls==windows[0].cls && out[0].rect.x==std::round(4000-arcW/2));
    const auto twice=arrange(windows, 4000, 0, ring, m, gap, mru);
    for(size_t i=0;i<out.size();++i) assert(out[i].name==twice[i].name && out[i].rect.x==twice[i].rect.x && out[i].rect.y==twice[i].rect.y);
    // Gazing two rows down moves the whole block by two rows.
    const auto lower=arrange(windows, 4000, 1700, ring, m, gap, mru);
    for(size_t i=0;i<out.size();++i) assert(lower[i].name==out[i].name && lower[i].rect.x==out[i].rect.x && lower[i].rect.y==out[i].rect.y+2*m.rowHeight);
    std::cout<<"  arrange 200 windows: "<<ms<<" ms\n";
#if defined(__SANITIZE_ADDRESS__) || !defined(__OPTIMIZE__)
    assert(ms<50); // unoptimised and sanitizer builds
#else
    assert(ms<5);
#endif
}
static void neighbours() {
    const float p=ring.period();
    std::vector<Placed> w{{"a",{100,-300,1000,600}}, {"b",{1200,-300,1000,600}}, {"c",{p-1100,-300,1000,600}},
                         {"d",{100,400,1000,600}}, {"e",{1300,-1275,800,500}}, {"f",{4000,300,1000,600}}};
    assert(neighbour("a", Direction::Right, w, ring)=="b");
    assert(neighbour("a", Direction::Left, w, ring)=="c");   // across the seam
    assert(neighbour("c", Direction::Right, w, ring)=="a");
    assert(neighbour("a", Direction::Down, w, ring)=="d");
    assert(neighbour("b", Direction::Up, w, ring)=="e");
    assert(!neighbour("a", Direction::Up, w, ring));          // e is beyond 45 degrees of a
    assert(!neighbour("zz", Direction::Right, w, ring));
    const auto moved=nudge({10,10,5,5}, Direction::Left, 20);
    assert(moved.x==-10 && moved.y==10 && nudge(moved, Direction::Down, 20).y==30 && nudge(moved, Direction::Up, 20).y==-10);
}
// A category-like key over several classes: every group stays one contiguous run along the ring.
static void grouping() {
    std::vector<Placed> windows;
    static const char* classes[]={"foot", "firefox", "kitty", "chromium", "code", "alacritty"};
    const auto groupOf=[](const Placed& p) {
        return p.cls=="foot" || p.cls=="kitty" || p.cls=="alacritty" ? std::string("terminal") : p.cls=="code" ? std::string("dev") : std::string("web");
    };
    for(int i=0;i<18;++i) windows.push_back({"0x"+std::to_string(100+i), {float(i*3000%13000), 0, 400, 300}, 0, classes[i%6]});
    const auto out=arrange(windows, 2000, 0, ring, m, gap, {"0x105", "0x101"}, groupOf);
    assertApart(out);
    // In packing order: the gaze row, then +1, -1 ...; along each row, top-down inside a column.
    std::vector<std::tuple<int,float,float,std::string>> along;
    for(const auto& w:out) {
        const int row=rowFor(w.rect.cy(), m);
        along.push_back({row>0 ? 2*row-1 : -2*row, ring.unwrap(w.rect.x-2000+ring.period()/4), w.rect.y, groupOf(w)});
    }
    std::sort(along.begin(), along.end());
    std::vector<std::string> runs;
    for(const auto& [row, x, y, g]:along) if(runs.empty() || runs.back()!=g) runs.push_back(g);
    assert(runs==(std::vector<std::string>{"terminal", "web", "dev"}));   // alacritty (0x105) is the most recent
    // Inside a group: class, then MRU; the class overload groups by class alone.
    assert(out[0].name=="0x105" && out[2].cls=="alacritty" && out[3].cls=="foot" && out[9].cls=="chromium");
    const auto byClass=arrange(windows, 2000, 0, ring, m, gap, {"0x105", "0x101"});
    assert(byClass[0].cls=="alacritty" && byClass[3].cls=="firefox");
    // A two-level key (category, class): classes of one category stay together even when a window of
    // another category sits between them in MRU order (foot 0, firefox 1, alacritty 2).
    const std::vector<Placed> three{{"0x1", {0, 0, 400, 300}, 0, "foot"}, {"0x2", {3000, 0, 400, 300}, 0, "firefox"}, {"0x3", {6000, 0, 400, 300}, 0, "alacritty"}};
    const auto twoLevel=[](const Placed& p) { return std::string(p.cls=="firefox" ? "Web" : "Terminal")+'\t'+p.cls; };
    const auto packed=arrange(three, 2000, 0, ring, m, gap, {"0x1", "0x2", "0x3"}, twoLevel);
    assert(packed[0].name=="0x1" && packed[1].name=="0x3" && packed[2].name=="0x2");
}
static Snapshot snapshotOf(float x) { return {{{"a", Rect{x, 0, 10, 10}}}}; }
static void undoing() {
    Undo u;
    assert(!u.popUndo(snapshotOf(0)) && !u.popRedo(snapshotOf(0)));
    u.checkpoint(snapshotOf(1)); u.checkpoint(snapshotOf(2));
    auto back=u.popUndo(snapshotOf(3));
    assert(back && back->rects[0].second.x==2 && u.undo.size()==1 && u.redo.size()==1);
    auto forward=u.popRedo(snapshotOf(2));
    assert(forward && forward->rects[0].second.x==3 && u.undo.size()==2 && u.redo.empty());
    u.popUndo(snapshotOf(3)); assert(u.redo.size()==1);
    u.checkpoint(snapshotOf(9)); assert(u.redo.empty() && u.undo.size()==2);   // a new change clears redo
    for(int i=0;i<40;++i) u.checkpoint(snapshotOf(100+i));
    assert(u.undo.size()==Undo::limit && u.undo.front().rects[0].second.x==110 && u.undo.back().rects[0].second.x==139);
    for(int i=0;i<40;++i) u.popUndo(snapshotOf(200+i));
    assert(u.undo.empty() && u.redo.size()==Undo::limit);
}
static void summoning() {
    const float p=ring.period();
    const auto free=summonRect({7000, -300, 800, 600}, p-100, 0, {}, ring, gap);
    assert(free.w==800 && free.h==600 && std::abs(ring.wrap(free.cx()-(p-100)))<=10 && std::abs(free.cy())<=10 && free.x>=0 && free.x<p);
    std::vector<Placed> others{{"a", {1600, -400, 800, 800}}, {"b", {2460, -400, 800, 800}}};
    for(int i=0;i<8;++i) {
        const auto r=summonRect({0, 0, 900, 500}, 2000, 0, others, ring, gap);
        assert(freeAt(r, others, gap, p) && r.w==900 && r.h==500);
        others.push_back({"s"+std::to_string(i), r});
    }
    assert(std::abs(ring.wrap(others[2].rect.cx()-2000))<=3*(900+gap));   // the nearest free spot, not across the ring
    const auto high=summonRect({0, 0, 900, 500}, 2000, 2600, others, ring, gap);   // M8: any height
    assert(std::abs(high.cy()-2600)<=10 && std::abs(ring.wrap(high.cx()-2000))<=10);
    assert(nudge({0,0,1,1}, Direction::Right, nudgeStep).x==100 && nudgeStep==5*20);
}
int main() {
    placement(); dialogs(); arranging(); neighbours(); grouping(); undoing(); summoning();
    std::cout<<"Canvas placement: periodic no-overlap on the cylinder, determinism, the 2D spiral, dialog start, block arrange, groups, neighbours, undo and summon passed\n";
}
