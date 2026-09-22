#pragma once
#include "notification_content.hpp"
#include "head_shake.hpp"
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
    if(card.count>1) text(cr,"+"+std::to_string(card.count-1),card.accent,610,y,84,18,1,true,true);
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
    std::string directory;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping=false;
    unsigned version=0,consumed=0;
    std::shared_ptr<Raster> pending,current;
    std::thread worker;
    GLuint texture=0;
    double visibleSince=0;
    std::string dismissed;
    HeadShake shake;
    space::Floater floater;
    space::Scene scene;
    static constexpr float cardWidth=1.45f;
    float cardHeight() const {return current?cardWidth*current->height/current->width:0;}
    void run() {
        std::optional<Card> previous;
        for(;;) {
            auto next=readCard(readFile(directory+"/notifications.json"),wallMilliseconds());
            if(next!=previous) {
                auto image=next?rasterize(*next):nullptr;
                std::lock_guard lock(mutex);pending=std::move(image);++version;
                previous=std::move(next);
            }
            std::unique_lock lock(mutex);
            if(wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return stopping;})) return;
        }
    }
    void receive(double now) {
        std::shared_ptr<Raster> next;
        {
            std::unique_lock lock(mutex,std::try_to_lock);
            if(!lock || version==consumed) return;
            consumed=version;next=pending;
        }
        const bool changed=!current || !next || current->card.identity()!=next->card.identity();
        current=std::move(next);
        if(changed) {visibleSince=now;floater.reset();}
        if(!current) return;
        if(!texture) glGenTextures(1,&texture);
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        glPushAttrib(GL_TEXTURE_BIT);glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,current->width,current->height,0,GL_BGRA,GL_UNSIGNED_BYTE,current->pixels.data());
        glPopAttrib();glPopClientAttrib();
    }
public:
    explicit Hud(std::string runtime):directory(std::move(runtime)),worker([this]{run();}) {}
    ~Hud() {
        {std::lock_guard lock(mutex);stopping=true;}
        wake.notify_one();worker.join();
    }
    void release() {if(texture)glDeleteTextures(1,&texture);texture=0;consumed=~0u;}
    bool visible() const {return current && texture && current->card.identity()!=dismissed;}
    int count() const {return visible()?current->card.count:0;}
    bool interacting(double now) const {return shake.busy(now);}
    void update(const tracking::Camera& camera,double now) {
        receive(now);
        const auto key=visible() && floater.onscreen?current->card.identity():std::string{};
        const bool dismiss=shake.update(key,camera.axes[2].raw,camera.axes[1].raw,camera.axes[0].raw,
            camera.timestamp,camera.fresh(now));
        if(dismiss && current) {
            dismissed=current->card.identity();
            AsyncFile::instance().write(directory+"/notification-dismiss.json",dismissal(current->card,wallMilliseconds()));
        }
    }
    void place(space::Scene next,double now) {
        scene=std::move(next);
        if(visible())floater.update(scene,cardWidth,cardHeight(),now);
    }
    const space::Floater& placement()const{return floater;}
    void draw(double now) const {
        if(!visible()) return;
        const float alpha=float(std::clamp((now-visibleSince)/.3,0.,1.));
        glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT);
        // This translucent assembly is painted back-to-front. Test against
        // monitor depth, but never let an invisible/rounded layer write depth
        // that can reject the next layer of the card itself.
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        if(floater.safe && floater.opacity>0)drawCard(texture,current->card,scene,floater.position,cardWidth,cardHeight(),alpha*floater.opacity);
        if(floater.cueOpacity>0)drawCue(current->card,scene,floater.position,alpha*floater.cueOpacity,now);
        glPopAttrib();
    }
};
}
