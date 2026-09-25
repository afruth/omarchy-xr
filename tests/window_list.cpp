#include "window_list.hpp"
#include <cassert>
#include <iostream>
using namespace windows;
static const std::string header="v1 omxr-1234 42 1700000000\n";
static std::string row(const std::string& address, const std::string& tail="1920 1080 0 0 0 stage 0 4242 0 1") {
    return address+' '+encodeHex("foot")+' '+encodeHex("~/src")+' '+tail+'\n';
}
static void sample() {
    const auto text=header+
        "0x55d4a1b2c3d0 666f6f74 7e2f737263 1920 1080 0 0 0 stage 0 4242 0 1\n"
        "0x55D4A1B2C3E0 6669726566f78 - 1280 720 -1300 40 3 park 1 99 1 1\n"
        "0x10 - - 800 600 5 6 7 off 0 0 0 0\n\n\n";
    assert(!parse(text)); // odd-length class hex is not the notification token
    const auto good=header+
        "0x55d4a1b2c3d0 666f6f74 7e2f737263 1920 1080 0 0 0 stage 0 4242 0 1\n"
        "0x55D4A1B2C3E0 66697265666f78 - 1280 720 -1300 40 3 park 1 99 1 1\n"
        "0x10 - - 800 600 5 6 7 off 0 0 0 0\n"
        "0xa 7a 7a 1 1 0 0 0 sliver 0 1 0 1\n\n\n";
    const auto list=parse(good);
    assert(list && list->owner=="omxr-1234" && list->seq==42 && list->stamp==1700000000 && list->records.size()==4);
    const auto& a=list->records[0];
    assert(a.address==0x55d4a1b2c3d0ULL && a.cls=="foot" && a.title=="~/src" && a.w==1920 && a.h==1080 && a.place==Place::Stage);
    assert(!a.floating && a.pid==4242 && !a.xwayland && a.canvas && a.name()=="0x55d4a1b2c3d0");
    const auto& b=list->records[1];
    assert(b.name()=="0x55d4a1b2c3e0" && b.cls=="firefox" && b.title.empty() && b.atX==-1300 && b.atY==40);
    assert(b.focusHistoryID==3 && b.place==Place::Park && b.floating && b.xwayland && b.pid==99);
    assert(list->records[2].place==Place::Off && !list->records[2].canvas && list->records[2].name()=="0x10");
    assert(list->records[3].place==Place::Sliver && list->records[3].name()=="0xa");
    Record zero; assert(zero.name()=="0x0");
    const auto empty=parse(header);
    assert(empty && empty->records.empty() && empty->outputName.empty() && empty->outputX==0);
    // The 7-field header adds the canvas output's origin and name.
    const auto placed=parse("v1 omxr-1234 43 1700000000 20000 -40 OMXR-0123abcd-canvas\n"+row("0x1"));
    assert(placed && placed->seq==43 && placed->outputX==20000 && placed->outputY==-40 && placed->outputName=="OMXR-0123abcd-canvas");
    assert(placed->records.size()==1);
    for(auto name:{"SPIKE-canvas", "OMXRTEST-a_b-9"}) assert(parse(std::string("v1 o 1 2 0 0 ")+name+"\n")->outputName==name);
}
static void hex() {
    for(std::string text:std::initializer_list<std::string>{"", "foot", "tab\there", "Ünïcødé — ✓", std::string("\0\x01\xff", 3), std::string(550, 'x')}) {
        const auto token=encodeHex(text);
        assert(validHex(token) && decodeHex(token)==text);
    }
    assert(encodeHex("")=="-" && decodeHex("-").empty());
    assert(decodeHex("ABCD").empty() && decodeHex("abc").empty() && decodeHex("zz").empty() && decodeHex(std::string(1102, 'a')).empty());
    assert(!validHex("") && !validHex("0g") && validHex(std::string(1100, 'a')) && !validHex(std::string(1102, 'a')));
    assert(parseAddress("0x0")==0 && parseAddress("0xFFffFFffFFffFFff")==~0ULL && parseAddress("0xDeadBeef")==0xdeadbeefULL);
    for(auto bad:{"", "0x", "123", "x12", "0x12g", "0x-1", "0x10000000000000000", " 0x1"}) assert(!parseAddress(bad));
}
static void rejections() {
    assert(!parse("") && !parse("\n") && !parse("v2 o 1 2\n") && !parse("v1 o 1\n") && !parse("v1 o x 2\n") && !parse("v1 o 1 2 3\n"));
    for(auto tail:{"0 1080 0 0 0 stage 0 1 0 1", "1920 16385 0 0 0 stage 0 1 0 1", "1920 1080 0 0 0 floating 0 1 0 1",
                   "1920 1080 0 0 0 stage 2 1 0 1", "1920 1080 0 0 0 stage 0 -1 0 1", "1920 1080 0 0 0 stage 0 1 0 1 extra",
                   "1920 1080 0 0 stage 0 1 0 1", "1920 1080 a 0 0 stage 0 1 0 1", "1920 1080 0 0 0 stage 0 1 yes 1"})
        assert(!parse(header+row("0x1", tail)));
    assert(parse(header+row("0x1", "16384 1 0 0 0 park 0 1 0 1")));
    assert(!parse(header+row("0x1")+row("0x2")+row("0x01"))); // duplicate address
    assert(!parse(header+row("0x1")+"\n"+row("0x2")));          // blank line mid-list
    assert(!parse(header+row("0x1")+"0x2 666f6f74 - 1 1 0 0 0 park 0 1 0 1 0\n"));
    assert(!parse(header+"0x2 ABCD - 1 1 0 0 0 park 0 1 0 1\n"));
    std::string full=header;
    for(int i=1;i<=512;++i) full+=row("0x"+std::to_string(i*16));
    const auto capped=parse(full);
    assert(capped && capped->records.size()==512);
    assert(!parse(full+row("0x99999")));
    // 5- and 6-field headers, bad origins and names outside the canvas/test namespaces.
    for(auto bad:{"v1 o 1 2 0\n", "v1 o 1 2 0 0\n", "v1 o 1 2 x 0 SPIKE-a\n", "v1 o 1 2 0 0 SPIKE-a extra\n", "v1 o 1 2 0 0 eDP-1\n",
                  "v1 o 1 2 0 0 DP-1\n", "v1 o 1 2 0 0 OMXR-0123abcd-left\n", "v1 o 1 2 0 0 OMXR-0123ABCD-canvas\n", "v1 o 1 2 0 0 OMXR-0123abc-canvas\n",
                  "v1 o 1 2 0 0 SPIKE-\n", "v1 o 1 2 0 0 SPIKE-a.b\n", "v1 o 1 2 0 0 OMXRTEST-a/b\n"})
        assert(!parse(bad));
    assert(!parse("v1 o 1 2 0 0 SPIKE-"+std::string(41, 'a')+"\n") && parse("v1 o 1 2 0 0 SPIKE-"+std::string(40, 'a')+"\n"));
}
static void cursor() {
    const auto c=parseCursor("v1 4242 7 20100.5 300 -12 40.25 1700000000\n");
    assert(c && c->owner=="4242" && c->seq==7 && c->x==20100.5 && c->y==300 && c->overflowX==-12 && c->overflowY==40.25 && c->stamp==1700000000);
    assert(parseCursor("v1 o 1 -1000000 1000000 0 0 5"));
    for(auto bad:{"", "v1 o 1 0 0 0 0", "v1 o 1 0 0 0 0 5 6", "v2 o 1 0 0 0 0 5", "v1 o x 0 0 0 0 5", "v1 o 1 nan 0 0 0 5",
                  "v1 o 1 0 inf 0 0 5", "v1 o 1 0 0 -inf 0 5", "v1 o 1 1000001 0 0 0 5", "v1 o 1 0 0 0 -1e7 5", "v1 o 1 0 0 0 0 s"})
        assert(!parseCursor(bad));
}
static void search() {
    const auto s=parseSearch("v1 omxr-9 4 17 "+encodeHex("café ✓")+" 1 shift-enter 1700000000\n");
    assert(s && s->owner=="omxr-9" && s->promptSeq==4 && s->editSeq==17 && s->text=="café ✓" && s->open && s->key=="shift-enter" && s->stamp==1700000000);
    const auto closed=parseSearch("v1 o 0 0 - 0 - 5");
    assert(closed && closed->text.empty() && !closed->open && closed->key=="-");
    for(auto key:{"enter", "up", "down", "tab", "shift-tab", "esc", "ctrl-1", "ctrl-8", "ctrl-a", "ctrl-z", "ctrl-shift-z", "f1"})
        assert(parseSearch(std::string("v1 o 1 2 - 1 ")+key+" 5")->key==key);
    for(auto bad:{"", "v1 o 1 2 - 1 - ", "v1 o 1 2 - 1 - 5 6", "v2 o 1 2 - 1 - 5", "v1 o 1 2 - 1 ctrl-9 5", "v1 o 1 2 - 1 ctrl-0 5",
                  "v1 o 1 2 - 1 space 5", "v1 o 1 2 - 1 ENTER 5", "v1 o 1 2 abc 1 - 5", "v1 o 1 2 ZZ 1 - 5", "v1 o 1 2 - 2 - 5",
                  "v1 o x 2 - 1 - 5", "v1 o 1 -2 - 1 - 5", "v1 o 1 2 - 1 - s",
                  "v1 o 1 2 - 1 down,,enter 5", "v1 o 1 2 - 1 down, 5", "v1 o 1 2 - 1 ,enter 5", "v1 o 1 2 - 1 -,enter 5",
                  "v1 o 1 9 - 1 tab,tab,tab,tab,tab,tab,tab,tab,tab 5", "v1 o 1 2 - 1 down,bogus 5"})
        assert(!parseSearch(bad));
    // The key log: the keys since the last text edit, oldest first; the newest is this line's key.
    const auto log=parseSearch("v1 o 1 3 - 1 down,down,enter 5");
    assert(log && log->keys==(std::vector<std::string>{"down", "down", "enter"}) && log->key=="enter");
    assert(parseSearch("v1 o 1 8 - 1 tab,tab,tab,tab,tab,tab,tab,tab 5")->keys.size()==maxSearchKeys);
    assert(closed->keys.empty() && s->keys==std::vector<std::string>{"shift-enter"});
    const auto prompt=promptLine(4242, 7, true, "OMXR-0123abcd-canvas", 1700000000);
    assert(prompt=="v1 4242 7 1 OMXR-0123abcd-canvas 1700000000\n" && promptLine(1, 2, false, "", 3)=="v1 1 2 0 - 3\n");
    const auto f=fields(std::string_view(prompt).substr(0, prompt.size()-1));
    assert(f.size()==6 && canvasOutputName(f[4]));
    const auto fill=fillLine(4242, 8, 0x55d4a1b2c3d0ULL, 1622, 950, 1700000001);
    assert(fill=="v1 4242 8 0x55d4a1b2c3d0 1622 950 1700000001\n");
    const auto g=fields(std::string_view(fill).substr(0, fill.size()-1));
    unsigned w=0, h=0; assert(g.size()==7 && parseAddress(g[3])==0x55d4a1b2c3d0ULL && number(g[4],w) && w==1622 && number(g[5],h) && h==950);
}
int main() {
    sample(); hex(); rejections(); cursor(); search();
    std::cout<<"Window list: mailbox sample, hex round trip, addresses, rejections, the 512-row cap, the output header, the cursor, search, prompt and fill lines passed\n";
}
