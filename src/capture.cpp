#include "capture.hpp"
#include "pixels.hpp"
#include "wlr-screencopy-client.h"
#include <wayland-client.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

struct DesktopCapture::Impl {
    struct Output { Impl* owner; uint32_t id; wl_output* proxy; std::string name; };
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_shm* shm = nullptr;
    zwlr_screencopy_manager_v1* manager = nullptr;
    zwlr_screencopy_frame_v1* pending = nullptr;
    wl_buffer* buffer = nullptr;
    void* pixels = MAP_FAILED;
    size_t size = 0;
    uint32_t format = 0, width = 0, height = 0, stride = 0, flags = 0;
    std::vector<std::unique_ptr<Output>> outputs;
    Output* selected = nullptr;
    bool ready = false;
    unsigned interval = 33;
    std::string failure;
    using Clock = std::chrono::steady_clock;
    Clock::time_point requested{}, next{};

    void clearFrame() {
        if (pending) zwlr_screencopy_frame_v1_destroy(pending);
        if (buffer) wl_buffer_destroy(buffer);
        if (pixels != MAP_FAILED) munmap(pixels, size);
        pending = nullptr; buffer = nullptr; pixels = MAP_FAILED;
        size = 0; flags = 0; ready = false;
    }
    ~Impl() {
        clearFrame();
        for (auto& out : outputs) wl_output_release(out->proxy);
        if (manager) zwlr_screencopy_manager_v1_destroy(manager);
        if (shm) wl_shm_destroy(shm);
        if (registry) wl_registry_destroy(registry);
        if (display) wl_display_disconnect(display);
    }
    static void geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t,
                         int32_t, const char*, const char*, int32_t) {}
    static void mode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
    static void done(void*, wl_output*) {}
    static void scale(void*, wl_output*, int32_t) {}
    static void name(void* data, wl_output*, const char* value) {
        static_cast<Output*>(data)->name = value;
    }
    static void description(void*, wl_output*, const char*) {}
    static constexpr wl_output_listener outputListener{geometry, mode, done, scale, name, description};
    static void global(void* data, wl_registry* reg, uint32_t id, const char* iface, uint32_t version) {
        auto& self = *static_cast<Impl*>(data);
        if (std::strcmp(iface, wl_shm_interface.name) == 0)
            self.shm = static_cast<wl_shm*>(wl_registry_bind(reg, id, &wl_shm_interface, 1));
        else if (std::strcmp(iface, zwlr_screencopy_manager_v1_interface.name) == 0)
            self.manager = static_cast<zwlr_screencopy_manager_v1*>(
                wl_registry_bind(reg, id, &zwlr_screencopy_manager_v1_interface, 1));
        else if (std::strcmp(iface, wl_output_interface.name) == 0 && version >= 4) {
            auto out = std::make_unique<Output>();
            out->owner = &self; out->id = id;
            out->proxy = static_cast<wl_output*>(wl_registry_bind(reg, id, &wl_output_interface, 4));
            wl_output_add_listener(out->proxy, &outputListener, out.get());
            self.outputs.push_back(std::move(out));
        }
    }
    static void removed(void* data, wl_registry*, uint32_t id) {
        auto& self = *static_cast<Impl*>(data);
        for (auto it = self.outputs.begin(); it != self.outputs.end(); ++it) {
            if ((*it)->id != id) continue;
            if (self.selected == it->get()) {
                self.failure = "Captured output was disconnected";
                self.selected = nullptr;
            }
            wl_output_release((*it)->proxy);
            self.outputs.erase(it);
            break;
        }
    }
    static constexpr wl_registry_listener registryListener{global, removed};
    static void onBuffer(void* data, zwlr_screencopy_frame_v1* frame, uint32_t fmt,
                         uint32_t w, uint32_t h, uint32_t row) {
        auto& self = *static_cast<Impl*>(data);
        if (fmt != WL_SHM_FORMAT_XRGB8888 && fmt != WL_SHM_FORMAT_ARGB8888 &&
            fmt != WL_SHM_FORMAT_XBGR8888 && fmt != WL_SHM_FORMAT_ABGR8888) {
            self.failure = "Unsupported capture pixel format: " + std::to_string(fmt);
            return;
        }
        if (!w || !h || w > 16384 || h > 16384 || row < w * 4 || row % 4 ||
            uint64_t(row) * h > INT_MAX) {
            self.failure = "Invalid or excessively large capture buffer"; return;
        }
        self.format = fmt; self.width = w; self.height = h; self.stride = row;
        self.size = size_t(row) * h;
        const int fd = memfd_create("omarchy-xr-capture", MFD_CLOEXEC);
        if (fd < 0) { self.failure = "Cannot create capture shared memory"; return; }
        if (ftruncate(fd, static_cast<off_t>(self.size)) != 0) {
            close(fd); self.failure = "Cannot size capture shared memory"; return;
        }
        self.pixels = mmap(nullptr, self.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (self.pixels == MAP_FAILED) {
            close(fd); self.failure = "Cannot map capture shared memory"; return;
        }
        wl_shm_pool* pool = wl_shm_create_pool(self.shm, fd, static_cast<int>(self.size));
        self.buffer = wl_shm_pool_create_buffer(pool, 0, static_cast<int>(w),
            static_cast<int>(h), static_cast<int>(row), fmt);
        wl_shm_pool_destroy(pool);
        close(fd);
        zwlr_screencopy_frame_v1_copy(frame, self.buffer);
    }
    static void onFlags(void* data, zwlr_screencopy_frame_v1*, uint32_t value) {
        static_cast<Impl*>(data)->flags = value;
    }
    static void onReady(void* data, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t) {
        static_cast<Impl*>(data)->ready = true;
    }
    static void onFailed(void* data, zwlr_screencopy_frame_v1*) {
        static_cast<Impl*>(data)->failure = "Compositor rejected capture (output unavailable or capture blocked)";
    }
    // Bind version 1: only these four events can be delivered.
    static constexpr zwlr_screencopy_frame_v1_listener frameListener{
        onBuffer, onFlags, onReady, onFailed, nullptr, nullptr, nullptr};

    void pump() {
        if (wl_display_dispatch_pending(display) < 0) { failure = "Wayland connection lost"; return; }
        while (wl_display_prepare_read(display) != 0) {
            if (wl_display_dispatch_pending(display) < 0) { failure = "Wayland connection lost"; return; }
        }
        const int flushed = wl_display_flush(display);
        if (flushed < 0 && errno != EAGAIN) {
            wl_display_cancel_read(display); failure = "Wayland flush failed"; return;
        }
        pollfd descriptor{wl_display_get_fd(display), POLLIN, 0};
        const int result = poll(&descriptor, 1, 0);
        if (result > 0 && (descriptor.revents & POLLIN)) {
            if (wl_display_read_events(display) < 0 || wl_display_dispatch_pending(display) < 0)
                failure = "Wayland connection lost";
        } else {
            wl_display_cancel_read(display);
            if ((result < 0 && errno != EINTR) || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)))
                failure = "Wayland capture socket disconnected";
        }
    }
};

DesktopCapture::DesktopCapture() : impl(std::make_unique<Impl>()) {}
DesktopCapture::~DesktopCapture() = default;
void DesktopCapture::setFrameRate(unsigned fps) { impl->interval = 1000 / std::clamp(fps, 1u, 60u); }
const std::string& DesktopCapture::error() const { return impl->failure; }
bool DesktopCapture::connect() {
    auto& s = *impl;
    s.display = wl_display_connect(nullptr);
    if (!s.display) { s.failure = "Cannot connect to Wayland; run inside your Hyprland session"; return false; }
    s.registry = wl_display_get_registry(s.display);
    wl_registry_add_listener(s.registry, &Impl::registryListener, &s);
    if (wl_display_roundtrip(s.display) < 0 || wl_display_roundtrip(s.display) < 0) {
        s.failure = "Wayland output discovery failed"; return false;
    }
    if (!s.manager || !s.shm) {
        s.failure = "Compositor lacks wlr-screencopy/shared-memory capture support"; return false;
    }
    return true;
}
std::vector<std::string> DesktopCapture::outputs() const {
    std::vector<std::string> names;
    for (const auto& out : impl->outputs) if (!out->name.empty()) names.push_back(out->name);
    return names;
}
bool DesktopCapture::select(const std::string& name) {
    for (auto& out : impl->outputs) if (out->name == name) { impl->selected = out.get(); return true; }
    impl->failure = "Unknown output '" + name + "'; use --list-outputs";
    return false;
}
bool DesktopCapture::update(CapturedFrame& frame) {
    auto& s = *impl;
    if (!s.failure.empty() || !s.selected) return false;
    s.pump();
    if (!s.failure.empty()) return false;
    const auto now = Impl::Clock::now();
    if (s.pending && !s.ready && now - s.requested > std::chrono::seconds(3)) {
        s.failure = "Capture timed out after three seconds"; return false;
    }
    bool updated = false;
    if (s.ready) {
        frame.width = s.width; frame.height = s.height;
        frame.rgba.resize(size_t(s.width) * s.height * 4);
        const bool bgr = s.format == WL_SHM_FORMAT_XBGR8888 || s.format == WL_SHM_FORMAT_ABGR8888;
        copyRgba(s.pixels, frame.rgba.data(), s.width, s.height, s.stride, bgr,
                 s.flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT);
        s.clearFrame();
        updated = true;
    }
    if (!s.pending && now >= s.next) {
        s.pending = zwlr_screencopy_manager_v1_capture_output(s.manager, 1, s.selected->proxy);
        zwlr_screencopy_frame_v1_add_listener(s.pending, &Impl::frameListener, &s);
        s.requested = now; s.next = now + std::chrono::milliseconds(s.interval);
        wl_display_flush(s.display);
    }
    return updated;
}
