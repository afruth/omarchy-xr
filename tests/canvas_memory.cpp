#include "canvas_memory.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <unistd.h>
using namespace canvas;

static void roundTrip() {
    Memory memory;
    memory.note("foot", "tab\there\nnext \\t literal", {100.5f, -200, 1920, 1080}, 1000);
    memory.note("fire\tfox", "", {-40, 20, 800, 600}, 1001.25);
    memory.camera=Fit{1234.5f, -10, .25f, false, -2550};
    assert(memory.dirty && memory.dirtySince==1000);
    const auto text=memory.serialize();
    assert(text.find("camera\t1234.5\t-10\t0.25\t-2550\n")!=std::string::npos);
    assert(text.find("window\tfoot\ttab\\there\\nnext \\\\t literal\t100.5\t-200\t1920\t1080\t1000") != std::string::npos);
    Memory loaded; std::istringstream in(text+"window\tbad\trow\n\ngarbage\ncamera\t1\t2\t9\nwindow\tx\ty\tnan\t0\t1\t1\t0\n");
    loaded.read(in);
    assert(loaded.entries.size()==2 && loaded.camera && loaded.camera->focusX==1234.5f && loaded.camera->zoom==.25f && loaded.camera->scrollY==-2550);
    const auto& a=loaded.entries[0];
    assert(a.cls=="foot" && a.title=="tab\there\nnext \\t literal" && a.rect.x==100.5f && a.rect.w==1920 && a.seen==1000 && !a.claimed);
    assert(loaded.entries[1].cls=="fire\tfox" && loaded.entries[1].title.empty() && loaded.entries[1].seen==1001.25);
    assert(loaded.serialize()==text);
    // A camera row from before M8 (four fields) scrolls its focus to eye level; a non-finite scroll is skipped.
    std::istringstream old("camera\t10\t-850\t0.5\n"); loaded.read(old);
    assert(loaded.camera && loaded.camera->focusY==-850 && loaded.camera->scrollY==-850);
    std::istringstream bad("camera\t10\t-850\t0.5\tnan\n"); loaded.read(bad);
    assert(!loaded.camera);
}
static void files() {
    const auto dir=std::filesystem::temp_directory_path()/("omxr-canvas-memory-"+std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    const auto path=(dir/"canvas-memory.tsv").string();
    Memory memory;
    memory.load(path); assert(memory.entries.empty() && !memory.camera);
    memory.note("foot", "zsh", {0, 0, 640, 480}, 50);
    memory.flush(path, 50.5); AsyncFile::instance().flush();
    assert(memory.dirty && !std::filesystem::exists(path)); // debounced
    memory.flush(path, 51); AsyncFile::instance().flush();
    assert(!memory.dirty && std::filesystem::exists(path));
    Memory loaded; loaded.load(path);
    assert(loaded.entries.size()==1 && loaded.entries[0].rect.w==640 && !loaded.dirty);
    std::filesystem::remove_all(dir);
}
static void claiming() {
    Memory memory;
    std::istringstream in("window\tfoot\tbuild\t10\t0\t800\t600\t100\n"
                          "window\tfoot\tvim\t20\t0\t800\t600\t200\n"
                          "window\tfoot\tzsh\t30\t0\t800\t600\t150\n"
                          "window\tslack\tgeneral\t40\t0\t800\t600\t150\n");
    memory.read(in);
    assert(memory.claim("foot", "zsh", 1000)->x==30);        // exact title beats a newer class match
    assert(!memory.claim("foot", "zsh", 1000));             // once per session
    assert(!memory.claim("foot", "htop", 246));             // newest class match is 46 s old
    assert(memory.claim("foot", "htop", 245)->x==20);       // within the restore window: newest first
    assert(memory.claim("foot", "other", 145)->x==10);
    assert(!memory.claim("foot", "other", 145));
    assert(!memory.claim("code", "vim", 150));
    assert(memory.claim("slack", "general", 1e9)->x==40);   // exact matches never expire
    memory.note("foot", "zsh", {70, 0, 800, 600}, 2000);
    assert(memory.entries.size()==4 && memory.entries[2].rect.x==70 && memory.entries[2].seen==2000);
    memory.forget("foot", "zsh", 2001); memory.forget("nobody", "none", 2002);
    assert(memory.entries.size()==3 && memory.dirtySince==2000);
}
static void cap() {
    Memory memory;
    for(int i=0;i<300;++i) memory.note("app", "window "+std::to_string(i), {float(i), 0, 100, 100}, 1000+(i*37)%300);
    assert(memory.entries.size()==Memory::maxEntries);
    for(const auto& e:memory.entries) assert(e.seen>=1000+300-256);
    std::string text;
    for(int i=0;i<300;++i) text+="window\tapp\tw"+std::to_string(i)+"\t0\t0\t10\t10\t"+std::to_string(i)+"\n";
    std::istringstream in(text); memory.read(in);
    assert(memory.entries.size()==Memory::maxEntries && memory.entries.back().seen==44);
}
int main() {
    roundTrip(); files(); claiming(); cap();
    std::cout<<"Canvas memory: TSV round trip with escapes, debounced save, claim precedence, restore window, single claim and the 256 cap passed\n";
}
