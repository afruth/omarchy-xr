#include "canvas_search.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace canvas;
static windows::Record record(std::uint64_t address, const std::string& cls, const std::string& title, bool onCanvas=true, int focus=0) {
    windows::Record r; r.address=address; r.cls=cls; r.title=title; r.canvas=onCanvas; r.focusHistoryID=focus;
    return r;
}
static std::vector<std::string> names(const std::vector<Result>& results) {
    std::vector<std::string> out;
    for(const auto& r:results) out.push_back(r.name);
    return out;
}
static std::vector<gunichar> token(const std::string& text) { return tokens(text).at(0); }
static void folding() {
    const auto f=fold("Café fastFetch");
    assert(f.chars.size()==14 && f.chars[3]=='e' && f.chars[9]=='f');
    assert(f.wordStart[0] && !f.wordStart[1] && f.wordStart[5] && f.wordStart[9] && !f.wordStart[4]);
    const auto bad=fold(std::string("A\xff" "B", 3));
    assert(bad.chars.size()==3 && bad.chars[0]=='a' && bad.chars[2]=='b' && bad.wordStart[0] && !bad.wordStart[1] && bad.wordStart[2]);
    assert(tokens("  Foo \t bÄr ").size()==2 && tokens("   ").empty() && token("bÄr")==token("bar"));
    std::vector<size_t> at;
    assert(scoreToken(fold("abc"), token("abc"), &at)==212 && at==(std::vector<size_t>{0,1,2}));
    assert(scoreToken(fold("x abc"), token("abc"), &at)==186 && at==(std::vector<size_t>{2,3,4}));
    assert(scoreToken(fold("xxabc"), token("abc"))==141 && scoreToken(fold("ab"), token("abc"))==-1);
    assert(scoreToken(fold("fastfetch"), token("fas"))>0 && scoreToken(fold("free images"), token("fas"))==-1);
    assert(scoreToken(fold("Café"), token("cafe"))>0 && scoreToken(fold("é"), token("e"))==184);
    // The best start wins: the word start at 8 beats the earlier middle match.
    assert(scoreToken(fold("xabc - abc"), token("abc"), &at)==100+42-3+45 && at[0]==7);
}
static void categories() {
    assert(category("foot", "")==Category::Terminal && category("firefox", "News")==Category::Browser);
    assert(category("code", "main.cpp")==Category::Development && category("chromium", "Slack | general")==Category::Communication);
    assert(category("org.gnome.Nautilus", "")==Category::FilesNotes && category("mpv", "")==Category::Media && category("gimp", "")==Category::Other);
    assert(category("Alacritty", "slack")==Category::Terminal);   // class lists first, as in phantomat
    assert(std::string(categoryName(Category::Communication))=="chat mail" && std::string(categoryName(Category::Other))=="app");
    assert(std::string(categoryCode(Category::Terminal))=="TM" && std::string(categoryCode(Category::Other))=="AP");
}
static void ranking() {
    // Origin penalty: "term" from one terminal finds the other one first.
    const std::vector<windows::Record> terms{record(1, "foot", "~/a"), record(2, "foot", "~/b"), record(3, "firefox", "Docs")};
    const auto term=rank("term", terms, {"0x1", "0x2", "0x3"}, "0x1");
    assert(names(term)==(std::vector<std::string>{"0x2", "0x1"}) && term[0].score==124+32 && term[1].score==124+36-24);
    // AND of tokens across fields.
    const std::vector<windows::Record> notes{record(1, "foot", "notes"), record(2, "foot", "shell"), record(3, "obsidian", "notes")};
    const auto both=rank("foot notes", notes, {}, "");
    assert(names(both)==std::vector<std::string>{"0x1"} && both[0].classMatches==(std::vector<size_t>{0,1,2,3}));
    assert(both[0].titleMatches==(std::vector<size_t>{0,1,2,3,4}));
    assert(rank("notes", notes, {}, "").size()==2 && rank("NOTES FILES", notes, {}, "").size()==1);
    // Accent folding both ways, and contiguous matches only.
    const std::vector<windows::Record> text{record(1, "x", "Café Menu"), record(2, "x", "fastfetch"), record(3, "x", "free images")};
    const auto cafe=rank("cafe", text, {}, "");
    assert(names(cafe)==std::vector<std::string>{"0x1"} && cafe[0].titleMatches==(std::vector<size_t>{0,1,2,3}));
    assert(names(rank("CAFÉ", text, {}, ""))==std::vector<std::string>{"0x1"} && rank("é", text, {}, "").size()==3);
    assert(names(rank("fas", text, {}, ""))==std::vector<std::string>{"0x2"});
    // Prefix +70 beats word start +45 beats the middle, even against the MRU bonus.
    const std::vector<windows::Record> starts{record(1, "z", "abc"), record(2, "z", "x abc"), record(3, "z", "xxabc")};
    assert(names(rank("abc", starts, {"0x3", "0x2", "0x1"}, ""))==(std::vector<std::string>{"0x1", "0x2", "0x3"}));
    // Off the canvas: x0.7, listed below the canvas window despite the better MRU rank.
    const std::vector<windows::Record> off{record(1, "z", "report", false), record(2, "z", "report")};
    const auto report=rank("report", off, {"0x1", "0x2"}, "");
    assert(names(report)==(std::vector<std::string>{"0x2", "0x1"}) && report[0].canvas && !report[1].canvas);
    assert(report[0].score==254+32 && report[1].score==(254+36)*7/10);
}
static void recency() {
    const std::vector<windows::Record> desk{record(1, "a", "one", true, 2), record(2, "b", "two", true, 0), record(3, "c", "three", true, 1),
                                            record(4, "d", "four", true, 1)};
    Mru mru; mru.note("0x3", 10); mru.note("0x1", 12);
    const auto order=mru.order(desk);
    assert(order==(std::vector<std::string>{"0x1", "0x3", "0x2", "0x4"}));
    const auto all=rank("", desk, order, "0x1");
    assert(names(all)==order && all[0].score==0 && all[3].score==-3);
    // Windows missing from the frozen order follow it; gone ones are skipped.
    assert(names(rank("  ", desk, {"0x4", "0x9"}, ""))==(std::vector<std::string>{"0x4", "0x1", "0x2", "0x3"}));
    // Equal matches: the MRU bonus decides.
    const auto o=rank("o", desk, order, "");
    assert(names(o)==(std::vector<std::string>{"0x1", "0x2", "0x4"}) && o[1].score==113+28 && o[2].score==114+24);
    assert(keepSelection(o, "0x4")==2 && keepSelection(o, "0x3")==0 && keepSelection(o, "0x77")==0 && keepSelection({}, "0x1")==0);
}
static void speed() {
    static const char* classes[]={"foot", "firefox", "code", "slack", "obsidian", "mpv", "gimp", "org.gnome.Nautilus"};
    std::vector<windows::Record> many; std::vector<std::string> order;
    for(int i=0;i<512;++i) {
        many.push_back(record(0x1000+i, classes[i%8], "Window number "+std::to_string(i)+" — Ünïcødé project notes"));
        order.push_back(many.back().name());
    }
    double best=INFINITY; size_t found=0;
    for(int run=0;run<3;++run) {
        const auto start=std::chrono::steady_clock::now();
        found=rank("win proj no", many, order, order[0]).size();
        best=std::min(best, std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    }
    std::cout<<"  rank 512 windows x 3 tokens: "<<best<<" ms\n";
    assert(found==512);
#if !(defined(__SANITIZE_ADDRESS__) || !defined(__OPTIMIZE__))
    assert(best<20);   // optimised builds only; sanitizer and unoptimised timings are informational
#endif
}
int main() {
    folding(); categories(); ranking(); recency(); speed();
    std::cout<<"Canvas search: folding, contiguous scoring, categories, origin penalty, AND tokens, accents, off-canvas x0.7 and MRU passed\n";
}
