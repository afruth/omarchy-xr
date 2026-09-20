#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct CapturedFrame {
    unsigned width = 0, height = 0;
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
    std::vector<std::string> outputs() const;
    bool select(const std::string& name);
    // Nonblocking. Returns true only when a new frame is available.
    bool update(CapturedFrame& frame);
    const std::string& error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
