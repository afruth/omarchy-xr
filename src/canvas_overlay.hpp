#pragma once
#include "canvas_search.hpp"
#include "gl_texture.hpp"
#include "notification_draw.hpp"
#include "notification_hud.hpp"
#include <SDL_opengl.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Body-locked canvas overlays (docs/infinite-canvas-plan.md §4.5 item 3, §5.4, §5.8): the search
// palette, the Alt-Tab switcher, the radar strip and the F1 help as Pango/Cairo rasters on quads that
// lazily follow the head inside the ring. One code path draws them in stereo, the 2D window and the
// spectator; in 2D a berth in front of the eye reads as screen space.
namespace canvas::overlay {
namespace space=notifications::space;
using notifications::Color;
constexpr float degrees=spatial::pi/180;
// Lazy follow (the notification Floater's turned/settled gates without its retreat route): the berth
// holds its place until the wanted point has been more than 12° away for 0.3 s, then eases there with
// tau 0.15 s. Positions are eye-relative and world-oriented, so the eye dolly carries the berth along
// and the distance (0.9 or 0.85 of the ring reach, never behind the ring) follows at once.
struct Berth {
    space::Vec position{}, target{};
    double awaySince=-1, lastTime=-1;
    float yawOffsetDeg=0, pitchOffsetDeg=0;
    bool placed=false;
    Berth(float yaw=0, float pitch=0) : yawOffsetDeg(yaw), pitchOffsetDeg(pitch) {}
    void reset() { placed=false; awaySince=lastTime=-1; }
    space::Vec wanted(const space::Scene& scene, float radius) const {
        const auto direction=space::normalize({std::tan(yawOffsetDeg*degrees), std::tan(pitchOffsetDeg*degrees), -1});
        return space::rotate(tracking::conjugate(scene.view), space::mul(direction, radius));
    }
    static float apartDeg(space::Vec a, space::Vec b) {
        return std::acos(std::clamp(space::dot(space::normalize(a), space::normalize(b)), -1.f, 1.f))/degrees;
    }
    void update(const space::Scene& scene, float radius, double now) {
        const auto want=wanted(scene, radius);
        if(!placed) { position=target=want; placed=true; lastTime=now; awaySince=-1; return; }
        const float dt=float(std::clamp(now-lastTime, 0., .1)); lastTime=now;
        if(apartDeg(target, want)<=12) awaySince=-1;
        else if(awaySince<0) awaySince=now;
        else if(now-awaySince>=.3) { target=want; awaySince=-1; }
        target=space::mul(space::normalize(target), radius);
        position=space::add(position, space::mul(space::sub(target, position), float(-std::expm1(-dt/.15f))));
    }
    space::Vec centre(const space::Scene& scene) const { return space::add(scene.eye, position); }
};
// A raster is rebuilt only when its content key changes, and at most every minInterval seconds.
struct Raster {
    GLuint texture=0;
    int width=0, height=0;
    std::string key;
    double builtAt=-1e9;
    void release() { if(texture) glDeleteTextures(1, &texture); texture=0; width=height=0; key.clear(); builtAt=-1e9; }
    // paint(cr) draws into a w x maxHeight surface and returns the height it used (the rest is cropped).
    template<class Paint> bool update(const std::string& next, int w, int maxHeight, double now, double minInterval, Paint&& paint) {
        if(texture && (next==key || now-builtAt<minInterval)) return false;
        auto* surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, maxHeight); auto* cr=cairo_create(surface);
        const int used=std::clamp(paint(cr), 1, maxHeight);
        cairo_surface_flush(surface);
        if(texture) glDeleteTextures(1, &texture);
        texture=gltex::uploadBgra(cairo_image_surface_get_data(surface), w, used);
        width=w; height=used; key=next; builtAt=now;
        cairo_destroy(cr); cairo_surface_destroy(surface);
        return true;
    }
};
struct Style {
    Color panel{.07, .08, .11, 1}, text{.9, .92, .98, 1}, dim{.58, .62, .70, 1}, accent{.35, .65, 1, 1};
};
inline std::string colorKey(const Color& c) { char out[32]; std::snprintf(out, sizeof(out), "%02x%02x%02x", int(c[0]*255), int(c[1]*255), int(c[2]*255)); return out; }
inline void roundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x+w-r, y+r, r, -spatial::pi/2, 0); cairo_arc(cr, x+w-r, y+h-r, r, 0, spatial::pi/2);
    cairo_arc(cr, x+r, y+h-r, r, spatial::pi/2, spatial::pi); cairo_arc(cr, x+r, y+r, r, spatial::pi, 1.5*spatial::pi);
    cairo_close_path(cr);
}
// The translucent card every overlay sits on, with a faint accent rim.
inline void card(cairo_t* cr, int w, int h, const Style& s) {
    roundedRect(cr, 1, 1, w-2, h-2, 18);
    notifications::source(cr, s.panel, .92); cairo_fill_preserve(cr);
    notifications::source(cr, s.accent, .5); cairo_set_line_width(cr, 2); cairo_stroke(cr);
}
// A two-letter category chip (categoryCode).
inline void chip(cairo_t* cr, const std::string& code, int x, int y, const Style& s) {
    roundedRect(cr, x, y, 40, 24, 6);
    notifications::source(cr, s.accent, .28); cairo_fill(cr);
    notifications::text(cr, code, s.text, x, y+3, 40, 14, 1, true);
}
// Title markup: the matched codepoints (canvas_search positions) bold in the accent colour.
inline std::string highlighted(const std::string& title, const std::vector<size_t>& matches, const Color& accent) {
    const auto escape=[](const char* from, size_t size) { gchar* e=g_markup_escape_text(from, gssize(size)); std::string out(e); g_free(e); return out; };
    if(matches.empty() || !g_utf8_validate(title.data(), gssize(title.size()), nullptr)) return escape(title.data(), title.size());
    std::string out; size_t index=0;
    for(const char* p=title.c_str(); p<title.c_str()+title.size(); ++index) {
        const char* next=g_utf8_next_char(p);
        const auto piece=escape(p, size_t(next-p));
        if(std::binary_search(matches.begin(), matches.end(), index)) out+="<span weight=\"bold\" foreground=\"#"+colorKey(accent)+"\">"+piece+"</span>";
        else out+=piece;
        p=next;
    }
    return out;
}
inline void markupText(cairo_t* cr, const std::string& markup, const Color& color, int x, int y, int width, int size) {
    auto* layout=pango_cairo_create_layout(cr);
    auto* font=pango_font_description_from_string("Liberation Sans");
    pango_font_description_set_absolute_size(font, size*PANGO_SCALE);
    pango_layout_set_font_description(layout, font); pango_font_description_free(font);
    pango_layout_set_width(layout, width*PANGO_SCALE); pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_markup(layout, markup.c_str(), int(markup.size()));
    notifications::source(cr, color); cairo_move_to(cr, x, y); pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}
// One palette or switcher row: chip, class, title (with matches); off-canvas rows say how landing works.
struct Row { std::string code, cls, title; std::vector<size_t> matches; bool canvas=true; };
struct List { std::string query; std::vector<Row> rows; size_t selected=0, matches=0, total=0; };
inline std::string listKey(const List& l, const Style& s) {
    std::string key=l.query+'\n'+std::to_string(l.selected)+' '+std::to_string(l.matches)+' '+std::to_string(l.total)+' '+colorKey(s.accent);
    for(const auto& r:l.rows) {
        key+='\n'+r.code+'\t'+r.cls+'\t'+r.title+'\t'+(r.canvas ? '1' : '0');
        for(auto m:r.matches) key+=' '+std::to_string(m);
    }
    return key;
}
inline void rows(cairo_t* cr, const List& l, int width, int top, int height, const Style& s) {
    for(size_t i=0;i<l.rows.size();++i) {
        const auto& r=l.rows[i]; const int y=top+int(i)*height;
        if(i==l.selected) { roundedRect(cr, 10, y+2, width-20, height-4, 10); notifications::source(cr, s.accent, .24); cairo_fill(cr); }
        chip(cr, r.code, 18, y+(height-24)/2, s);
        notifications::text(cr, r.cls, s.dim, 70, y+(height-20)/2, 140, 17, 1);
        const int titleWidth=width-222-(r.canvas ? 18 : 150);
        markupText(cr, highlighted(r.title, r.matches, s.accent), s.text, 218, y+(height-24)/2, titleWidth, 19);
        if(!r.canvas) notifications::text(cr, "bring to canvas", s.accent, width-160, y+(height-18)/2, 146, 15, 1, false, true);
    }
}
constexpr int paletteWidth=720, paletteMaxHeight=480, rowHeight=46;
// Query line with caret and "N of M", then up to 8 rows.
inline int paintPalette(cairo_t* cr, const List& l, const Style& s) {
    const int height=64+int(l.rows.size())*rowHeight+12;
    card(cr, paletteWidth, height, s);
    const bool empty=l.query.empty();
    notifications::text(cr, empty ? "Type to search windows" : l.query, empty ? s.dim : s.text, empty ? 32 : 24, 18, 520, 24, 1, !empty);
    int caret=24;
    if(!empty) {
        auto* layout=pango_cairo_create_layout(cr); auto* font=pango_font_description_from_string("Liberation Sans Bold");
        pango_font_description_set_absolute_size(font, 24*PANGO_SCALE); pango_layout_set_font_description(layout, font); pango_font_description_free(font);
        pango_layout_set_text(layout, l.query.c_str(), int(l.query.size()));
        int w=0; pango_layout_get_pixel_size(layout, &w, nullptr); g_object_unref(layout);
        caret+=std::min(w, 520)+2;
    }
    notifications::source(cr, s.accent); cairo_rectangle(cr, caret, 18, 2, 30); cairo_fill(cr);
    notifications::text(cr, std::to_string(l.matches)+" of "+std::to_string(l.total), s.dim, paletteWidth-184, 22, 160, 18, 1, false, true);
    notifications::source(cr, s.dim, .35); cairo_rectangle(cr, 16, 62, paletteWidth-32, 1); cairo_fill(cr);
    rows(cr, l, paletteWidth, 66, rowHeight, s);
    return height;
}
constexpr int switcherWidth=600, switcherMaxHeight=480;
inline int paintSwitcher(cairo_t* cr, const List& l, const Style& s) {
    const int height=54+int(l.rows.size())*rowHeight+12;
    card(cr, switcherWidth, height, s);
    notifications::text(cr, "Switch windows", s.text, 24, 16, 300, 20, 1, true);
    notifications::text(cr, "release to land", s.dim, switcherWidth-224, 19, 200, 16, 1, false, true);
    rows(cr, l, switcherWidth, 54, rowHeight, s);
    return height;
}
// Radar (§5.8): the full 360° around the heading, 2 px per degree, one mark per live window on its
// row's lane (upper, middle, lower), the staged window in the accent, the view span as a lighter band.
struct Mark { float offsetDeg=0, widthDeg=0; int row=0; bool staged=false; };
struct Radar { std::vector<Mark> marks; float viewDeg=0; };
constexpr int radarWidth=720, radarHeight=72;
inline std::string radarKey(const Radar& r, const Style& s) {
    std::string key=std::to_string(std::lround(r.viewDeg*2))+' '+colorKey(s.accent);
    for(const auto& m:r.marks) key+=' '+std::to_string(std::lround(m.offsetDeg*2))+':'+std::to_string(std::lround(m.widthDeg*2))+':'+std::to_string(m.row)+(m.staged ? "s" : "");
    return key;
}
inline int paintRadar(cairo_t* cr, const Radar& r, const Style& s) {
    card(cr, radarWidth, radarHeight, s);
    const double mid=radarWidth/2., perDeg=(radarWidth-24)/360.;
    const double band=std::min<double>(r.viewDeg, 360)*perDeg;
    roundedRect(cr, mid-band/2, 8, band, radarHeight-16, 6); notifications::source(cr, s.text, .12); cairo_fill(cr);
    cairo_save(cr); roundedRect(cr, 12, 4, radarWidth-24, radarHeight-8, 12); cairo_clip(cr);
    for(const auto& m:r.marks) {
        const double w=std::max(3., m.widthDeg*perDeg), y=radarHeight/2.+std::clamp(m.row, -1, 1)*18-6;
        notifications::source(cr, m.staged ? s.accent : s.dim, m.staged ? 1 : .8);
        for(const double wrap:{-360., 0., 360.}) { roundedRect(cr, mid+(m.offsetDeg+wrap)*perDeg-w/2, y, w, 12, 3); cairo_fill(cr); }
    }
    cairo_restore(cr);
    notifications::source(cr, s.text, .9); cairo_rectangle(cr, mid-1, 4, 2, radarHeight-8); cairo_fill(cr);
    return radarHeight;
}
// F1 help: the canvas chords in one table, so the user guide and this card stay in sync. Takeover rows
// (canvas.tsv takeoverKeys) show only while the takeover switch is on; off, those chords stay Omarchy's.
struct HelpRow { const char* section; const char* keys; const char* action; bool takeover=false; };
inline constexpr HelpRow helpRows[]={
    {"Find", "type", "search windows"}, {"Find", "Up/Down / Tab", "move the selection"}, {"Find", "Ctrl+1-8", "land on that row"},
    {"Find", "Enter", "land on the selection"}, {"Find", "Shift+Enter", "summon it here"}, {"Find", "Esc", "clear, then go back"},
    {"View", "SUPER+TAB", "overview", true}, {"View", "SUPER+CTRL+G", "search"}, {"View", "SUPER+F", "fill"},
    {"View", "SUPER+arrows", "neighbour", true}, {"View", "flick in / out", "land / zoom out"},
    {"Arrange", "SUPER+SHIFT+arrows", "nudge", true}, {"Arrange", "Ctrl+A", "arrange"}, {"Arrange", "Ctrl+Z / Ctrl+Shift+Z", "undo / redo"},
    {"Arrange", "SUPER+ALT+P", "pin"},
    {"Anywhere", "ALT+TAB", "switcher", true}, {"Anywhere", "three-finger double tap", "release pointer"}, {"Anywhere", "F1", "this help"},
};
constexpr int helpWidth=960, helpHeight=560;
inline std::string helpKey(bool takeover, const Style& s) { return std::string("help ")+(takeover ? "1 " : "0 ")+colorKey(s.accent); }
inline int paintHelp(cairo_t* cr, const Style& s, bool takeover=true) {
    card(cr, helpWidth, helpHeight, s);
    notifications::text(cr, "Window canvas keys", s.text, 32, 22, 500, 26, 1, true);
    notifications::text(cr, "F1 or Esc closes", s.dim, helpWidth-272, 28, 240, 17, 1, false, true);
    int column=0, y=76; std::string section;
    for(const auto& row:helpRows) {
        if(row.takeover && !takeover) continue;
        if(row.section!=section) {
            section=row.section;
            // Find and View on the left, Arrange and Anywhere on the right.
            if(section=="Arrange") { column=1; y=76; }
            notifications::text(cr, section, s.accent, 32+column*464, y+6, 400, 19, 1, true); y+=38;
        }
        const int x=32+column*464;
        notifications::text(cr, row.keys, s.text, x, y, 200, 17, 1, true);
        notifications::text(cr, row.action, s.dim, x+210, y, 220, 17, 1);
        y+=32;
    }
    if(!takeover) notifications::text(cr, "SUPER+TAB, SUPER+arrows and ALT+TAB keep Omarchy's keys (takeover off in Studio)", s.dim, 32, helpHeight-44, helpWidth-64, 15, 1);
    return helpHeight;
}
// A textured quad facing the eye, depth test off, premultiplied blend (the label state block). An
// opaque one (a pinned window's frame) takes its alpha from the fade only: capture alpha is undefined.
inline void drawQuad(GLuint texture, const space::Scene& scene, space::Vec centre, float width, float height, float alpha, bool opaque=false) {
    if(!texture || alpha<=0) return;
    const auto b=space::facing(centre, scene.eye);
    glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    if(opaque) {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE); glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PRIMARY_COLOR);
    } else glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(alpha, alpha, alpha, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); notifications::vertex(notifications::local(centre, b, -width/2, height/2));
    glTexCoord2f(1, 0); notifications::vertex(notifications::local(centre, b, width/2, height/2));
    glTexCoord2f(1, 1); notifications::vertex(notifications::local(centre, b, width/2, -height/2));
    glTexCoord2f(0, 1); notifications::vertex(notifications::local(centre, b, -width/2, -height/2));
    glEnd();
    glPopAttrib();
}
// Distance from the eye to the ring along the view direction (the eye dollies inside it in Work).
inline float ringReach(const space::Scene& scene, float radius) {
    const auto f=space::rotate(tracking::conjugate(scene.view), {0, 0, -1});
    const float a=f.x*f.x+f.z*f.z, b=2*(scene.eye.x*f.x+scene.eye.z*f.z), c=scene.eye.x*scene.eye.x+scene.eye.z*scene.eye.z-radius*radius;
    if(a<1e-4f || c>=0) return radius;
    return std::clamp((-b+std::sqrt(b*b-4*a*c))/(2*a), .3f, 2*radius);
}
}
