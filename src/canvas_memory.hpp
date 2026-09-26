#pragma once
#include "async_file.hpp"
#include "canvas_model.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// <state>/canvas-memory.tsv: where windows were, so they come back there. Format and claim()
// rules from phantomat's Memory.cpp (BSD-3-Clause, see THIRD_PARTY_NOTICES.md).
//   window\t<class>\t<title>\tx\ty\tw\th\tseen      camera\tx\ty\tzoom\tscroll
// The scroll field is M8's; a four-field camera row (before M8) loads with scroll = y.
namespace canvas {
inline std::string escapeField(const std::string& text) {
    std::string out;
    for(char c:text) {
        if(c=='\\') out+="\\\\"; else if(c=='\t') out+="\\t"; else if(c=='\n') out+="\\n"; else out+=c;
    }
    return out;
}
inline std::string unescapeField(const std::string& text) {
    std::string out;
    for(size_t i=0;i<text.size();++i) {
        if(text[i]!='\\' || i+1==text.size()) { out+=text[i]; continue; }
        const char c=text[++i];
        out+=c=='t' ? '\t' : c=='n' ? '\n' : c;
    }
    return out;
}
inline std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out; size_t start=0;
    for(size_t tab; (tab=line.find('\t',start))!=std::string::npos; start=tab+1) out.push_back(line.substr(start,tab-start));
    out.push_back(line.substr(start));
    return out;
}
struct Memory {
    struct Entry { std::string cls, title; Rect rect; double seen=0; bool claimed=false; };
    static constexpr size_t maxEntries=256;
    static constexpr double restoreWindow=45;
    std::vector<Entry> entries;
    std::optional<Fit> camera;
    bool dirty=false;
    double dirtySince=0;

    static bool finite(std::initializer_list<double> values) {
        return std::all_of(values.begin(), values.end(), [](double v) { return std::isfinite(v); });
    }
    // Malformed rows are skipped: a damaged file must never stop the canvas.
    void read(std::istream& in) {
        entries.clear(); camera.reset();
        std::string line;
        while(std::getline(in,line)) {
            const auto f=splitTabs(line);
            try {
                if(f.size()==8 && f[0]=="window") {
                    Entry e{unescapeField(f[1]), unescapeField(f[2]), {std::stof(f[3]), std::stof(f[4]), std::stof(f[5]), std::stof(f[6])}, std::stod(f[7])};
                    if(finite({e.rect.x, e.rect.y, e.rect.w, e.rect.h, e.seen}) && e.rect.w>=1 && e.rect.h>=1) entries.push_back(std::move(e));
                } else if((f.size()==4 || f.size()==5) && f[0]=="camera") {
                    Fit c{std::stof(f[1]), std::stof(f[2]), std::stof(f[3])};
                    c.scrollY=f.size()==5 ? std::stof(f[4]) : c.focusY;
                    if(finite({c.focusX, c.focusY, c.zoom, c.scrollY}) && c.zoom>0 && c.zoom<=1) camera=c;
                }
            } catch(const std::exception&) {}
        }
        trim();
    }
    void load(const std::string& path) { std::ifstream file(path); read(file); dirty=false; }
    std::string serialize() const {
        std::ostringstream out; out<<std::setprecision(9);
        for(const auto& e:entries)
            out<<"window\t"<<escapeField(e.cls)<<'\t'<<escapeField(e.title)<<'\t'<<e.rect.x<<'\t'<<e.rect.y<<'\t'<<e.rect.w<<'\t'<<e.rect.h
               <<'\t'<<std::setprecision(17)<<e.seen<<std::setprecision(9)<<'\n';
        if(camera) out<<"camera\t"<<camera->focusX<<'\t'<<camera->focusY<<'\t'<<camera->zoom<<'\t'<<camera->scrollY<<'\n';
        return out.str();
    }
    void save(const std::string& path) { AsyncFile::instance().write(path, serialize()); dirty=false; }
    void touch(double now) { if(!dirty) { dirty=true; dirtySince=now; } }
    // Exact class+title first; else a class-only match seen within the restore window. Once per entry.
    std::optional<Rect> claim(const std::string& cls, const std::string& title, double now) {
        Entry* best=nullptr;
        for(auto& e:entries) if(!e.claimed && e.cls==cls && e.title==title && (!best || e.seen>best->seen)) best=&e;
        if(!best) for(auto& e:entries)
            if(!e.claimed && e.cls==cls && now-e.seen<=restoreWindow && (!best || e.seen>best->seen)) best=&e;
        if(!best) return {};
        best->claimed=true;
        return best->rect;
    }
    void note(const std::string& cls, const std::string& title, const Rect& rect, double now) {
        const auto it=std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.cls==cls && e.title==title; });
        if(it!=entries.end()) { it->rect=rect; it->seen=now; }
        else entries.push_back({cls, title, rect, now, true});
        trim(); touch(now);
    }
    void noteCamera(const Fit& fit, double now) { camera=fit; touch(now); }
    void forget(const std::string& cls, const std::string& title, double now) {
        const auto before=entries.size();
        std::erase_if(entries, [&](const Entry& e) { return e.cls==cls && e.title==title; });
        if(entries.size()!=before) touch(now);
    }
    void trim() {
        if(entries.size()<=maxEntries) return;
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.seen>b.seen; });
        entries.resize(maxEntries);
    }
    // Debounced: at most one write per second while windows move.
    void flush(const std::string& path, double now) { if(dirty && now-dirtySince>=1) save(path); }
};
}
