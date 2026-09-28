#pragma once
// The in-headset key help (docs/xr-controls-plan.md §4.3, §8): rows built from the chords the controls
// adapter actually bound (`.keys`), so the help can never disagree with the keyboard. The action ids,
// groups and titles match studio/input_settings.py ACTIONS (checked by tests/test_input_settings.py).
#include <string>
#include <string_view>
#include <vector>

namespace helpkeys {
struct Title { const char* id; const char* group; const char* title; };
inline constexpr Title titles[]={
    {"recenter", "View", "recenter"}, {"grab", "View", "grab: hold to carry the view"}, {"zoom_in", "View", "zoom in"},
    {"zoom_out", "View", "zoom out"}, {"overview", "View", "overview / fit all"}, {"focus", "View", "focus the gazed window"},
    {"fill", "View", "fill with the window"}, {"previous", "Windows", "previous window"}, {"next", "Windows", "next window"},
    {"scroll_up", "Windows", "scroll up"}, {"scroll_down", "Windows", "scroll down"}, {"search", "Windows", "search windows"},
    {"nudge_left", "Arrange", "nudge left"}, {"nudge_right", "Arrange", "nudge right"}, {"nudge_up", "Arrange", "nudge up"},
    {"nudge_down", "Arrange", "nudge down"}, {"narrower", "Arrange", "narrower"}, {"wider", "Arrange", "wider"},
    {"shorter", "Arrange", "shorter"}, {"taller", "Arrange", "taller"}, {"pin", "Arrange", "pin to view"},
    {"arrange", "Arrange", "arrange"}, {"undo", "Arrange", "undo"}, {"redo", "Arrange", "redo"},
    {"notification_dismiss", "Notifications & system", "dismiss notification"},
    {"notification_next", "Notifications & system", "next notification"},
    {"pointer_home", "Notifications & system", "pointer to laptop"}, {"help", "Notifications & system", "this help"},
    {"stats", "Notifications & system", "performance stats"},
};
struct Entry { std::string action, chord; bool live=true; };
struct Keys { std::string modifier; std::vector<Entry> entries; };
struct Row { std::string section, keys, action; };

// `.keys` (Lua -> renderer): v1 <owner> <seq> <stamp>, a "modifier<TAB><modifier>" row, then
// "<action>\t<chord>\t<live 0/1>" rows. An unknown action or a malformed row is skipped; another owner's
// file gives nothing.
inline Keys parse(std::string_view text, const std::string& owner, unsigned long long* seq=nullptr) {
    Keys out;
    const auto firstEnd=text.find('\n');
    const std::string head(text.substr(0, firstEnd));
    const std::string prefix="v1 "+owner+" ";
    if(!head.starts_with(prefix)) return out;
    if(seq) *seq=std::strtoull(head.c_str()+prefix.size(), nullptr, 10);
    std::string_view rest=firstEnd==std::string_view::npos ? std::string_view{} : text.substr(firstEnd+1);
    while(!rest.empty()) {
        const auto end=rest.find('\n');
        const std::string_view line=rest.substr(0, end);
        rest=end==std::string_view::npos ? std::string_view{} : rest.substr(end+1);
        const auto a=line.find('\t'), b=a==std::string_view::npos ? a : line.find('\t', a+1);
        if(line.starts_with("modifier\t")) { if(line.size()<=100) out.modifier=std::string(line.substr(9)); continue; }
        if(b==std::string_view::npos) continue;
        Entry e{std::string(line.substr(0, a)), std::string(line.substr(a+1, b-a-1)), line.substr(b+1)=="1"};
        bool known=false;
        for(const auto& t:titles) known=known || e.action==t.id;
        if(known && !e.chord.empty() && e.chord.size()<=120) out.entries.push_back(std::move(e));
    }
    return out;
}
// "CTRL + ALT + SHIFT + Left" -> "Ctrl+Alt+Shift+←".
inline std::string pretty(const std::string& chord) {
    static const std::pair<const char*, const char*> names[]={{"space", "Space"}, {"equal", "="}, {"minus", "-"}, {"comma", ","},
        {"period", "."}, {"slash", "/"}, {"semicolon", ";"}, {"apostrophe", "'"}, {"bracketleft", "["}, {"bracketright", "]"},
        {"backslash", "\\"}, {"grave", "`"}, {"Return", "Enter"}, {"Page_Up", "PgUp"}, {"Page_Down", "PgDn"}, {"Up", "↑"},
        {"Down", "↓"}, {"Left", "←"}, {"Right", "→"}, {"BackSpace", "Backspace"}};
    std::string out, part;
    const auto flush=[&] {
        while(!part.empty() && part.back()==' ') part.pop_back();
        while(!part.empty() && part.front()==' ') part.erase(part.begin());
        if(part.empty()) return;
        std::string shown=part;
        const bool modifier=part=="CTRL" || part=="ALT" || part=="SHIFT" || part=="SUPER";
        if(modifier) { for(size_t i=1;i<shown.size();++i) shown[i]=char(shown[i]-'A'+'a'); }
        else for(const auto& [from, to]:names) if(part==from) shown=to;
        out+=(out.empty() ? "" : "+")+shown; part.clear();
    };
    for(const char c:chord) { if(c=='+') flush(); else part+=c; }
    flush();
    return out;
}
// The help table: the search field's keys (canvas), every bound layer action live in this scene, grouped
// as in Studio, and the mouse on the modifier. Without `.keys` (controls before v7) one row says so.
inline std::vector<Row> rows(const Keys& keys, bool canvas) {
    std::vector<Row> out;
    if(canvas) for(const auto& [k, a]:{std::pair{"type", "search the list"}, {"Up/Down / Tab", "move the selection"},
                                         {"Enter / Shift+Enter", "land / summon here"}, {"Ctrl+1-8", "land on that row"}, {"Esc", "clear, then go back"}})
        out.push_back({"Search field", k, a});
    for(const auto& t:titles) for(const auto& e:keys.entries)
        if(e.action==t.id && e.live) out.push_back({t.group, pretty(e.chord), t.title});
    if(keys.modifier.empty()) { out.push_back({"XR keys", "Utilities → Setup & integrations", "install XR controls v7"}); return out; }
    const auto modifier=pretty(keys.modifier);
    out.push_back({"Mouse", modifier+"+wheel", "zoom"});
    out.push_back({"Mouse", modifier+"+middle (hold)", "grab"});
    if(canvas) {
        out.push_back({"Mouse", modifier+"+Shift+wheel", "scroll"});
        out.push_back({"Mouse", modifier+"+drag / right-drag", "move / resize"});
    }
    return out;
}
}
