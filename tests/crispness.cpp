// Text crispness through the panel path (docs/architecture.md, "Text crispness"): a Pango text raster
// at native size goes through the capture scale passes (capture_scale.hpp, the GL_LINEAR blits of
// gpu_capture.hpp present()), then the panel draw (a curved surface, curvature.hpp) into one eye of the
// glasses, 1920x1080 at 28° vertical FOV. Each variant is compared with a 4x4 supersampled reference
// of the native raster on the same surface: PSNR (blur and aliasing both lower it), the gradient
// energy ratio (below 1 soft, above 1 aliased) and the worst PSNR over eight 1/8-pixel offsets
// (shimmer while the head moves).
//   crispness [OUTPUT_DIR]      prints the table, writes crops (N-old/copy/new/reference.png), and fails
//                               unless panel_filter.hpp is never worse than the old path and 1 dB better
//                               on average.
#define GL_GLEXT_PROTOTYPES
#include "capture_plan.hpp"
#include "capture_scale.hpp"
#include "curvature.hpp"
#include "panel_filter.hpp"
#include <SDL.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <pango/pangocairo.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr int eyeW=1920, eyeH=1080, super=4;
constexpr float fov=28;
const float focal=eyeH/(2*std::tan(fov*spatial::pi/360));   // screen px per world unit at distance 1

// A terminal (monospace 14 px, light on dark) over most of it, UI text (sans 11 px, dark on light) below.
std::vector<unsigned char> textRaster(int w, int h) {
    auto* surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h); auto* cr=cairo_create(surface);
    cairo_set_source_rgb(cr, .118, .118, .18); cairo_paint(cr);
    auto* options=cairo_font_options_create(); cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_SLIGHT);
    auto* layout=pango_cairo_create_layout(cr); pango_cairo_context_set_font_options(pango_layout_get_context(layout), options);
    const auto lines=[&](const char* font, double r, double g, double b, int y0, int y1, int step) {
        auto* desc=pango_font_description_from_string(font); pango_layout_set_font_description(layout, desc);
        cairo_set_source_rgb(cr, r, g, b);
        const char* text[]={"$ make check-crispness && ./build/omarchy-xr --canvas ~/.local/state/omarchy-xr/canvas.tsv",
            "src/gpu_capture.hpp:189: glGenerateMipmap(GL_TEXTURE_2D); // the half-resolution level",
            "drwxr-xr-x  andreasfruth  1050 Sep 28 18:22  .config/omarchy/plugins/afruth.omarchy-xr/bin",
            "The quick brown fox jumps over the lazy dog 0123456789 il1| O0 rn m {}[]();:,. ~/-_=+*&^%$#@!"};
        for(int y=y0, i=0; y+step<=y1; y+=step, ++i) { cairo_move_to(cr, 12, y); pango_layout_set_text(layout, text[i%4], -1); pango_cairo_show_layout(cr, layout); }
        pango_font_description_free(desc);
    };
    lines("Monospace 10.5", .80, .84, .96, 8, h*2/3, 19);          // 14 px at 96 dpi
    cairo_set_source_rgb(cr, .95, .95, .96); cairo_rectangle(cr, 0, h*2/3, w, h-h*2/3); cairo_fill(cr);
    lines("Sans 8.25", .12, .12, .14, h*2/3+8, h, 16);               // 11 px UI text
    g_object_unref(layout); cairo_font_options_destroy(options);
    cairo_surface_flush(surface);
    std::vector<unsigned char> out(cairo_image_surface_get_data(surface), cairo_image_surface_get_data(surface)+size_t(w)*h*4);
    cairo_destroy(cr); cairo_surface_destroy(surface);
    return out;
}

GLuint texture(int w, int h, const void* pixels) {
    GLuint t=0; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    return t;
}
struct Target { GLuint fbo=0, color=0, depth=0; int w=0, h=0; };
Target target(int w, int h) {
    Target t{0, texture(w, h, nullptr), 0, w, h};
    glGenRenderbuffers(1, &t.depth); glBindRenderbuffer(GL_RENDERBUFFER, t.depth); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &t.fbo); glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    return t;
}
void release(Target& t) { glDeleteFramebuffers(1, &t.fbo); glDeleteTextures(1, &t.color); glDeleteRenderbuffers(1, &t.depth); t={}; }

// gpu_capture.hpp present(): halve until within 2x, then the exact size, each a GL_LINEAR blit.
GLuint capture(GLuint native, int sw, int sh, int dw, int dh) {
    GLuint src=0; glGenFramebuffers(1, &src); glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, native, 0);
    int cw=sw, ch=sh; GLuint out=0, scratch=0;
    for(const auto& pass:scalePasses(sw, sh, dw, dh)) {
        const GLuint dest=texture(int(pass.width), int(pass.height), nullptr);
        GLuint fbo=0; glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dest, 0);
        glBlitFramebuffer(0, 0, cw, ch, 0, 0, int(pass.width), int(pass.height), GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glDeleteFramebuffers(1, &src); src=fbo; glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
        if(scratch) glDeleteTextures(1, &scratch);
        scratch=out; out=dest; cw=int(pass.width); ch=int(pass.height);
    }
    if(scratch) glDeleteTextures(1, &scratch);
    glDeleteFramebuffers(1, &src); glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if(!out) { out=texture(sw, sh, nullptr); GLuint a=0, b=0; glGenFramebuffers(1, &a); glGenFramebuffers(1, &b);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, a); glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, native, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, b); glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out, 0);
        glBlitFramebuffer(0, 0, sw, sh, 0, 0, sw, sh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glDeleteFramebuffers(1, &a); glDeleteFramebuffers(1, &b); glBindFramebuffer(GL_FRAMEBUFFER, 0); }
    return out;
}

// The old sampling (trilinear, one level, no bias) or the renderer's (panel::mipmaps plus panel::lodBias).
void sample(bool renderer, float minification) {
    panel::mipmaps();
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, renderer ? panel::lodBias(minification) : 0.f);
}

// main.cpp surface(): horizontal strips on the bent pose, v flipped as the capture is top-down.
void surface(const spatial::Pose& pose, float w, float h) {
    const int segments=spatial::surfaceSegments(w, pose.surfaceBend);
    glBegin(GL_QUAD_STRIP);
    for(int i=0;i<=segments;++i) {
        const float u=float(i)/segments;
        const auto bottom=spatial::vertex(pose, -w/2+u*w, -h/2, .01f), top=spatial::vertex(pose, -w/2+u*w, h/2, .01f);
        glTexCoord2f(u, 1); glVertex3f(bottom.x, bottom.y, bottom.z);
        glTexCoord2f(u, 0); glVertex3f(top.x, top.y, top.z);
    }
    glEnd();
}
struct Case { const char* name; int layoutW, layoutH, nativeW, nativeH; float density, curve; };
spatial::Pose poseOf(const Case& c, float distance) {
    return spatial::pose(0, 0, c.layoutW/900.f, c.layoutW/900.f, distance, spatial::Workspace{0}, c.curve);
}
// drawEye's projection; the panel moves sideways by `shift` screen pixels.
std::vector<float> render(const Case& c, GLuint tex, Target& t, float distance, float shift) {
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo); glViewport(0, 0, t.w, t.h);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    const double top=.1*std::tan(fov*spatial::pi/360), right=top*eyeW/eyeH; glFrustum(-right, right, -top, top, .1, 20000);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glTranslatef(shift/focal*distance, 0, 0);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); glColor3f(1, 1, 1);
    surface(poseOf(c, distance), c.layoutW/900.f, c.layoutH/900.f);
    glDisable(GL_TEXTURE_2D);
    std::vector<unsigned char> rgba(size_t(t.w)*t.h*4); glReadPixels(0, 0, t.w, t.h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    // Luma, box-averaged down to one eye.
    const int k=t.w/eyeW; std::vector<float> out(size_t(eyeW)*eyeH, 0);
    for(int y=0;y<t.h;++y) for(int x=0;x<t.w;++x) { const auto* p=&rgba[(size_t(y)*t.w+x)*4]; out[size_t(y/k)*eyeW+x/k]+=(.2126f*p[0]+.7152f*p[1]+.0722f*p[2])/(k*k); }
    return out;
}
double psnr(const std::vector<float>& a, const std::vector<float>& b) {
    double e=0; for(size_t i=0;i<a.size();++i) { const double d=a[i]-b[i]; e+=d*d; }
    e/=double(a.size()); return 10*std::log10(255.0*255.0/std::max(e, 1e-9));
}
double gradient(const std::vector<float>& a) {
    double g=0; for(int y=0;y<eyeH-1;++y) for(int x=0;x<eyeW-1;++x) { const size_t i=size_t(y)*eyeW+x; g+=std::abs(a[i+1]-a[i])+std::abs(a[i+eyeW]-a[i]); }
    return g;
}
void crop(const std::filesystem::path& path, const std::vector<float>& a) {
    constexpr int cw=480, ch=180, x0=eyeW/2-cw, top=eyeH/2-ch;   // left of and above the centre, 2x nearest
    auto* s=cairo_image_surface_create(CAIRO_FORMAT_RGB24, cw*2, ch*2); auto* d=cairo_image_surface_get_data(s); const int stride=cairo_image_surface_get_stride(s);
    for(int y=0;y<ch*2;++y) for(int x=0;x<cw*2;++x) { const auto v=(unsigned char)std::clamp(a[size_t(eyeH-1-(top+y/2))*eyeW+x0+x/2], 0.f, 255.f); auto* p=d+y*stride+x*4; p[0]=p[1]=p[2]=v; }
    cairo_surface_mark_dirty(s); cairo_surface_write_to_png(s, path.c_str()); cairo_surface_destroy(s);
}
struct Score { double psnr=0, worst=1e9, sharp=0; };
}

int main(int argc, char** argv) {
    const std::filesystem::path out=argc>1 ? argv[1] : "build/crispness"; std::filesystem::create_directories(out);
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    auto* window=SDL_CreateWindow("crispness", 0, 0, 64, 64, SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN); assert(window);
    auto context=SDL_GL_CreateContext(window); assert(context);
    std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
    // A monitor from nearly filling the view down to an overview, flat and fully curved, and a canvas
    // window at Work (its layout 0.927 of the 941x1026 buffer, surface curvature 30%).
    const Case cases[]={{"monitor, 0.9 px/px", 1920, 1080, 1920, 1080, .9f, 0}, {"monitor, 0.7 px/px", 1920, 1080, 1920, 1080, .7f, 0},
        {"monitor, 0.7 px/px, curved 100%", 1920, 1080, 1920, 1080, .7f, 100}, {"ultrawide fitted, 0.56 px/px", 1920, 1080, 1920, 1080, .56f, 0},
        {"monitor, 0.45 px/px", 1920, 1080, 1920, 1080, .45f, 0}, {"monitor, 0.41 px/px (copy 2.4x)", 1920, 1080, 1920, 1080, .41f, 0},
        {"overview, 0.3 px/px", 1920, 1080, 1920, 1080, .3f, 0},
        {"canvas window at Work, 0.9 px/px", 872, 951, 941, 1026, .9f, 30}};
    bool better=true; double gain=0;
    std::printf("%-34s %-30s %-9s %6s %6s %6s %s\n", "case", "path", "copy", "PSNR", "worst", "sharp", "native");
    for(const auto& c:cases) {
        const auto raster=textRaster(c.nativeW, c.nativeH);
        const GLuint native=texture(c.nativeW, c.nativeH, raster.data());
        const float distance=focal/(900*c.density);
        // The renderer's demand: projected density x 1.25 into a bucket, of the layout size.
        const auto plan=adaptive::project({"", -c.layoutW/2.f, -c.layoutH/2.f, float(c.layoutW), float(c.layoutH), c.curve}, poseOf(c, distance), {}, {0, 0, 0}, eyeW, eyeH, fov, 0);
        adaptive::Quality quality; quality.update(plan.scale, 0);
        const float bucket=quality.update(plan.scale, 1);   // past the 0.35 s it waits before lowering
        const int oldW=int(std::ceil(c.layoutW*bucket)), oldH=int(std::ceil(c.layoutH*bucket));
        const auto [newW, newH]=panel::captureSize(c.nativeW, c.nativeH, oldW, oldH);
        Target big=target(eyeW*super, eyeH*super), eye=target(eyeW, eyeH);
        glBindTexture(GL_TEXTURE_2D, native);
        std::vector<std::vector<float>> reference;
        for(int s=0;s<8;++s) reference.push_back(render(c, native, big, distance, s/8.f));
        const double referenceSharp=gradient(reference[0]);
        // The old path (a copy at the demand, trilinear), the new copy size alone, and the renderer.
        struct Variant { const char* name; int w, h; bool renderer; };
        const Variant variants[]={{"old: demand copy, trilinear", oldW, oldH, false}, {"captureSize copy, trilinear", int(newW), int(newH), false},
                                  {"new: captureSize + lodBias", int(newW), int(newH), true}};
        Score scores[3];
        for(int v=0;v<3;++v) {
            const GLuint copy=capture(native, c.nativeW, c.nativeH, variants[v].w, variants[v].h);
            glBindTexture(GL_TEXTURE_2D, copy);
            sample(variants[v].renderer, float(variants[v].w)/(c.layoutW*c.density));
            auto& score=scores[v];
            for(int s=0;s<8;++s) {
                const auto image=render(c, copy, eye, distance, s/8.f);
                const double p=psnr(image, reference[s]); score.psnr+=p/8; score.worst=std::min(score.worst, p);
                if(s==0) { score.sharp=gradient(image)/referenceSharp; crop(out/(std::to_string(&c-cases)+(v==0 ? "-old.png" : v==1 ? "-copy.png" : "-new.png")), image); }
            }
            std::printf("%-34s %-30s %4dx%-4d %6.2f %6.2f %6.3f %5.2f\n", c.name, variants[v].name, variants[v].w, variants[v].h, score.psnr, score.worst, score.sharp,
                        double(variants[v].w)*variants[v].h/(double(c.nativeW)*c.nativeH));
            glDeleteTextures(1, &copy);
        }
        // Never worse anywhere (0.05 dB of noise), and at least 1 dB better on average.
        if(scores[2].psnr<scores[0].psnr-.05 || scores[2].worst<scores[0].worst-.05) better=false;
        gain+=(scores[2].psnr-scores[0].psnr)/double(std::size(cases));
        crop(out/(std::to_string(&c-cases)+"-reference.png"), reference[0]);
        release(big); release(eye); glDeleteTextures(1, &native);
    }
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    std::printf("mean PSNR gain %.2f dB\n", gain);
    if(!better || gain<1) { std::cerr << "The renderer's capture size and sampling are not sharper than the old path\n"; return 1; }
    std::cout << "Crispness: the renderer's capture size and filtering beat the old trilinear path; crops in " << out << '\n';
    return 0;
}
