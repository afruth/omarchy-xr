#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct CapturedFrame {
    unsigned width = 0, height = 0;
    unsigned sourceWidth=0,sourceHeight=0,texture=0;
    std::vector<std::uint8_t> rgba;
};

// Owns a separate Wayland connection; all methods run on the render thread.
class DesktopCapture {
public:
    DesktopCapture();
    ~DesktopCapture();
    DesktopCapture(const DesktopCapture&) = delete;
    DesktopCapture& operator=(const DesktopCapture&) = delete;
    bool connect();
    void setFrameRate(unsigned fps);
    void setDemand(bool visible,unsigned width,unsigned height);
    const char* transport() const;
    unsigned requests() const;
    std::vector<std::string> outputs() const;
    bool select(const std::string& name);
    // Nonblocking. Returns true only when a new frame is available.
    bool update(CapturedFrame& frame);
    // Drain/flush protocol while presentation waits; GL context must remain current.
    void service();
    void setIncludeCursor(bool enabled);
    const std::string& error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
