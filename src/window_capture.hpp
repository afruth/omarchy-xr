#pragma once
#include "capture_cadence.hpp"
#include "frame_source.hpp"
#include <cstdint>
#include <memory>
#include <string>

struct gbm_device;
class WindowCapture;

// Every window capture of the canvas (docs/infinite-canvas-plan.md §3.2, §4.1): one Wayland
// connection with hyprland_toplevel_export_v1 v2, and one GBM device on the EGL display's render
// node shared by all lanes. All methods run on the render thread with the GL context current.
class WindowCaptureHub {
public:
    WindowCaptureHub();
    ~WindowCaptureHub();    // detaches open captures (their proxies go before the display)
    WindowCaptureHub(const WindowCaptureHub&)=delete;
    WindowCaptureHub& operator=(const WindowCaptureHub&)=delete;
    bool connect(std::string& error);
    void pump();            // nonblocking dispatch and flush; sets error() when the connection is lost
    // Dispatch until the compositor answered every request sent so far (buffer_done submits the copy
    // at once) or maxSeconds pass; a render loop that blocks in swap would otherwise add a frame.
    void settle(double maxSeconds);
    gbm_device* device();   // opened on first use; null without a current EGL display
    std::unique_ptr<WindowCapture> open(std::uint64_t address);
    const std::string& error() const;
    unsigned version() const;
    double now() const;     // seconds since construction, the cadence clock shared by every window
private:
    friend class WindowCapture;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// One Hyprland window by address. Keeps a request outstanding on each lane while visible (the
// focused window runs two lanes, staggered by one period) and imports frames with a new
// presentation stamp only; idle windows keep a <= 256 px thumbnail, release full-size slots and keep
// one frame open without copy so the export session stays alive.
class WindowCapture final : public FrameSource {
public:
    ~WindowCapture() override;
    WindowCapture(const WindowCapture&)=delete;
    WindowCapture& operator=(const WindowCapture&)=delete;
    bool update(CapturedFrame& frame) override;
    void service() override;
    void setDemand(bool visible,unsigned width,unsigned height) override;
    void setFrameRate(unsigned fps,unsigned inFlight=1,double phase=0) override;
    void setIgnoreDamage(bool enabled) override;
    const char* transport() const override;
    unsigned requests() const override;
    double importLatencyMs() const override;
    double requestToReadyMs() const override;
    const std::string& error() const override;
    bool alive() const override;
    double phase() const;
    unsigned lanes() const;
    unsigned frames() const;           // imported frames, duplicates excluded
    unsigned failures() const;         // failed or expired requests
private:
    friend class WindowCaptureHub;
    struct Impl;
    std::unique_ptr<Impl> impl;
    explicit WindowCapture(std::unique_ptr<Impl> state);
};
