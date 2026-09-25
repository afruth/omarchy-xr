#pragma once
#include "frame_source.hpp"
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
    // Nonblocking. Returns true only when a new frame is available.
    bool update(CapturedFrame& frame) override;
    // Drain/flush protocol while presentation waits; GL context must remain current.
    void service() override;
    void setIncludeCursor(bool enabled);
    // Milliseconds from the compositor ready event to the finished import. Negative when no frame has been imported.
    double importLatencyMs() const override;
    // Milliseconds from the capture request to the compositor ready event. Negative before the first frame.
    double requestToReadyMs() const override;
    const std::string& error() const override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
