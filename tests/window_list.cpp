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
    assert(empty && empty->records.empty());
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
}
int main() {
    sample(); hex(); rejections();
    std::cout<<"Window list: mailbox sample, hex round trip, addresses, rejections and the 512-row cap passed\n";
}
