#include "capture.hpp"
#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {
constexpr float pi = 3.14159265358979323846f;

// Mouse orientation is a development stand-in for a future SDK pose source.
struct PreviewCamera {
    float yaw = 0;
    float pitch = 0;
    void look(int dx, int dy) {
        yaw += static_cast<float>(dx) * 0.15f;
        pitch = std::clamp(pitch + static_cast<float>(dy) * 0.15f, -80.f, 80.f);
    }
    void recenter() { yaw = pitch = 0; }
};

void rectangle(float x, float y, float w, float h, float z) {
    glBegin(GL_QUADS);
    glVertex3f(x, y, z); glVertex3f(x + w, y, z);
    glVertex3f(x + w, y + h, z); glVertex3f(x, y + h, z);
    glEnd();
}

void panel(int index, GLuint texture, float aspect) {
    glPushMatrix();
    glRotatef(static_cast<float>(index) * 43.f, 0, 1, 0);
    glTranslatef(0, 0, -3.3f);
    const float colors[3][3] = {{.35f,.65f,.95f}, {.4f,.85f,.65f}, {.8f,.55f,.95f}};
    glColor3fv(colors[index + 1]);
    rectangle(-1.15f, -.67f, 2.3f, 1.34f, 0);
    glColor3f(.065f, .085f, .12f);
    rectangle(-1.12f, -.64f, 2.24f, 1.20f, .005f);
    if (index == 0 && texture) {
        const float w = std::min(2.24f, 1.20f * aspect);
        const float h = w / aspect;
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 1); glVertex3f(-w/2, -h/2, .015f);
        glTexCoord2f(1, 1); glVertex3f(w/2, -h/2, .015f);
        glTexCoord2f(1, 0); glVertex3f(w/2, h/2, .015f);
        glTexCoord2f(0, 0); glVertex3f(-w/2, h/2, .015f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    } else for (int row = 0; row < 8; ++row) {
        glColor3f(.16f + .015f * row, .22f, .3f);
        rectangle(-1.f, .36f - row * .115f, 1.1f + .13f * (row % 4), .045f, .01f);
    }
    glPopMatrix();
}

int preview(bool smoke, DesktopCapture* capture, const std::string& output) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL initialization: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* window = SDL_CreateWindow(
        "Omarchy XR | Preview panels | Right-drag: look | R: recenter | Esc: exit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::cerr << "Window creation: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    if (capture) {
        const std::string title = "Omarchy XR | Live: " + output + " | Right-drag: look | R: recenter | Esc: exit";
        SDL_SetWindowTitle(window, title.c_str());
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        std::cerr << "OpenGL context: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window); SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\n"
              << (capture ? "Live capture on center panel; side panels are placeholders.\n"
                          : "Preview only: no desktop capture or VITURE tracking yet.\n");
    GLuint texture = 0;
    CapturedFrame captured;
    unsigned textureWidth = 0, textureHeight = 0;
    int capturedFrames = 0;
    const auto started = SDL_GetTicks64();
    if (capture) {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        std::cout << "Keep this window on a different output from " << output
                  << " to avoid recursive capture.\n";
    }
    glEnable(GL_DEPTH_TEST);
    PreviewCamera camera;
    bool running = true;
    int frames = 0;
    int result = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (event.key.keysym.sym == SDLK_r) camera.recenter();
            }
            if (event.type == SDL_MOUSEMOTION && (event.motion.state & SDL_BUTTON_RMASK))
                camera.look(event.motion.xrel, event.motion.yrel);
        }
        if (capture) {
            if (capture->update(captured)) {
                glBindTexture(GL_TEXTURE_2D, texture);
                if (textureWidth != captured.width || textureHeight != captured.height) {
                    textureWidth = captured.width; textureHeight = captured.height;
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<int>(textureWidth),
                        static_cast<int>(textureHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE, captured.rgba.data());
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<int>(textureWidth),
                        static_cast<int>(textureHeight), GL_RGBA, GL_UNSIGNED_BYTE, captured.rgba.data());
                }
                ++capturedFrames;
            }
            if (!capture->error().empty()) {
                std::cerr << capture->error() << '\n'; result = 1; break;
            }
            if (smoke && SDL_GetTicks64() - started > 10000) {
                std::cerr << "Live capture smoke test timed out\n"; result = 1; break;
            }
        }
        int width = 0, height = 0;
        SDL_GL_GetDrawableSize(window, &width, &height);
        if (width <= 0 || height <= 0) { SDL_Delay(16); continue; }
        glViewport(0, 0, width, height);
        glClearColor(.025f, .035f, .055f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        const double top = .1 * std::tan(65.0 * pi / 360.0);
        const double right = top * width / height;
        glFrustum(-right, right, -top, top, .1, 100);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glRotatef(camera.pitch, 1, 0, 0);
        glRotatef(camera.yaw, 0, 1, 0);
        for (int i = -1; i <= 1; ++i) panel(i, textureWidth ? texture : 0, textureHeight ? float(textureWidth) / textureHeight : 1.f);
        if (smoke) {
            glFinish();
            if (glGetError() != GL_NO_ERROR) {
                std::cerr << "OpenGL rendering check failed\n";
                result = 1; running = false;
            }
        }
        SDL_GL_SwapWindow(window);
        if (smoke && ++frames >= 10 && (!capture || capturedFrames >= 10)) running = false;
        SDL_Delay(1);
    }
    if (smoke && (frames < 10 || (capture && capturedFrames < 10))) result = 1;
    if (capture) std::cout << "Captured " << capturedFrames << " frames (" << textureWidth << 'x' << textureHeight << ")\n";
    if (texture) glDeleteTextures(1, &texture);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
} // namespace

int main(int argc, char** argv) {
    bool smoke = false, listOutputs = false;
    std::string output;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help") {
            std::cout << "Usage: omarchy-xr [--help|--version|--smoke-test] [--list-outputs|--capture OUTPUT]\n"
                      << "3D preview: right-drag to look, R to recenter, Esc to exit.\n";
            return 0;
        }
        if (arg == "--version") { std::cout << "omarchy-xr 0.1.0-dev\n"; return 0; }
        if (arg == "--smoke-test") smoke = true;
        else if (arg == "--list-outputs") listOutputs = true;
        else if (arg == "--capture") {
            if (i + 1 >= argc || std::string_view(argv[i + 1]).empty() ||
                std::string_view(argv[i + 1]).starts_with("--")) {
                std::cerr << "--capture requires an output name; use --list-outputs\n"; return 2;
            }
            output = argv[++i];
        }
        else { std::cerr << "Unknown option: " << arg << '\n'; return 2; }
    }
    if (listOutputs && (!output.empty() || smoke)) {
        std::cerr << "--list-outputs cannot be combined with capture or smoke testing\n"; return 2;
    }
    std::unique_ptr<DesktopCapture> capture;
    if (listOutputs || !output.empty()) {
        capture = std::make_unique<DesktopCapture>();
        if (!capture->connect()) { std::cerr << capture->error() << '\n'; return 1; }
        if (listOutputs) {
            for (const auto& name : capture->outputs()) std::cout << name << '\n';
            return 0;
        }
        if (!capture->select(output)) { std::cerr << capture->error() << '\n'; return 1; }
    }
    return preview(smoke, capture.get(), output);
}
