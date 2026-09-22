#include "dwell.hpp"
#include "theme.hpp"
#include <cassert>
#include <iostream>

int main() {
    gaze::Dwell d;
    auto hit=[](const char* out,float x,float y){ targeting::Hit h; h.output=out; h.pixelX=x; h.pixelY=y; return std::optional<targeting::Hit>(h); };
    // Resting on one spot with a settled head fires once after the dwell time, not before.
    double t=0;
    for (int i=0; i<60; ++i) { assert(!d.update(hit("A",100,100), 3, t)); t+=1/60.0; }   // 1.0 s exactly: not yet
    assert(d.fraction(t)>0.95);
    const auto fired=d.update(hit("A",110,95), 3, t+0.05);
    assert(fired && fired->output=="A" && fired->pixelX==110);
    // It does not fire again while the gaze stays in the area.
    for (int i=0; i<120; ++i) { assert(!d.update(hit("A",120,110), 2, t+0.1+i/60.0)); }
    // Leaving the area re-arms: a new rest elsewhere fires after another dwell time.
    t+=3;
    assert(!d.update(hit("A",600,100), 2, t));
    for (int i=1; i<=59; ++i) assert(!d.update(hit("A",600,100), 2, t+i/60.0));
    assert(d.update(hit("A",600,100), 2, t+1.01));
    // A fast head (above settleSpeed) or a miss resets the rest, so a glance never fires.
    t+=5;
    for (int i=0; i<30; ++i) assert(!d.update(hit("B",0,0), 2, t+i/60.0));
    assert(!d.update(hit("B",0,0), 40, t+0.6));           // moving: reset
    for (int i=0; i<50; ++i) assert(!d.update(hit("B",0,0), 2, t+0.7+i/60.0));   // 0.83 s since the reset
    assert(!d.update(std::nullopt, 2, t+1.6));            // miss: reset
    for (int i=0; i<59; ++i) assert(!d.update(hit("B",0,0), 2, t+1.7+i/60.0));
    assert(d.update(hit("B",0,0), 2, t+1.7+1.0));
    // Switching monitors mid-rest starts over on the new one.
    t+=10;
    for (int i=0; i<40; ++i) assert(!d.update(hit("A",0,0), 2, t+i/60.0));
    for (int i=0; i<40; ++i) assert(!d.update(hit("B",0,0), 2, t+0.7+i/60.0));
    assert(!d.update(hit("B",0,0), 2, t+1.5) && d.update(hit("B",0,0), 2, t+1.75)->output=="B");
    // Settings line.
    const auto s=gaze::parseSettings("gaze-v1 700 8 90 0");
    assert(s && s->dwellMs==700 && s->settleSpeed==8 && s->radiusPx==90 && !s->pointer);
    for (const char* bad:{"","gaze-v2 700 8 90 0","gaze-v1 50 8 90 1","gaze-v1 700 8 90 2","gaze-v1 700 8 90 1 x","gaze-v1 nan 8 90 1"}) assert(!gaze::parseSettings(bad));
    // Theme accent parsing.
    const auto accent=theme::parseAccent("mode = \"dark\"\n\naccent = \"#82FB9C\"\nselection = \"#45475a\"\n");
    assert(accent && std::abs((*accent)[0]-0x82/255.f)<1e-6 && std::abs((*accent)[1]-0xFB/255.f)<1e-6 && std::abs((*accent)[2]-0x9C/255.f)<1e-6);
    assert(!theme::parseAccent("background = \"#000000\"\n") && !theme::parseAccent("accent = \"#12345\"\n") && !theme::parseAccent("accent = \"#zzzzzz\"\n"));
    assert(theme::parseAccent("  accent=\"#010203\"")->at(2)==3/255.f);
    std::cout << "Gaze dwell: rest, once-per-rest, re-arm, glance immunity, monitor switch, settings and theme accent passed\n";
}
