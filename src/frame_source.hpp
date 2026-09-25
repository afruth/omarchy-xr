#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct CapturedFrame {
    unsigned width = 0, height = 0;
    unsigned sourceWidth=0,sourceHeight=0,texture=0;
    std::vector<std::uint8_t> rgba;
};

// One captured surface: a monitor today, a window in canvas mode. All methods run on the render thread.
class FrameSource {
public:
    virtual ~FrameSource()=default;
    virtual bool update(CapturedFrame& frame)=0;            // nonblocking; true only when a new frame is available
    virtual void service()=0;                                // drain/flush protocol while presentation waits
    virtual void setDemand(bool visible,unsigned width,unsigned height)=0;
    // inFlight/phase serve per-window sources; output capture keeps one request in flight.
    virtual void setFrameRate(unsigned fps,unsigned inFlight=1,double phase=0)=0;
    virtual void setIgnoreDamage(bool) {}
    virtual const char* transport() const=0;
    virtual unsigned requests() const=0;
    virtual double importLatencyMs() const=0;                // ready -> imported; negative before the first frame
    virtual double requestToReadyMs() const { return -1; }   // request -> compositor ready; negative when unknown
    virtual const std::string& error() const=0;
    // False once the surface is gone for good (a closed window); a disconnected output may come back, so true here.
    virtual bool alive() const { return true; }
    // Backoff for a transient failure: 0.5 s, then doubling, capped at 5 s.
    static constexpr int nextRetryMs(int current) { return current<500 ? 500 : (current*2>5000 ? 5000 : current*2); }
};
