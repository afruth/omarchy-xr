#pragma once
#include "window_list.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <glib.h>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

// Window search (docs/infinite-canvas-plan.md §5.4): folding, contiguous token scoring, the category
// classifier and MRU-weighted ranking, adapted from phantomat Navigator.cpp (BSD-3-Clause, see
// THIRD_PARTY_NOTICES.md).
namespace canvas {
// One folded char per source codepoint, so match positions index the source text.
struct Folded { std::vector<gunichar> chars; std::vector<bool> wordStart; };
// Casefold, NFD, first non-mark: "É" folds to "e". ASCII takes the short way (same result), the
// rest is memoised per thread (glib allocates twice per call).
inline gunichar foldChar(gunichar c) {
    if(c<128) return gunichar(std::tolower(int(c)));
    thread_local std::unordered_map<gunichar,gunichar> memo;
    if(const auto it=memo.find(c); it!=memo.end()) return it->second;
    if(memo.size()>=4096) memo.clear();
    char buffer[8]; const int length=g_unichar_to_utf8(c, buffer);
    gchar* folded=g_utf8_casefold(buffer, length);
    gchar* nfd=folded ? g_utf8_normalize(folded, -1, G_NORMALIZE_NFD) : nullptr;
    gunichar result=g_unichar_tolower(c);
    for(const gchar* p=nfd; p && *p; p=g_utf8_next_char(p)) {
        const gunichar d=g_utf8_get_char(p);
        if(g_unichar_ismark(d)) continue;
        result=d; break;
    }
    g_free(nfd); g_free(folded);
    return memo[c]=result;
}
// Word starts: alnum after non-alnum, or a camelCase boundary. Invalid UTF-8 folds bytewise.
inline Folded fold(const std::string& text) {
    Folded out; bool previousAlnum=false;
    if(!g_utf8_validate(text.data(), gssize(text.size()), nullptr)) {
        for(const unsigned char c:text) {
            const bool alnum=std::isalnum(c);
            out.chars.push_back(gunichar(std::tolower(c))); out.wordStart.push_back(alnum && !previousAlnum);
            previousAlnum=alnum;
        }
        return out;
    }
    gunichar previous=0;
    for(const char* p=text.c_str(); p<text.c_str()+text.size(); p=g_utf8_next_char(p)) {
        const gunichar c=g_utf8_get_char(p);
        const bool alnum=g_unichar_isalnum(c), camel=previous && g_unichar_islower(previous) && g_unichar_isupper(c);
        out.chars.push_back(foldChar(c)); out.wordStart.push_back(alnum && (!previousAlnum || camel));
        previousAlnum=alnum; previous=c;
    }
    return out;
}
inline std::vector<std::vector<gunichar>> tokens(const std::string& query) {
    std::vector<std::vector<gunichar>> out; std::vector<gunichar> current;
    for(const auto c:fold(query).chars) {
        if(!g_unichar_isspace(c)) { current.push_back(c); continue; }
        if(!current.empty()) out.push_back(std::move(current));
        current.clear();
    }
    if(!current.empty()) out.push_back(std::move(current));
    return out;
}
// Contiguous only ("fas" finds FAStfetch, never Free imAgeS): the text start beats a word start beats
// the middle, earlier beats later. -1 when absent; positions of the best match.
inline int scoreToken(const Folded& field, const std::vector<gunichar>& token, std::vector<size_t>* positions=nullptr) {
    const size_t n=field.chars.size(), m=token.size();
    if(m==0) return 0;
    if(m>n) return -1;
    int best=-1; size_t bestStart=0;
    for(size_t start=0;start+m<=n;++start) {
        if(!std::equal(token.begin(), token.end(), field.chars.begin()+start)) continue;
        int score=100+int(m)*14-int(std::min<size_t>(start,40))/2;
        if(start==0) score+=70; else if(field.wordStart[start]) score+=45;
        if(score>best) { best=score; bestStart=start; }
    }
    if(best>=0 && positions) { positions->clear(); for(size_t i=0;i<m;++i) positions->push_back(bestStart+i); }
    return best;
}

enum class Category { Terminal, Communication, Browser, Development, FilesNotes, Media, Other };
inline bool containsAny(std::string_view value, std::initializer_list<std::string_view> needles) {
    return std::any_of(needles.begin(), needles.end(), [&](std::string_view n) { return value.find(n)!=std::string_view::npos; });
}
inline std::string lowerAscii(std::string s) {
    for(auto& c:s) c=char(std::tolower((unsigned char)c));
    return s;
}
// Class-only lists except communication, which also looks at the title (webmail and chat in a browser).
inline Category category(const std::string& cls, const std::string& title) {
    const auto c=lowerAscii(cls), all=c+" "+lowerAscii(title);
    if(containsAny(c, {"foot", "alacritty", "kitty", "ghostty", "wezterm", "konsole", "terminal"})) return Category::Terminal;
    if(containsAny(all, {"slack", "discord", "msteams", "microsoft teams", "signal", "telegram", "whatsapp", "mattermost", "element", "gmail",
                         "mail.google", "outlook", "thunderbird", "hey.com", " imbox", " inbox"})) return Category::Communication;
    if(containsAny(c, {"chromium", "chrome", "firefox", "brave", "vivaldi", "zen", "browser"})) return Category::Browser;
    if(containsAny(c, {"code", "codium", "jetbrains", "idea", "clion", "pycharm", "webstorm", "zed", "sublime", "emacs", "nvim", "dev"}))
        return Category::Development;
    if(containsAny(c, {"nautilus", "thunar", "dolphin", "nemo", "files", "obsidian", "notion", "logseq", "notes", "writer"})) return Category::FilesNotes;
    if(containsAny(c, {"spotify", "vlc", "mpv", "music", "video", "player"})) return Category::Media;
    return Category::Other;
}
inline const char* categoryName(Category c) {
    switch(c) {
        case Category::Terminal: return "terminal";
        case Category::Communication: return "chat mail";
        case Category::Browser: return "browser web";
        case Category::Development: return "code editor";
        case Category::FilesNotes: return "files notes";
        case Category::Media: return "media music video";
        default: return "app";
    }
}
// The palette chip.
inline const char* categoryCode(Category c) {
    static constexpr const char* codes[]={"TM", "CH", "WB", "DV", "FL", "MD", "AP"};
    return codes[int(c)];
}

// Landing stamps; the caller freezes order() for a search session (recency must not shift under the list).
struct Mru {
    std::map<std::string,double> landed;
    void note(const std::string& name, double now) { landed[name]=now; }
    std::vector<std::string> order(const std::vector<windows::Record>& records) const {
        std::vector<std::tuple<double,int,std::string>> keys;
        for(const auto& r:records) {
            const auto name=r.name(); const auto it=landed.find(name);
            keys.emplace_back(it==landed.end() ? -INFINITY : it->second, r.focusHistoryID, name);
        }
        // Latest landing first (never landed last), then focus history, then name.
        std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) {
            if(std::get<0>(a)!=std::get<0>(b)) return std::get<0>(a)>std::get<0>(b);
            return std::tie(std::get<1>(a), std::get<2>(a))<std::tie(std::get<1>(b), std::get<2>(b));
        });
        std::vector<std::string> out;
        for(auto& k:keys) out.push_back(std::move(std::get<2>(k)));
        return out;
    }
};
struct Result { std::string name; int score=0; bool canvas=true; std::vector<size_t> titleMatches, classMatches; };
inline void uniqueSorted(std::vector<size_t>& v) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); }
// AND of tokens: each token adds its best field score (title x1, class x0.85, category x0.55) or drops
// the window. Then the MRU bonus, the origin penalty ("term" in a terminal finds the other terminal)
// and x0.7 for windows off the canvas.
inline bool scoreRecord(const windows::Record& r, const std::vector<std::vector<gunichar>>& query, size_t position, bool origin, Result& out) {
    const auto title=fold(r.title), cls=fold(r.cls), cat=fold(categoryName(category(r.cls, r.title)));
    int total=0; std::vector<size_t> titlePos, classPos;
    for(const auto& token:query) {
        const int t=scoreToken(title, token, &titlePos), c=scoreToken(cls, token, &classPos), k=scoreToken(cat, token);
        const int best=std::max({t, c<0 ? -1 : c*85/100, k<0 ? -1 : k*55/100});
        if(best<0) return false;
        total+=best;
        if(t>=0) out.titleMatches.insert(out.titleMatches.end(), titlePos.begin(), titlePos.end());
        if(c>=0) out.classMatches.insert(out.classMatches.end(), classPos.begin(), classPos.end());
    }
    total+=std::max(0, 36-4*int(std::min<size_t>(position, 100)));
    if(origin) total-=24;
    if(!r.canvas) total=total*7/10;
    out.score=total;
    uniqueSorted(out.titleMatches); uniqueSorted(out.classMatches);
    return true;
}
// Session order first, then windows that appeared since; an empty query lists that order.
inline std::vector<Result> rank(const std::string& query, const std::vector<windows::Record>& records,
                                const std::vector<std::string>& sessionOrder, const std::string& origin) {
    std::unordered_map<std::string,size_t> byName;
    for(size_t i=0;i<records.size();++i) byName.emplace(records[i].name(), i);
    std::vector<size_t> ordered; std::vector<bool> seen(records.size());
    for(const auto& name:sessionOrder)
        if(const auto it=byName.find(name); it!=byName.end() && !seen[it->second]) { seen[it->second]=true; ordered.push_back(it->second); }
    for(size_t i=0;i<records.size();++i) if(!seen[i]) ordered.push_back(i);
    const auto parts=tokens(query);
    std::vector<Result> out; out.reserve(ordered.size());
    for(size_t position=0;position<ordered.size();++position) {
        const auto& r=records[ordered[position]];
        Result result{r.name(), -int(position), r.canvas, {}, {}};
        if(!parts.empty() && !scoreRecord(r, parts, position, result.name==origin, result)) continue;
        out.push_back(std::move(result));
    }
    std::stable_sort(out.begin(), out.end(), [](const Result& a, const Result& b) { return a.score>b.score; });
    return out;
}
// Re-ranking keeps the selected window selected.
inline size_t keepSelection(const std::vector<Result>& results, const std::string& previous) {
    for(size_t i=0;i<results.size();++i) if(results[i].name==previous) return i;
    return 0;
}
}
