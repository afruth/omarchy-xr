#pragma once
#include "frame_source.hpp"
#include "region_turns.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>

// Captures one output. Owns a separate Wayland connection; all methods run on the render thread.
class DesktopCapture final : public FrameSource {
public:
    DesktopCapture();
    ~DesktopCapture() override;
    DesktopCapture(const DesktopCapture&) = delete;
    DesktopCapture& operator=(const DesktopCapture&) = delete;
    bool connect();
    void setFrameRate(unsigned fps,unsigned inFlight=1,double phase=0) override;
    void setDemand(bool visible,unsigned width,unsigned height) override;
    const char* transport() const override;
    unsigned requests() const override;
    std::vector<std::string> outputs() const;
    bool select(const std::string& name);
    // A rectangle of the output in output-local logical px (screencopy semantics); the buffer arrives
    // at the output scale. Every region frame is a full copy, so frames keep coming for static content.
    bool selectRegion(const std::string& output,int x,int y,int width,int height);
    bool region() const;
    // Nonblocking. Returns true only when a new frame is available.
    bool update(CapturedFrame& frame) override;
    // Drain/flush protocol while presentation waits; GL context must remain current.
    void service() override;
    // Dispatch until the compositor asked for the pending request's buffer and the copy went out, or
    // maxSeconds pass: a render loop that blocks in swap would otherwise add a frame per round trip.
    void settle(double maxSeconds);
    void setIncludeCursor(bool enabled);
    // Held: frames still arrive, but no new request goes out (RegionCapture takes turns).
    void hold(bool held);
    // Milliseconds from the compositor ready event to the finished import. Negative when no frame has been imported.
    double importLatencyMs() const override;
    // Milliseconds from the capture request to the compositor ready event. Negative before the first frame.
    double requestToReadyMs() const override;
    const std::string& error() const override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// The staged window's rectangle on the canvas output (the window canvas region source). Hyprland
// answers a screencopy a little over one output frame after the request, so one request in flight
// gives 30 Hz: two lanes on their own connections take turns, one request per period (RegionTurns).
class RegionCapture final : public FrameSource {
public:
    RegionCapture();
    ~RegionCapture() override;
    RegionCapture(const RegionCapture&) = delete;
    RegionCapture& operator=(const RegionCapture&) = delete;
    bool open(const std::string& output,int x,int y,int width,int height,unsigned fps);
    bool update(CapturedFrame& frame) override;
    void service() override;
    void settle(double maxSeconds);
    void setDemand(bool visible,unsigned width,unsigned height) override;
    void setFrameRate(unsigned fps,unsigned inFlight=1,double phase=0) override;
    const char* transport() const override;
    unsigned requests() const override;
    double importLatencyMs() const override;
    double requestToReadyMs() const override;
    const std::string& error() const override;
private:
    std::array<std::unique_ptr<DesktopCapture>,2> lanes;
    RegionTurns<DesktopCapture> turns;
};
