#pragma once
#include <EGL/egl.h>
#include <SDL_opengl.h>
#include <cstdint>
#include <vector>

// GL_TIME_ELAPSED around capture blits and the scene. No-ops when the context
// cannot time queries, so a GL 2.1 preview or software renderer still runs.
class GpuTimers {
public:
    enum Kind { Capture = 0, Scene = 1 };

    void probe() {
        if (probed) return;
        probed = true;
        auto load = [](const char* name) -> void* {
            if (void* pointer = reinterpret_cast<void*>(eglGetProcAddress(name))) return pointer;
            return SDL_GL_GetProcAddress(name);
        };
        beginQuery = reinterpret_cast<BeginFn>(load("glBeginQuery"));
        if (!beginQuery) beginQuery = reinterpret_cast<BeginFn>(load("glBeginQueryEXT"));
        endQuery = reinterpret_cast<EndFn>(load("glEndQuery"));
        if (!endQuery) endQuery = reinterpret_cast<EndFn>(load("glEndQueryEXT"));
        genQueries = reinterpret_cast<GenFn>(load("glGenQueries"));
        deleteQueries = reinterpret_cast<DeleteFn>(load("glDeleteQueries"));
        getAvailable = reinterpret_cast<AvailFn>(load("glGetQueryObjectuiv"));
        if (!getAvailable) getAvailable = reinterpret_cast<AvailFn>(load("glGetQueryObjectuivEXT"));
        getResult64 = reinterpret_cast<Get64Fn>(load("glGetQueryObjectui64v"));
        if (!getResult64) getResult64 = reinterpret_cast<Get64Fn>(load("glGetQueryObjectui64vEXT"));
        getResult32 = reinterpret_cast<Get32Fn>(load("glGetQueryObjectuiv"));
        if (!beginQuery || !endQuery || !genQueries || !getAvailable || (!getResult64 && !getResult32)) return;

        while (glGetError() != GL_NO_ERROR) {}
        unsigned probeId = 0;
        genQueries(1, &probeId);
        if (!probeId || glGetError() != GL_NO_ERROR) { drain(); return; }
        beginQuery(timeElapsed, probeId);
        endQuery(timeElapsed);
        if (glGetError() != GL_NO_ERROR) { drain(); return; }
        if (deleteQueries) deleteQueries(1, &probeId);
        for (auto& slot : slots) {
            genQueries(1, &slot.capture);
            genQueries(1, &slot.scene);
        }
        drain();
        available = slots[0].capture && slots[0].scene;
    }

    bool begin(Kind kind) {
        if (!available) return false;
        if (active < 0) {
            for (int i = 0; i < slotCount; ++i) if (!slots[i].pending) { active = i; break; }
            if (active < 0) return false;
        }
        beginQuery(timeElapsed, kind == Capture ? slots[active].capture : slots[active].scene);
        return true;
    }

    void end(Kind kind) {
        if (!available || active < 0) return;
        endQuery(timeElapsed);
        if (kind == Scene) {
            slots[active].pending = true;
            active = -1;
        }
    }

    void collect(std::vector<double>& captureMs, std::vector<double>& sceneMs) {
        if (!available) return;
        for (auto& slot : slots) {
            if (!slot.pending) continue;
            unsigned captureReady = 0, sceneReady = 0;
            getAvailable(slot.capture, queryAvailable, &captureReady);
            getAvailable(slot.scene, queryAvailable, &sceneReady);
            if (!captureReady || !sceneReady) continue;
            captureMs.push_back(nanoseconds(slot.capture) / 1e6);
            sceneMs.push_back(nanoseconds(slot.scene) / 1e6);
            slot.pending = false;
        }
    }

private:
    static constexpr unsigned timeElapsed = 0x88BF;
    static constexpr unsigned queryResult = 0x8866;
    static constexpr unsigned queryAvailable = 0x8867;
    static constexpr int slotCount = 3;
    using BeginFn = void (*)(unsigned, unsigned);
    using EndFn = void (*)(unsigned);
    using GenFn = void (*)(int, unsigned*);
    using DeleteFn = void (*)(int, const unsigned*);
    using AvailFn = void (*)(unsigned, unsigned, unsigned*);
    using Get64Fn = void (*)(unsigned, unsigned, std::uint64_t*);
    using Get32Fn = void (*)(unsigned, unsigned, unsigned*);

    struct Slot { unsigned capture = 0, scene = 0; bool pending = false; };

    void drain() const { while (glGetError() != GL_NO_ERROR) {} }
    double nanoseconds(unsigned id) const {
        if (getResult64) {
            std::uint64_t value = 0;
            getResult64(id, queryResult, &value);
            return static_cast<double>(value);
        }
        unsigned value = 0;
        getResult32(id, queryResult, &value);
        return static_cast<double>(value);
    }

    bool probed = false, available = false;
    int active = -1;
    Slot slots[slotCount]{};
    BeginFn beginQuery = nullptr;
    EndFn endQuery = nullptr;
    GenFn genQueries = nullptr;
    DeleteFn deleteQueries = nullptr;
    AvailFn getAvailable = nullptr;
    Get64Fn getResult64 = nullptr;
    Get32Fn getResult32 = nullptr;
};
