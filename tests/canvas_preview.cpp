// Canvas overlays in the real renderer (docs/infinite-canvas-plan.md §4.5 item 3): a hidden window, an
// offline scene with synthetic window frames, and six stereo stills. A content smoke, not a pixel
// baseline (fonts and drivers vary): every overlay covers its projected region with visible pixels in
// both eyes, the eyes differ, and GL reports no error.
#define main rendererMain
#include "../src/main.cpp"
#undef main
#include <cassert>

namespace {
constexpr int width=1920, height=1080;
using Kind=canvas::OverlayQuad::Kind;
// One CPU frame per window: a coloured body, a darker title bar and light text-like lines.
struct PatternSource final : FrameSource {
    unsigned w=1, h=1, seed=0; bool sent=false; std::string failure;
    bool update(CapturedFrame& f) override {
        if(sent) return false;
        sent=true; f.width=f.sourceWidth=w; f.height=f.sourceHeight=h; f.texture=0; f.rgba.assign(size_t(w)*h*4, 255);
        const unsigned char base[3]={static_cast<unsigned char>(40+seed*37%120), static_cast<unsigned char>(50+seed*53%110), static_cast<unsigned char>(70+seed*29%120)};
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
            auto* p=&f.rgba[(size_t(y)*w+x)*4];
            const bool bar=y<h/14, line=y>h/8 && (y/18)%2==0 && x>w/16 && x<w/16+(w/2+(y*7919+seed*131)%(w/3));
            for(int c=0;c<3;++c) p[c]=bar ? base[c]/2 : line ? 230 : base[c];
        }
        return true;
    }
    void service() override {}
    void setDemand(bool, unsigned, unsigned) override {}
    void setFrameRate(unsigned, unsigned, double) override {}
    const char* transport() const override { return "pattern"; }
    unsigned requests() const override { return 0; }
    double importLatencyMs() const override { return -1; }
    const std::string& error() const override { return failure; }
};
windows::Record record(std::uint64_t address, const char* cls, const char* title, unsigned w, unsigned h, int focus, bool staged=false) {
    windows::Record r; r.address=address; r.cls=cls; r.title=title; r.w=w; r.h=h; r.focusHistoryID=focus;
    r.place=staged ? windows::Place::Stage : windows::Place::Park; r.pid=int(1000+address%1000);
    return r;
}
// Three terminals, so the search "term" has exactly three matches.
windows::List desk() {
    windows::List l; l.owner="preview"; l.seq=1;
    l.records={record(0x5000, "foot", "term: make check", 1280, 720, 3), record(0x5001, "foot", "term: journalctl -f", 1000, 700, 6),
        record(0x5002, "foot", "term: htop", 1000, 700, 8), record(0x5003, "firefox", "Omarchy manual - Mozilla Firefox", 1600, 900, 1),
        record(0x5004, "firefox", "Hyprland wiki - Mozilla Firefox", 1400, 800, 5), record(0x5005, "code", "canvas_overlay.hpp - omarchy-xr", 1600, 900, 0, true),
        record(0x5006, "Slack", "Slack - #xr", 1280, 720, 2), record(0x5007, "Spotify", "Spotify", 1280, 720, 9),
        record(0x5008, "org.gnome.Nautilus", "Downloads", 1000, 700, 10), record(0x5009, "obsidian", "Notes - Obsidian", 1280, 720, 4),
        record(0x500a, "thunderbird", "Inbox - Thunderbird", 1400, 800, 7), record(0x500b, "chromium", "Videos - Chromium", 1280, 720, 11)};
    return l;
}
void screenshot(const std::filesystem::path& path, const std::vector<unsigned char>& pixels) {
    const int w=2*width;
    std::vector<unsigned char> upright(pixels.size());
    for(int y=0;y<height;++y) std::copy_n(pixels.data()+size_t(height-y-1)*w*4, w*4, upright.data()+size_t(y)*w*4);
    auto* image=cairo_image_surface_create_for_data(upright.data(), CAIRO_FORMAT_ARGB32, w, height, w*4);
    assert(cairo_surface_write_to_png(image, path.c_str())==CAIRO_STATUS_SUCCESS); cairo_surface_destroy(image);
}
// The scene drawCanvasOverlays builds.
notifications::space::Scene overlayScene(const View& v) {
    notifications::space::Scene scene;
    scene.view=v.currentView(); scene.eye={-v.panX, -v.panY, -v.panZ};
    scene.tanV=std::tan(v.fov*pi/360); scene.tanH=scene.tanV*v.aspect(); scene.ipd=v.ipd/1000;
    return scene;
}
void ease(View& v, int frames) {
    for(int frame=0;frame<frames;++frame) { v.lastCameraTime=monotonicSeconds()-1./120; v.stepCanvas(v.easeCamera()); }
}
// Half a second of overlay time at 60 Hz (fades and berths settle), then one stereo render.
std::vector<unsigned char> settle(View& v) {
    ease(v, 120);
    double t=v.lastCameraTime;
    for(int i=0;i<30;++i) { t+=1/60.; v.lastCameraTime=t; v.projectPanels(2*width, height, t); v.renderScene(2*width, height, true, false, 2*width, height); }
    assert(glGetError()==GL_NO_ERROR);
    std::vector<unsigned char> pixels(size_t(2*width)*height*4);
    glReadPixels(0, 0, 2*width, height, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}
// The central 60 % of a quad as projected into one eye, as GL window pixels (y up).
struct Box { int x0, y0, x1, y1; };
Box projected(const View& v, const canvas::OverlayQuad& q, int eye) {
    const auto scene=overlayScene(v);
    auto p=scene.camera(q.centre); p.x-=eye==0 ? -v.ipd/2000.f : v.ipd/2000.f;
    const float depth=-p.z, tanV=std::tan(v.fov*pi/360), tanH=tanV*width/height;
    assert(depth>.1f);
    const float cx=(p.x/depth/tanH+1)/2*width, cy=(p.y/depth/tanV+1)/2*height;
    const float hw=.6f*q.width/2/depth/tanH*width/2, hh=.6f*q.height/2/depth/tanV*height/2;
    const auto clampX=[](float x) { return std::clamp(int(x), 0, width); };
    const auto clampY=[](float y) { return std::clamp(int(y), 0, height); };
    return {eye*width+clampX(cx-hw), clampY(cy-hh), eye*width+clampX(cx+hw), clampY(cy+hh)};
}
double visible(const std::vector<unsigned char>& pixels, const Box& b) {
    size_t lit=0, total=0;
    for(int y=b.y0;y<b.y1;++y) for(int x=b.x0;x<b.x1;++x) {
        const auto* p=&pixels[(size_t(y)*2*width+x)*4];
        ++total; if(p[0]+p[1]+p[2]>30) ++lit;
    }
    return total ? double(lit)/double(total) : 0;
}
// The overlay of this kind is drawn and more than half of its region is lit in both eyes.
void assertOverlay(View& v, const std::vector<unsigned char>& pixels, Kind kind, const char* still) {
    const auto quads=v.canvas->overlayQuads(overlayScene(v), v.lastCameraTime);
    const auto it=std::find_if(quads.begin(), quads.end(), [&](const auto& q) { return q.kind==kind; });
    assert(it!=quads.end() && it->texture && it->alpha>.99f);
    for(int eye=0;eye<2;++eye) {
        const auto box=projected(v, *it, eye);
        assert(box.x1-box.x0>20 && box.y1-box.y0>8);
        const double lit=visible(pixels, box);
        std::cout << still << " eye " << eye << ": " << int(lit*100) << "% of the overlay region lit\n";
        assert(lit>.5);
    }
}
void assertEyesDiffer(const std::vector<unsigned char>& pixels) {
    size_t differ=0;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const auto* l=&pixels[(size_t(y)*2*width+x)*4]; const auto* r=l+size_t(width)*4;
        if(std::abs(l[0]-r[0])+std::abs(l[1]-r[1])+std::abs(l[2]-r[2])>24) ++differ;
    }
    assert(differ>size_t(width)*height/200);
}
void still(View& v, const std::filesystem::path& directory, const char* name, Kind kind) {
    const auto pixels=settle(v);
    assertOverlay(v, pixels, kind, name); assertEyesDiffer(pixels);
    screenshot(directory/name, pixels);
}
// The staged window's centre in the left eye is lit (Fill has no overlay of its own).
void fillStill(View& v, const std::filesystem::path& directory) {
    v.navigate({View::Verb::Fill});
    assert(v.canvas->state==canvas::Scene::State::Fill && v.canvas->filled=="0x5005");
    const auto pixels=settle(v);
    assert(visible(pixels, {width/2-300, height/2-150, width/2+300, height/2+150})>.9);
    assertEyesDiffer(pixels);
    screenshot(directory/"fill.png", pixels);
    v.navigate({View::Verb::Fill});
    assert(v.canvas->state==canvas::Scene::State::Work && v.canvas->filled.empty());
}
void run(View& v, const std::filesystem::path& directory) {
    using State=canvas::Scene::State;
    v.navigate({View::Verb::Fit}); assert(v.canvas->state==State::Overview);
    still(v, directory, "overview.png", Kind::Radar);
    v.navigate({View::Verb::Search}); v.searchInput("term", "-");
    assert(v.canvas->state==State::Search && v.canvas->search.results.size()==3);
    still(v, directory, "search.png", Kind::Palette);
    assertOverlay(v, settle(v), Kind::Radar, "search.png radar");
    // Dwell is gated by an overlay under the gaze ray, not by the overlay being shown: the view centre
    // meets the palette's top edge; 8° up (above it) or down (between it and the radar) meets nothing.
    assert(v.canvas->overlayUnderGaze(overlayScene(v)));
    const float pitch=v.pitch;
    for(const float offset:{8.f, -8.f}) { v.pitch=pitch+offset; assert(!v.canvas->overlayUnderGaze(overlayScene(v))); }
    v.pitch=pitch;
    v.searchInput("term", "esc"); v.searchInput("", "esc");
    assert(!v.canvas->search.open && v.canvas->state==State::Overview);
    v.navigate({View::Verb::Fit}); assert(v.canvas->state==State::Work && v.canvas->landed=="0x5005");
    v.canvas->switcherStep(1, monotonicSeconds()); v.canvas->switcherStep(1, monotonicSeconds()); v.canvas->revealSwitcher(monotonicSeconds()+.3);
    assert(v.canvas->switcher.revealed && v.canvas->overlayOpen());
    still(v, directory, "switcher.png", Kind::Switcher);
    v.navigate({.verb=View::Verb::Switch, .begin=true, .output="cancel"}); assert(!v.canvas->switcher.active);
    v.navigate({View::Verb::Help}); assert(v.canvas->helpOpen);
    still(v, directory, "help.png", Kind::Help);
    v.navigate({View::Verb::Help}); assert(!v.canvas->helpOpen);
    fillStill(v, directory);
    v.navigate({View::Verb::Pin}); assert(v.canvas->pinnedCount()==1 && v.canvas->find("0x5005")->pinned);
    still(v, directory, "pinned.png", Kind::Pinned);
    // Hidden overlays fade out and leave nothing to draw but the pinned window.
    std::vector<canvas::OverlayQuad> quads;
    for(int step=1;step<=10;++step) quads=v.canvas->overlayQuads(overlayScene(v), v.lastCameraTime+step*.05);
    assert(quads.size()==1 && quads[0].kind==Kind::Pinned && quads[0].name=="0x5005");
    v.navigate({View::Verb::Pin}); assert(v.canvas->pinnedCount()==0);
    assert(v.canvas->overlayQuads(overlayScene(v), v.lastCameraTime+1).empty());
}
}
int main(int argc, char** argv) {
    if(argc<2) { std::cerr << "Usage: canvas-preview OUTPUT_DIR\n"; return 1; }
    const std::filesystem::path directory=argv[1]; std::filesystem::create_directories(directory);
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    auto* window=SDL_CreateWindow("Canvas overlay preview", 0, 0, 2*width, height, SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN); assert(window);
    auto context=SDL_GL_CreateContext(window); assert(context);
    std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
    char temp[]="/tmp/xr-canvas-preview-XXXXXX"; assert(mkdtemp(temp));
    const std::string pose=std::string(temp)+"/pose.sock", canvasPath=std::string(temp)+"/canvas.tsv", empty;
    {
        std::vector<Panel> none;
        View v(none, false, spatial::Workspace{80}, 24, empty, pose, false, true, 64, 28, canvasPath, 60, false);
        v.window=window; v.mode=View::SceneMode::Canvas; v.canvasPath=v.layoutPath;
        v.canvas=std::make_unique<canvas::Scene>(canvas::Ring{}, canvas::Settings{}, "", true);
        v.environment=std::make_unique<SkyEnvironment>("");
        v.accent.path=std::string(temp)+"/colors.toml"; v.accent.update(0);
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v.maxTexture);
        v.primeCamera(); v.startCanvas();
        v.canvas->adopt(desk(), monotonicSeconds());
        unsigned seed=0;
        for(auto& w:v.canvas->windows) {
            auto source=std::make_unique<PatternSource>(); source->w=w.pixelW; source->h=w.pixelH; source->seed=seed++;
            if(!w.texture) w.texture=gltex::create();
            w.source=std::move(source);
        }
        v.updateWindowCaptures();
        v.applyAim(v.canvas->land("0x5005", v.baseView()));
        glEnable(GL_DEPTH_TEST);
        run(v, directory);
        assert(glGetError()==GL_NO_ERROR);
        v.finishCanvas(); v.environment->release(); v.halo.release();
    }
    AsyncFile::instance().flush(); std::filesystem::remove_all(temp);
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    std::cout << "Canvas preview: overview (radar), search (palette), switcher, help, fill and pinned stills written to " << directory << '\n';
}
