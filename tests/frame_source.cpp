#include "frame_source.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

namespace {
// Records what the renderer asks of a source and answers with canned values.
struct Stub final : FrameSource {
    bool fresh=false; std::string failure="stub failure";
    unsigned fps=0, inFlight=0, demandWidth=0, demandHeight=0, serviced=0; double phase=-1; bool visible=false;
    bool update(CapturedFrame& frame) override { if (fresh) frame.width=frame.height=4; return fresh; }
    void service() override { ++serviced; }
    void setDemand(bool v,unsigned width,unsigned height) override { visible=v; demandWidth=width; demandHeight=height; }
    void setFrameRate(unsigned f,unsigned n=1,double p=0) override { fps=f; inFlight=n; phase=p; }
    const char* transport() const override { return "stub"; }
    unsigned requests() const override { return 7; }
    double importLatencyMs() const override { return -1; }
    const std::string& error() const override { return failure; }
};
}

int main() {
    Stub stub;
    // Output capture keeps one request in flight at phase zero unless a window source asks otherwise.
    stub.setFrameRate(30);
    assert(stub.fps==30 && stub.inFlight==1 && stub.phase==0);
    stub.setFrameRate(12, 3, .25);
    assert(stub.fps==12 && stub.inFlight==3 && stub.phase==.25);
    // Defaults: an output source may come back after a disconnect, and request timing is unknown.
    assert(stub.alive() && stub.requestToReadyMs()<0 && stub.importLatencyMs()<0);
    stub.setIgnoreDamage(true);
    // Backoff edges; the full ladder is asserted through DesktopCapture in tests/capture_plan.cpp.
    static_assert(FrameSource::nextRetryMs(499)==500 && FrameSource::nextRetryMs(3000)==5000);
    // The renderer drives every source through the base class.
    auto owned=std::make_unique<Stub>();
    Stub& raw=*owned;
    std::unique_ptr<FrameSource> source=std::move(owned);
    CapturedFrame frame;
    assert(!source->update(frame) && frame.width==0);
    raw.fresh=true;
    assert(source->update(frame) && frame.width==4);
    source->service(); source->service();
    source->setDemand(true, 1280, 720);
    source->setFrameRate(60);
    assert(raw.serviced==2 && raw.visible && raw.demandWidth==1280 && raw.demandHeight==720 && raw.fps==60 && raw.inFlight==1);
    assert(source->error()=="stub failure" && std::string(source->transport())=="stub" && source->requests()==7);
    raw.failure.clear();
    assert(source->error().empty());
    std::cout << "Frame source: default rate arguments, liveness, timing defaults, retry edges and virtual dispatch passed\n";
}
