#pragma once
#include "gl_texture.hpp"
#include "notification_hud.hpp"
#include <SDL_opengl.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

// Overview labels (docs/infinite-canvas-plan.md §4.5 item 2): `class - title` on a translucent dark
// pill, rasterised once per address and title with the notification font and uploaded like the HUD cards.
namespace canvas {
class Labels {
public:
    struct Item { GLuint texture=0; int width=0, height=0; unsigned long long used=0; };
    std::unordered_map<std::string, Item> atlas;
    // key is address+title, so a retitled window gets a new raster and the old one ages out.
    const Item& item(const std::string& key, const std::string& cls, const std::string& title, int px) {
        auto& found=atlas[key];
        found.used=++clock;
        if(!found.texture) rasterize(found, cls.empty() || title.empty() ? cls+title : cls+" - "+title, px);
        return found;
    }
    GLuint texture(const std::string& key, const std::string& cls, const std::string& title, int px) { return item(key, cls, title, px).texture; }
    void release() {
        for(auto& [key, item]:atlas) if(item.texture) glDeleteTextures(1, &item.texture);
        atlas.clear();
    }
    // Least recently used rasters go first.
    void trim(size_t max=512) {
        if(atlas.size()<=max) return;
        std::vector<std::pair<unsigned long long, std::string>> order;
        for(const auto& [key, item]:atlas) order.push_back({item.used, key});
        std::sort(order.begin(), order.end());
        for(size_t i=0;i<order.size()-max;++i) {
            auto it=atlas.find(order[i].second);
            if(it->second.texture) glDeleteTextures(1, &it->second.texture);
            atlas.erase(it);
        }
    }
private:
    unsigned long long clock=0;
    static int measure(const std::string& text, int size, int maxWidth) {
        auto* surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1); auto* cr=cairo_create(surface);
        auto* layout=pango_cairo_create_layout(cr);
        auto* font=pango_font_description_from_string("Liberation Sans");
        pango_font_description_set_absolute_size(font, size*PANGO_SCALE);
        pango_layout_set_font_description(layout, font); pango_font_description_free(font);
        pango_layout_set_width(layout, maxWidth*PANGO_SCALE); pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_text(layout, text.c_str(), int(text.size()));
        int width=0; pango_layout_get_pixel_size(layout, &width, nullptr);
        g_object_unref(layout); cairo_destroy(cr); cairo_surface_destroy(surface);
        return width;
    }
    // Long titles are shortened by Pango's ellipsis at 14 label heights.
    static void rasterize(Item& item, const std::string& text, int px) {
        const int size=std::max(8, px*6/10), pad=px/2;
        item.height=px; item.width=std::max(px, measure(text, size, 14*px)+2*pad);
        auto* surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32, item.width, item.height); auto* cr=cairo_create(surface);
        const double r=px/2.;
        cairo_new_path(cr);
        cairo_arc(cr, item.width-r, r, r, -spatial::pi/2, spatial::pi/2); cairo_arc(cr, r, r, r, spatial::pi/2, spatial::pi*1.5);
        cairo_close_path(cr);
        const notifications::Card theme;
        notifications::source(cr, {.05, .06, .08, 1}, .72); cairo_fill(cr);
        notifications::text(cr, text, theme.text, pad, (px-size*5/4)/2, item.width-2*pad, size, 1);
        cairo_surface_flush(surface);
        item.texture=gltex::uploadBgra(cairo_image_surface_get_data(surface), item.width, item.height);
        cairo_destroy(cr); cairo_surface_destroy(surface);
    }
};
}
