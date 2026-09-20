#include "capture.hpp"
#include "layout.hpp"
#include "curvature.hpp"
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>

namespace {
constexpr float pi = 3.14159265358979323846f;
struct Panel {
    PanelLayout layout;
    std::unique_ptr<DesktopCapture> capture;
    CapturedFrame frame;
    GLuint texture = 0;
    unsigned width = 0, height = 0, frames = 0;
    bool failed = false;
};
// Tessellate in horizontal strips so each monitor can have its own curvature.
void surface(const spatial::Pose& pose, float x, float y, float w, float h, float offset) {
    const int segments = pose.surfaceBend == 0 ? 1 : std::clamp(int(std::ceil(std::abs(w*pose.surfaceBend)*180/ pi)), 8, 180);
    glBegin(GL_QUAD_STRIP);
    for (int i=0;i<=segments;++i) {
        const float u=float(i)/segments;
        auto bottom=spatial::vertex(pose,x+u*w,y,offset);
        auto top=spatial::vertex(pose,x+u*w,y+h,offset);
        glTexCoord2f(u,1); glVertex3f(bottom.x,bottom.y,bottom.z);
        glTexCoord2f(u,0); glVertex3f(top.x,top.y,top.z);
    }
    glEnd();
}
void drawPanel(const Panel& panel, size_t index, float cx, float cy, float span, float distance, float workspace) {
    constexpr float unit=1.f/900.f;
    const auto& p=panel.layout;
    const float w=p.width*unit,h=p.height*unit;
    const auto pose=spatial::pose((p.x+p.width/2-cx)*unit,-(p.y+p.height/2-cy)*unit,w,span,distance,workspace,p.curvature);
    const float colors[3][3]{{.35f,.65f,.95f},{.4f,.85f,.65f},{.8f,.55f,.95f}};
    if (panel.failed) glColor3f(.9f,.25f,.25f); else glColor3fv(colors[index%3]);
    surface(pose,-w/2-.02f,-h/2-.02f,w+.04f,h+.04f,0);
    glColor3f(.065f,.085f,.12f);
    surface(pose,-w/2,-h/2,w,h,.005f);
    if (panel.width && !panel.failed) {
        float tw=w,th=tw*panel.height/panel.width;
        if (th>h) { th=h;tw=th*panel.width/panel.height; }
        glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,panel.texture);glColor3f(1,1,1);
        surface(pose,-tw/2,-th/2,tw,th,.01f);
        glDisable(GL_TEXTURE_2D);
    } else {
        for (int row=0;row<8;++row) {
            glColor3f(.2f,.25f,.32f);
            surface(pose,-w*.44f,h*.32f-row*h*.09f,w*.7f,h*.025f,.01f);
        }
    }
}
int preview(std::vector<Panel>& panels, bool smoke, float workspace) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::cerr << SDL_GetError() << '\n'; return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window* window = SDL_CreateWindow("Omarchy XR | Right-drag: look | Middle-drag: pan | Wheel: zoom | F: fit | R: recenter",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,
        SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) { std::cerr << SDL_GetError() << '\n'; SDL_Quit(); return 1; }
    auto context = SDL_GL_CreateContext(window);
    if (!context) { std::cerr << SDL_GetError() << '\n'; SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(1);
    GLint maxTexture; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxTexture);
    std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\nPanels: " << panels.size() << std::endl;
    float left=panels[0].layout.x, top=panels[0].layout.y, right=left, bottom=top;
    for (auto& p : panels) {
        left=std::min(left,p.layout.x); top=std::min(top,p.layout.y);
        right=std::max(right,p.layout.x+p.layout.width); bottom=std::max(bottom,p.layout.y+p.layout.height);
        glGenTextures(1,&p.texture); glBindTexture(GL_TEXTURE_2D,p.texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    const float cx=(left+right)/2, cy=(top+bottom)/2;
    float yaw=0,pitch=0,panX=0,panY=0,distance=5;
    auto fit = [&] {
        int w,h; SDL_GL_GetDrawableSize(window,&w,&h);
        const float aspect=float(std::max(w,1))/std::max(h,1);
        distance=std::max((bottom-top)/900.f,(right-left)/900.f/aspect)/2/std::tan(65.f*pi/360.f)*1.12f;
        distance=std::max(distance,1.f); panX=panY=yaw=pitch=0;
    };
    fit();
    glEnable(GL_DEPTH_TEST);
    bool running=true; int result=0,drawn=0;
    const auto started=SDL_GetTicks64();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type==SDL_QUIT) running=false;
            if (event.type==SDL_KEYDOWN) {
                if (event.key.keysym.sym==SDLK_ESCAPE) running=false;
                if (event.key.keysym.sym==SDLK_r) yaw=pitch=panX=panY=0;
                if (event.key.keysym.sym==SDLK_f) fit();
            }
            if (event.type==SDL_MOUSEWHEEL) distance=std::clamp(distance*std::pow(.9f,float(event.wheel.y)),.3f,10000.f);
            if (event.type==SDL_MOUSEMOTION) {
                if (event.motion.state&SDL_BUTTON_RMASK) { yaw+=event.motion.xrel*.15f; pitch=std::clamp(pitch+event.motion.yrel*.15f,-80.f,80.f); }
                if (event.motion.state&SDL_BUTTON_MMASK) { panX+=event.motion.xrel*distance*.0015f; panY-=event.motion.yrel*distance*.0015f; }
            }
        }
        for (auto& p : panels) {
            if (!p.capture || p.failed) continue;
            if (p.capture->update(p.frame)) {
                if (p.frame.width>unsigned(maxTexture) || p.frame.height>unsigned(maxTexture)) {
                    std::cerr << p.layout.output << ": exceeds GPU texture size\n"; p.failed=true; result=1; continue;
                }
                glBindTexture(GL_TEXTURE_2D,p.texture);
                if (p.width!=p.frame.width || p.height!=p.frame.height) {
                    p.width=p.frame.width; p.height=p.frame.height;
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,p.width,p.height,0,GL_RGBA,GL_UNSIGNED_BYTE,p.frame.rgba.data());
                } else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,p.width,p.height,GL_RGBA,GL_UNSIGNED_BYTE,p.frame.rgba.data());
                ++p.frames;
            }
            if (!p.capture->error().empty()) {
                std::cerr << p.layout.output << ": " << p.capture->error() << '\n';
                p.failed=true; p.capture.reset(); result=1;
            }
        }
        if (smoke && (result || SDL_GetTicks64()-started>15000)) { result=1; break; }
        int w,h; SDL_GL_GetDrawableSize(window,&w,&h);
        if (w<=0 || h<=0) { SDL_Delay(16); continue; }
        glViewport(0,0,w,h); glClearColor(.025f,.035f,.055f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        const double t=.1*std::tan(65.0*pi/360),r=t*w/h;
        glFrustum(-r,r,-t,t,.1,20000);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glRotatef(pitch,1,0,0); glRotatef(yaw,0,1,0); glTranslatef(panX,panY,0);
        for (size_t i=0;i<panels.size();++i) drawPanel(panels[i],i,cx,cy,(right-left)/900.f,distance,workspace);
        if (glGetError()!=GL_NO_ERROR) { std::cerr << "OpenGL rendering error\n"; result=1; break; }
        SDL_GL_SwapWindow(window); ++drawn;
        if (smoke && drawn>=10 && std::all_of(panels.begin(),panels.end(),[](const Panel& p){ return !p.capture || p.frames>=10; })) running=false;
        SDL_Delay(1);
    }
    for (auto& p : panels) {
        if (p.capture || p.failed) std::cout << p.layout.output << ": " << p.frames << " frames (" << p.width << 'x' << p.height << ")\n";
        if (smoke && p.capture && p.frames<10) result=1;
        glDeleteTextures(1,&p.texture);
    }
    if (smoke && drawn<10) result=1;
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit(); return result;
}
}
int main(int argc,char** argv) {
    try {
        bool smoke=false,list=false; int fps=30; float workspace=0,surfaceCurve=0;
        std::vector<PanelLayout> layouts; std::string path;
        for (int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            auto value=[&]() -> std::string { if (++i>=argc || std::string_view(argv[i]).starts_with("--") || !*argv[i]) throw std::runtime_error(arg+" requires a value"); return argv[i]; };
            if (arg=="--help") { std::cout << "Usage: omarchy-xr [--capture OUTPUT ... | --layout FILE | --list-outputs] [--fps 1..60] [--workspace-curvature 0..100] [--surface-curvature 0..100] [--smoke-test]\nRight-drag: look; middle-drag: pan; wheel: zoom; F: fit; R: recenter; Esc: exit\n"; return 0; }
            else if (arg=="--version") { std::cout << "omarchy-xr 0.2.0-dev\n"; return 0; }
            else if (arg=="--smoke-test") smoke=true;
            else if (arg=="--list-outputs") list=true;
            else if (arg=="--capture") { auto name=value(); layouts.push_back({name,float(layouts.size())*2000,0,1920,1080}); }
            else if (arg=="--layout") path=value();
            else if (arg=="--workspace-curvature" || arg=="--surface-curvature") {
                auto text=value();size_t end=0;float c=std::stof(text,&end);
                if(end!=text.size() || !std::isfinite(c) || c<0 || c>100) throw std::runtime_error("Curvature must be 0..100");
                if(arg=="--workspace-curvature") workspace=c;else surfaceCurve=c;
            }
            else if (arg=="--fps") { auto text=value(); size_t end=0; fps=std::stoi(text,&end); if (end!=text.size() || fps<1 || fps>60) throw std::runtime_error("FPS must be 1..60"); }
            else throw std::runtime_error("Unknown option: "+arg);
        }
        if ((!path.empty() && !layouts.empty()) || (list && (!path.empty() || !layouts.empty() || smoke))) throw std::runtime_error("Choose one of --layout, --capture, or --list-outputs");
        if (list) { DesktopCapture c; if (!c.connect()) throw std::runtime_error(c.error()); for (auto& o:c.outputs()) std::cout << o << '\n'; return 0; }
        if (!path.empty()) layouts=readLayout(path);
        bool live=!layouts.empty();
        if (!live) for (int i=0;i<3;++i) layouts.push_back({"",float(i)*2000,0,1920,1080});
        std::unordered_set<std::string> names;
        std::vector<Panel> panels;
        for (auto& layout:layouts) {
            Panel p; p.layout=layout;
            if (path.empty()) p.layout.curvature=surfaceCurve;
            if (live) {
                if (!names.insert(layout.output).second) throw std::runtime_error("Duplicate capture output: "+layout.output);
                p.capture=std::make_unique<DesktopCapture>(); p.capture->setFrameRate(fps);
                if (!p.capture->connect() || !p.capture->select(layout.output)) throw std::runtime_error(p.capture->error());
            }
            panels.push_back(std::move(p));
        }
        return preview(panels,smoke,workspace);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
