#pragma once
#include "canvas_model.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Placement on the periodic canvas (docs/infinite-canvas-plan.md §5.1). Ring search, shelf
// arrange and the 45° neighbour rule adapted from phantomat (BSD-3-Clause, see THIRD_PARTY_NOTICES.md).
namespace canvas {
enum class Direction { Left, Right, Up, Down };
struct Placed { std::string name; Rect rect; int pid=0; std::string cls={}; };

// Closer than gap counts as overlap; x is periodic when period > 0.
inline bool overlaps(const Rect& a, const Rect& b, float gap, float period) {
    if(!(a.y<b.y+b.h+gap && b.y<a.y+a.h+gap)) return false;
    float dx=b.x-a.x;
    if(period>0) { dx=std::fmod(dx,period); if(dx<0) dx+=period; }
    for(float shift:{0.f, -period}) {
        const float x=dx+shift;
        if(x<a.w+gap && -x<b.w+gap) return true;
        if(period<=0) break;
    }
    return false;
}
inline Rect snap(Rect r, float grid=20) {
    r.x=std::round(r.x/grid)*grid; r.y=std::round(r.y/grid)*grid;
    return r;
}
inline bool freeAt(const Rect& r, const std::vector<Placed>& taken, float gap, float period) {
    return std::none_of(taken.begin(), taken.end(), [&](const Placed& p) { return overlaps(r, p.rect, gap, period); });
}
// Rows -1..1 around eye height; a window taller than the band only needs its centre in row 0.
inline bool inBand(const Rect& r, const Metrics& m) {
    const float band=1.5f*m.rowHeight;
    if(r.h>2*band) return std::abs(r.cy())<=m.rowHeight/2;
    return r.y>=-band && r.y+r.h<=band;
}
// Start (camera focus, or the parent centre for a dialog), then rings 1..16 in 8 directions.
inline std::optional<Rect> placeNew(float w, float h, float startX, float startY, const std::vector<Placed>& taken,
                                    const Ring& ring, const Metrics& m, float gap, std::optional<Rect> parent={}) {
    static constexpr int directions[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
    const float period=ring.period();
    w=std::min(w, period/2);
    if(parent) { startX=parent->cx(); startY=parent->cy(); }
    const Rect origin{startX-w/2, startY-h/2, w, h};
    const auto attempt=[&](Rect r) -> std::optional<Rect> {
        r=snap(r); r.x=ring.unwrap(r.x);
        if(!inBand(r,m) || !freeAt(r,taken,gap,period)) return {};
        return r;
    };
    if(auto r=attempt(origin)) return r;
    for(int step=1;step<=16;++step)
        for(const auto& d:directions)
            if(auto r=attempt({origin.x+d[0]*step*(w+gap), origin.y+d[1]*step*(h+gap), w, h})) return r;
    return {};
}
struct Shelf { float cursor=0, columnX=0, columnY=0, columnW=0; bool open=false; };
// Whole pixels plus slack, so touching neighbours never read as overlapping after rounding.
inline float after(float edge) { return std::ceil(edge+.5f); }
// How far a must move right to clear the image of b it overlaps.
inline float clearRight(const Rect& a, const Rect& b, float gap, float period) {
    float best=0;
    for(float shift:{-period, 0.f, period}) {
        Rect image=b; image.x+=shift;
        if(overlaps(a, image, gap, 0)) best=std::max(best, image.x+image.w+gap-a.x);
    }
    return std::max(best, 1.f);
}
// Columns stacked top-down inside one row, advancing past anything already placed.
inline bool shelve(Placed& w, Shelf& shelf, int row, float headingX, const std::vector<Placed>& placed,
                   const Ring& ring, const Metrics& m, float gap) {
    const float period=ring.period(), top=row*m.rowHeight-m.rowHeight/2, tall=w.rect.h>m.rowHeight-gap;
    for(;;) {
        if(!shelf.open || tall || shelf.columnY+w.rect.h>top+m.rowHeight-gap/2) {
            if(shelf.open && shelf.columnW>0) shelf.cursor=after(shelf.columnX+shelf.columnW+gap);
            shelf.columnX=shelf.cursor; shelf.columnY=top+gap/2; shelf.columnW=0; shelf.open=true;
        }
        if(shelf.columnX+w.rect.w>headingX+period-gap) return false;
        Rect r{shelf.columnX, tall ? std::round(row*m.rowHeight-w.rect.h/2) : shelf.columnY, w.rect.w, w.rect.h};
        if(!inBand(r,m)) return false;
        const auto hit=std::find_if(placed.begin(), placed.end(), [&](const Placed& p) { return overlaps(r, p.rect, gap, period); });
        if(hit==placed.end()) {
            w.rect=r; shelf.columnY=after(r.y+r.h+gap); shelf.columnW=std::max(shelf.columnW, r.w);
            if(tall) { shelf.cursor=after(r.x+r.w+gap); shelf.open=false; }
            return true;
        }
        shelf.cursor=std::max(shelf.cursor, after(r.x+clearRight(r, hit->rect, gap, period))); shelf.open=false;
    }
}
// Groups (by default the class) ordered by their most recent member, then class, MRU and name inside a
// group. A key "outer\tinner" (category, class) groups twice: outer groups by their most recent member
// first, so a category stays together, then its inner groups the same way. Shelf-packed from the heading
// along row 0, then +1, then -1. Sizes are kept. A canvas too full for three rows keeps the rest stacked
// at the heading (M4 overflow policy; extra rows come later).
inline std::vector<Placed> arrange(std::vector<Placed> windows, float headingX, const Ring& ring, const Metrics& m,
                                   float gap, const std::vector<std::string>& mruOrder,
                                   const std::function<std::string(const Placed&)>& groupOf) {
    std::map<std::string,size_t> rank, groupRank, outerRank;
    std::map<std::string,std::string> group, outer;
    for(size_t i=0;i<mruOrder.size();++i) rank.emplace(mruOrder[i], i);
    const auto rankOf=[&](const Placed& p) { const auto it=rank.find(p.name); return it==rank.end() ? mruOrder.size() : it->second; };
    const auto note=[&](std::map<std::string,size_t>& ranks, const std::string& key, size_t r) {
        auto [it, fresh]=ranks.emplace(key, r); if(!fresh) it->second=std::min(it->second, r);
    };
    for(const auto& w:windows) {
        const auto& key=group[w.name]=groupOf(w);
        note(groupRank, key, rankOf(w)); note(outerRank, outer[w.name]=key.substr(0, key.find('\t')), rankOf(w));
    }
    std::stable_sort(windows.begin(), windows.end(), [&](const Placed& a, const Placed& b) {
        const auto &ka=group[a.name], &kb=group[b.name], &oa=outer[a.name], &ob=outer[b.name];
        if(outerRank[oa]!=outerRank[ob]) return outerRank[oa]<outerRank[ob];
        if(oa!=ob) return oa<ob;
        if(groupRank[ka]!=groupRank[kb]) return groupRank[ka]<groupRank[kb];
        if(ka!=kb) return ka<kb;
        if(a.cls!=b.cls) return a.cls<b.cls;
        if(rankOf(a)!=rankOf(b)) return rankOf(a)<rankOf(b);
        return a.name<b.name;
    });
    std::vector<Placed> placed, rest;
    Shelf shelves[3]; for(auto& s:shelves) s.cursor=std::round(headingX);
    static constexpr int rows[3]={0,1,-1};
    for(auto w:windows) {
        w.rect.w=std::min(w.rect.w, ring.period()/2);
        bool done=false;
        for(int i=0;i<3 && !done;++i) done=shelve(w, shelves[i], rows[i], headingX, placed, ring, m, gap);
        if(done) { w.rect.x=ring.unwrap(w.rect.x); placed.push_back(w); }
        else rest.push_back(w);
    }
    for(auto& w:rest) {
        if(auto r=placeNew(w.rect.w, w.rect.h, headingX, 0, placed, ring, m, gap)) w.rect=*r;
        placed.push_back(w);
    }
    return placed;
}
inline std::vector<Placed> arrange(std::vector<Placed> windows, float headingX, const Ring& ring, const Metrics& m,
                                   float gap, const std::vector<std::string>& mruOrder) {
    return arrange(std::move(windows), headingX, ring, m, gap, mruOrder, [](const Placed& p) { return p.cls; });
}
// The 45° rule: candidates within 45° of the direction, scored 13·gap² + offset² (gap along, offset across).
inline std::optional<std::string> neighbour(const std::string& from, Direction dir, const std::vector<Placed>& windows, const Ring& ring) {
    const auto origin=std::find_if(windows.begin(), windows.end(), [&](const Placed& p) { return p.name==from; });
    if(origin==windows.end()) return {};
    const Rect& a=origin->rect;
    std::optional<std::string> best; float bestScore=INFINITY;
    for(const auto& p:windows) {
        if(p.name==from) continue;
        const Rect& b=p.rect;
        const float dx=ring.wrap(b.cx()-a.cx()), dy=b.cy()-a.cy();
        const bool horizontal=dir==Direction::Left || dir==Direction::Right;
        const float along=(dir==Direction::Right ? dx : dir==Direction::Left ? -dx : dir==Direction::Down ? dy : -dy);
        const float offset=std::abs(horizontal ? dy : dx);
        if(along<=0 || offset>along) continue;
        const float gap=std::max(0.f, along-(horizontal ? (a.w+b.w) : (a.h+b.h))/2);
        const float score=13*gap*gap+offset*offset;
        if(score<bestScore || (score==bestScore && best && p.name<*best)) { bestScore=score; best=p.name; }
    }
    return best;
}
inline Rect nudge(Rect r, Direction dir, float step) {
    if(dir==Direction::Left) r.x-=step; else if(dir==Direction::Right) r.x+=step;
    else if(dir==Direction::Up) r.y-=step; else r.y+=step;
    return r;
}
// One nudge step: 5 snap cells, about 2.7 degrees at R 2.4. A nudge may overlap (phantomat).
inline constexpr float nudgeStep=100;
// Summon: centred at the heading (and y), snapped; a taken spot resolves like a new window from there.
inline Rect summonRect(const Rect& r, float headingX, float y, const std::vector<Placed>& others, const Ring& ring,
                       const Metrics& m, float gap) {
    Rect at=snap({headingX-r.w/2, y-r.h/2, r.w, r.h}); at.x=ring.unwrap(at.x);
    if(freeAt(at, others, gap, ring.period())) return at;
    if(auto placed=placeNew(r.w, r.h, headingX, y, others, ring, m, gap)) return *placed;
    return at;
}
// Undo of canvas layout changes (phantomat checkpointCanvas/applyCanvasSnapshot): a checkpoint
// before each change clears redo; both stacks keep the latest 30.
struct Snapshot { std::vector<std::pair<std::string,Rect>> rects; };
struct Undo {
    std::vector<Snapshot> undo, redo;
    static constexpr size_t limit=30;
    static void push(std::vector<Snapshot>& stack, Snapshot s) {
        if(stack.size()>=limit) stack.erase(stack.begin());
        stack.push_back(std::move(s));
    }
    void checkpoint(Snapshot s) { push(undo, std::move(s)); redo.clear(); }
    // current: the layout being left, pushed onto the other stack.
    std::optional<Snapshot> popUndo(Snapshot current) { return swap(undo, redo, std::move(current)); }
    std::optional<Snapshot> popRedo(Snapshot current) { return swap(redo, undo, std::move(current)); }
    static std::optional<Snapshot> swap(std::vector<Snapshot>& from, std::vector<Snapshot>& to, Snapshot current) {
        if(from.empty()) return {};
        Snapshot s=std::move(from.back()); from.pop_back();
        push(to, std::move(current));
        return s;
    }
};
}
