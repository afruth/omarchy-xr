#pragma once
#include "notification_content.hpp"
#include "async_file.hpp"
#include "tracking.hpp"
#include "notification_draw.hpp"
#include <SDL_opengl.h>
#include <pango/pangocairo.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace notifications {
inline double wallMilliseconds() {
    return std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
}
struct Raster {
    Card card;
    int width=720,height=0;
    std::vector<unsigned char> pixels;
};
inline void source(cairo_t* cr,const Color& color,double opacity=1) {
    cairo_set_source_rgba(cr,color[0],color[1],color[2],color[3]*opacity);
}
inline int text(cairo_t* cr,const std::string& value,const Color& color,int x,int y,int width,int size,int lines,bool bold=false,bool right=false) {
    auto* layout=pango_cairo_create_layout(cr);
    auto* font=pango_font_description_from_string(bold?"Liberation Sans Bold":"Liberation Sans");
    pango_font_description_set_absolute_size(font,size*PANGO_SCALE);
    pango_layout_set_font_description(layout,font);pango_font_description_free(font);
    pango_layout_set_width(layout,width*PANGO_SCALE);pango_layout_set_height(layout,-lines);
    pango_layout_set_wrap(layout,PANGO_WRAP_WORD_CHAR);pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_END);
    if(right) pango_layout_set_alignment(layout,PANGO_ALIGN_RIGHT);
    pango_layout_set_text(layout,value.c_str(),int(value.size()));
    int height=0;pango_layout_get_pixel_size(layout,nullptr,&height);
    source(cr,color);cairo_move_to(cr,x,y);pango_cairo_show_layout(cr,layout);g_object_unref(layout);
    return height;
}
inline std::shared_ptr<Raster> rasterize(const Card& card) {
    auto result=std::make_shared<Raster>();result->card=card;
    // A fixed, bounded canvas keeps even oversized notifications cheap. Pango
    // shapes Unicode and fallback fonts; rasterization happens only on changes.
    auto* surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,result->width,310);
    auto* cr=cairo_create(surface);
    constexpr double radius=16;
    // Height is cropped below, so the final outline is applied after the text.
    source(cr,card.background);cairo_paint(cr);
    const auto body=plainBody(card.body);
    int y=20;
    text(cr,card.app.empty()?"Notification":card.app,card.accent,26,y,540,18,1,true);
    y+=32;
    if(!card.summary.empty()) y+=text(cr,card.summary,card.text,26,y,668,28,2,true)+8;
    if(!body.empty()) y+=text(cr,body,card.text,26,y,668,24,4)+4;
    result->height=std::min(310,y+20);
    cairo_new_path(cr);
    const double right=result->width-1,bottom=result->height-1;
    cairo_arc(cr,right-radius,1+radius,radius,-spatial::pi/2,0);
    cairo_arc(cr,right-radius,bottom-radius,radius,0,spatial::pi/2);
    cairo_arc(cr,1+radius,bottom-radius,radius,spatial::pi/2,spatial::pi);
    cairo_arc(cr,1+radius,1+radius,radius,spatial::pi,spatial::pi*1.5);
    cairo_close_path(cr);
    source(cr,card.accent,.55);cairo_set_line_width(cr,2);cairo_stroke_preserve(cr);
    cairo_rectangle(cr,0,0,result->width,result->height);cairo_set_fill_rule(cr,CAIRO_FILL_RULE_EVEN_ODD);
    cairo_set_operator(cr,CAIRO_OPERATOR_CLEAR);cairo_fill(cr);
    cairo_set_operator(cr,CAIRO_OPERATOR_OVER);
    cairo_surface_flush(surface);
    const auto* bytes=cairo_image_surface_get_data(surface);
    result->pixels.assign(bytes,bytes+result->width*result->height*4);
    cairo_destroy(cr);cairo_surface_destroy(surface);
    return result;
}

class Hud {
    struct Item {std::shared_ptr<Raster> raster;GLuint texture=0;};
    std::string directory;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping=false,trackingFresh=false;
    unsigned version=0,consumed=0;
    std::vector<std::shared_ptr<Raster>> pending;
    std::vector<Item> items;
    std::set<std::string> dismissed;
    std::string highlighted;
    std::thread worker;
    double visibleSince=0;
    space::Floater floater;
    space::Scene scene;
    static constexpr float cardWidth=1.45f,stepX=.09f,stepY=.16f,stepDepth=.08f;
    float cardHeight(size_t i) const {return cardWidth*items[i].raster->height/items[i].raster->width;}
    size_t layers() const {return std::min<size_t>(3,items.size());}
    float stackHeight() const {
        float height=0;for(size_t i=0;i<layers();++i)height=std::max(height,cardHeight(i));
        return height+(layers()?layers()-1:0)*stepY;
    }
    void run() {
        std::vector<Card> previous;
        std::vector<std::shared_ptr<Raster>> images;
        for(;;) {
            auto next=readCards(readFile(directory+"/notifications.json"),wallMilliseconds());
            if(next!=previous) {
                std::vector<std::shared_ptr<Raster>> updated;
                for(const auto& card:next) {
                    auto found=std::find_if(images.begin(),images.end(),[&](const auto& image){return image->card==card;});
                    updated.push_back(found==images.end()?rasterize(card):*found);
                }
                images=std::move(updated);
                std::lock_guard lock(mutex);pending=images;++version;previous=std::move(next);
            }
            std::unique_lock lock(mutex);
            if(wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return stopping;})) return;
        }
    }
    static GLuint upload(const Raster& image) {
        GLuint texture=0;glGenTextures(1,&texture);
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        glPushAttrib(GL_TEXTURE_BIT);glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,image.width,image.height,0,GL_BGRA,GL_UNSIGNED_BYTE,image.pixels.data());
        glPopAttrib();glPopClientAttrib();return texture;
    }
    void receive(double now) {
        std::vector<std::shared_ptr<Raster>> next;
        {
            std::unique_lock lock(mutex,std::try_to_lock);
            if(!lock || version==consumed)return;
            consumed=version;next=pending;
        }
        std::set<std::string> present;
        for(const auto& r:next)present.insert(r->card.identity());
        const bool wasEmpty=items.empty();
        // Keep cycling order through heartbeat, theme and content updates.
        std::vector<Item> updated;
        for(auto& item:items) {
            auto found=std::find_if(next.begin(),next.end(),[&](const auto& r){return r->card.identity()==item.raster->card.identity();});
            if(found==next.end() || *found!=item.raster)glDeleteTextures(1,&item.texture);
            if(found!=next.end()) {
                updated.push_back({*found,*found==item.raster?item.texture:0});next.erase(found);
            }
        }
        for(const auto& raster:next)if(!dismissed.contains(raster->card.identity()))updated.push_back({raster,0});
        items=std::move(updated);
        if(wasEmpty && !items.empty()){visibleSince=now;floater.reset();}
        if(items.empty())highlighted.clear();
        // Forget tombstones only once the service has acknowledged removal.
        std::erase_if(dismissed,[&](const auto& id){return !present.contains(id);});
    }
    void textures() {
        for(size_t i=0;i<layers();++i)if(!items[i].texture)items[i].texture=upload(*items[i].raster);
    }
    void target() {
        highlighted.clear();
        if(!visible() || !trackingFresh || !floater.safe || floater.opacity<.5f)return;
        float nearest=1e9f;
        for(size_t i=0;i<layers();++i) {
            const auto center=position(i);
            const float hit=space::gazeHit(scene,center,cardWidth,cardHeight(i));
            if(hit>0 && hit<nearest && space::clear(scene,center,cardWidth,cardHeight(i),.02f)) {
                nearest=hit;highlighted=items[i].raster->card.identity();
            }
        }
    }
public:
    explicit Hud(std::string runtime):directory(std::move(runtime)),worker([this]{run();}) {}
    ~Hud() {
        {std::lock_guard lock(mutex);stopping=true;}
        wake.notify_one();worker.join();
    }
    void release() {for(auto& item:items){glDeleteTextures(1,&item.texture);item.texture=0;}highlighted.clear();}
    bool visible() const {return !items.empty() && items.front().texture;}
    int count() const {return int(items.size());}
    const std::string& highlight() const {return highlighted;}
    std::string front() const {return items.empty()?std::string{}:items.front().raster->card.identity();}
    bool interacting(double) const {return !highlighted.empty();}
    void update(const tracking::Camera& camera,double now) {
        trackingFresh=camera.fresh(now);receive(now);textures();
        if(!trackingFresh)highlighted.clear();
    }
    space::Vec position(size_t i) const {
        const auto b=space::facing(floater.position,scene.eye);
        const float offset=float(i)-float(layers()-1)/2;
        // Align top edges so each rear card exposes its own header, even with unequal body lengths.
        return local(floater.position,b,offset*stepX,stackHeight()/2-cardHeight(i)/2-float(layers()-1-i)*stepY,float(i)*stepDepth);
    }
    bool flick(const std::string& identity,bool up) {
        if(identity.empty() || identity!=highlighted)return false;
        auto found=std::find_if(items.begin(),items.end(),[&](const auto& item){return item.raster->card.identity()==identity;});
        if(found==items.end())return false;
        if(up) {
            AsyncFile::instance().write(directory+"/notification-dismiss.json",dismissal(found->raster->card,wallMilliseconds()));
            dismissed.insert(identity);glDeleteTextures(1,&found->texture);items.erase(found);
        } else if(items.size()>1) {
            auto next=std::next(found);if(next==items.end())next=items.begin();
            std::rotate(items.begin(),next,items.end());
        }
        textures();target();return true;
    }
    void place(space::Scene next,double now) {
        scene=std::move(next);
        target(); // Holding any exposed card also holds the whole stack for reading.
        if(visible())floater.update(scene,cardWidth+float(layers()-1)*stepX,stackHeight(),now,!highlighted.empty());
        target();
    }
    const space::Floater& placement()const{return floater;}
    void draw(double now) const {
        if(!visible()) return;
        const float alpha=float(std::clamp((now-visibleSince)/.3,0.,1.));
        glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT|GL_LINE_BIT);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        for(size_t j=layers();j>0;--j) {
            const size_t i=j-1;const auto& item=items[i];
            if(floater.safe && floater.opacity>0)drawCard(item.texture,item.raster->card,scene,position(i),cardWidth,cardHeight(i),alpha*floater.opacity,highlighted==item.raster->card.identity());
        }
        if(floater.cueOpacity>0)drawCue(items.front().raster->card,scene,floater.position,alpha*floater.cueOpacity,now);
        glPopAttrib();
    }
};
}
