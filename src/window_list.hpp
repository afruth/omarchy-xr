#pragma once
#include "hex_token.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// The `.windows` mailbox (docs/infinite-canvas-plan.md §3.1), written by the Lua adapter:
//   v1 <owner> <seq> <stamp> [<outX> <outY> <outName>]
//   <address> <class-hex> <title-hex> <w> <h> <atX> <atY> <focus_history_id> <place> <floating> <pid> <xwayland> <canvas>
namespace windows {
enum class Place { Stage, Sliver, Park, Off };
struct Record {
    std::uint64_t address=0;
    std::string cls, title;
    unsigned w=0, h=0;
    int atX=0, atY=0, focusHistoryID=0;
    Place place=Place::Park;
    bool floating=false;
    int pid=0;
    bool xwayland=false, canvas=true;
    // The PanelLayout.output identity of a canvas window.
    std::string name() const {
        std::string hex;
        for(auto a=address; a; a>>=4) hex.insert(hex.begin(), hextoken::digits[a&15]);
        return "0x"+(hex.empty() ? std::string("0") : hex);
    }
};
// outputX/Y/Name: the canvas output's global origin and name, when the header carries them.
struct List { std::string owner; unsigned long long seq=0; long stamp=0; int outputX=0, outputY=0; std::string outputName; std::vector<Record> records; };
// The `.cursor` mailbox: v1 <owner> <seq> <x> <y> <ox> <oy> <stamp>, the compositor cursor in global
// logical px plus the cumulative overflow beyond the staged window's rectangle.
struct Cursor { std::string owner; unsigned long long seq=0; double x=0, y=0, overflowX=0, overflowY=0; long stamp=0; };
constexpr size_t maxRecords=512;

using hextoken::validHex;
using hextoken::decodeHex;
using hextoken::encodeHex;
inline std::optional<std::uint64_t> parseAddress(std::string_view token) {
    if(token.size()<3 || token.size()>18 || token.substr(0,2)!="0x") return {};
    std::uint64_t value=0; const auto end=token.data()+token.size();
    const auto [ptr, ec]=std::from_chars(token.data()+2, end, value, 16);
    if(ec!=std::errc{} || ptr!=end) return {};
    return value;
}
template<class T> bool number(std::string_view token, T& value) {
    const auto end=token.data()+token.size();
    const auto [ptr, ec]=std::from_chars(token.data(), end, value);
    return ec==std::errc{} && ptr==end && !token.empty();
}
inline std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> out;
    size_t i=0;
    while(i<line.size()) {
        while(i<line.size() && line[i]==' ') ++i;
        const size_t start=i;
        while(i<line.size() && line[i]!=' ') ++i;
        if(i>start) out.push_back(line.substr(start, i-start));
    }
    return out;
}
// OMXR-<8hex>-canvas, or a SPIKE-/OMXRTEST- test output.
inline bool canvasOutputName(std::string_view name) {
    const auto word=[](char c) { return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'; };
    if(name.size()==20 && name.starts_with("OMXR-") && name.ends_with("-canvas"))
        return std::all_of(name.begin()+5, name.begin()+13, [](char c) { return (c>='0' && c<='9') || (c>='a' && c<='f'); });
    for(std::string_view prefix:{"SPIKE-", "OMXRTEST-"}) {
        if(!name.starts_with(prefix)) continue;
        const auto rest=name.substr(prefix.size());
        return !rest.empty() && rest.size()<=40 && std::all_of(rest.begin(), rest.end(), word);
    }
    return false;
}
inline bool parseHeader(std::string_view line, List& list) {
    const auto f=fields(line);
    if((f.size()!=4 && f.size()!=7) || f[0]!="v1" || !number(f[2],list.seq) || !number(f[3],list.stamp)) return false;
    if(f.size()==7 && (!number(f[4],list.outputX) || !number(f[5],list.outputY) || !canvasOutputName(f[6]))) return false;
    if(f.size()==7) list.outputName=f[6];
    list.owner=f[1]; return true;
}
inline std::optional<Place> parsePlace(std::string_view token) {
    if(token=="stage") return Place::Stage;
    if(token=="sliver") return Place::Sliver;
    if(token=="park") return Place::Park;
    if(token=="off") return Place::Off;
    return {};
}
inline bool flag(std::string_view token, bool& value) {
    if(token!="0" && token!="1") return false;
    value=token=="1"; return true;
}
inline std::optional<Record> parseRecord(std::string_view line) {
    const auto f=fields(line);
    if(f.size()!=13 || !validHex(f[1]) || !validHex(f[2])) return {};
    Record r;
    const auto address=parseAddress(f[0]); const auto place=parsePlace(f[8]);
    if(!address || !place) return {};
    r.address=*address; r.place=*place; r.cls=decodeHex(f[1]); r.title=decodeHex(f[2]);
    if(!number(f[3],r.w) || !number(f[4],r.h) || r.w<1 || r.h<1 || r.w>16384 || r.h>16384 ||
       !number(f[5],r.atX) || !number(f[6],r.atY) || !number(f[7],r.focusHistoryID) ||
       !flag(f[9],r.floating) || !number(f[10],r.pid) || r.pid<0 || !flag(f[11],r.xwayland) || !flag(f[12],r.canvas))
        return {};
    return r;
}
// All or nothing: a torn or foreign file never replaces the last good list.
inline std::optional<List> parse(std::string_view text) {
    while(!text.empty() && (text.back()=='\n' || text.back()==' ')) text.remove_suffix(1);
    List list; std::unordered_set<std::uint64_t> seen;
    bool header=true;
    while(!text.empty() || header) {
        const size_t end=text.find('\n');
        const auto line=text.substr(0, end);
        text=end==std::string_view::npos ? std::string_view{} : text.substr(end+1);
        if(header) {
            if(!parseHeader(line, list)) return {};
            header=false; continue;
        }
        auto record=parseRecord(line);
        if(!record || list.records.size()>=maxRecords || !seen.insert(record->address).second) return {};
        list.records.push_back(std::move(*record));
    }
    return list;
}
inline bool coordinate(std::string_view token, double& value) {
    return number(token, value) && std::isfinite(value) && std::abs(value)<=1e6;
}
inline std::optional<Cursor> parseCursor(std::string_view line) {
    while(!line.empty() && (line.back()=='\n' || line.back()==' ')) line.remove_suffix(1);
    const auto f=fields(line);
    Cursor c;
    if(f.size()!=8 || f[0]!="v1" || !number(f[2],c.seq) || !coordinate(f[3],c.x) || !coordinate(f[4],c.y)
       || !coordinate(f[5],c.overflowX) || !coordinate(f[6],c.overflowY) || !number(f[7],c.stamp)) return {};
    c.owner=f[1]; return c;
}
// The `.search` mailbox (search prompt -> renderer):
//   v1 <owner> <promptSeq> <editSeq> <hex text|-> <open 0/1> <keys> <stamp>
// promptSeq echoes the `.prompt` request it answers; editSeq counts edits and forwarded keys, since the
// prompt owns the keyboard while it is open. keys is `-` for a text edit, else the forwarded keys since
// the last text edit, comma-separated, oldest first, at most 8, the last one this line's: they had the
// editSeqs up to this one, so a reader that missed lines replays the ones it has not seen.
constexpr size_t maxSearchKeys=8;
struct Search {
    std::string owner; unsigned long long promptSeq=0, editSeq=0; std::string text; bool open=false;
    std::string key;                 // the newest key, `-` for a text edit
    std::vector<std::string> keys;   // the key log, oldest first; empty for a text edit
    long stamp=0;
};
inline bool searchKey(std::string_view key) {
    static constexpr std::string_view keys[]={"-", "enter", "shift-enter", "up", "down", "tab", "shift-tab", "esc",
                                             "ctrl-a", "ctrl-z", "ctrl-shift-z", "f1"};
    if(key.size()==6 && key.starts_with("ctrl-") && key[5]>='1' && key[5]<='8') return true;
    return std::find(std::begin(keys), std::end(keys), key)!=std::end(keys);
}
// `-`, or 1..8 comma-separated keys other than `-`.
inline bool searchKeys(std::string_view field, std::vector<std::string>& keys) {
    keys.clear();
    if(field=="-") return true;
    for(size_t start=0;;) {
        const size_t end=std::min(field.find(',', start), field.size());
        const auto key=field.substr(start, end-start);
        if(key=="-" || !searchKey(key) || keys.size()==maxSearchKeys) return false;
        keys.emplace_back(key);
        if(end==field.size()) return true;
        start=end+1;
    }
}
inline std::optional<Search> parseSearch(std::string_view line) {
    while(!line.empty() && (line.back()=='\n' || line.back()==' ')) line.remove_suffix(1);
    const auto f=fields(line);
    Search s;
    if(f.size()!=8 || f[0]!="v1" || !number(f[2],s.promptSeq) || !number(f[3],s.editSeq) || !validHex(f[4]) ||
       !flag(f[5],s.open) || !searchKeys(f[6],s.keys) || !number(f[7],s.stamp)) return {};
    s.owner=f[1]; s.text=decodeHex(f[4]); s.key=s.keys.empty() ? "-" : s.keys.back();
    return s;
}
inline std::string addressToken(std::uint64_t address) { Record r; r.address=address; return r.name(); }
// `.prompt` (renderer -> prompt): v1 <pid> <seq> <open 0/1> <output|-> <stamp>
inline std::string promptLine(int pid, unsigned long long seq, bool open, const std::string& output, long stamp) {
    return "v1 "+std::to_string(pid)+' '+std::to_string(seq)+' '+(open ? '1' : '0')+' '+(output.empty() ? "-" : output)+' '+std::to_string(stamp)+'\n';
}
// `.fill` (renderer -> Lua): v1 <pid> <seq> <address> <w> <h> <stamp>, logical px for the staged window.
inline std::string fillLine(int pid, unsigned long long seq, std::uint64_t address, unsigned w, unsigned h, long stamp) {
    return "v1 "+std::to_string(pid)+' '+std::to_string(seq)+' '+addressToken(address)+' '+std::to_string(w)+' '+std::to_string(h)+' '+
        std::to_string(stamp)+'\n';
}
}
