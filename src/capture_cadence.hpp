#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

// Request times for one window (docs/infinite-canvas-plan.md §4.4). Lane i of K fires at
// epoch + phase*period + i*period + n*K*period: one request per period overall, lanes staggered
// by one period, so two in flight give the full rate while each lane waits a commit plus an output
// frame. Times are seconds on the capture hub's clock; the shared epoch keeps equal-rate windows spread.
struct Cadence {
    double period=1./60;
    double phase=0;      // fraction of the period, [0,1)
    unsigned lanes=1;
    double epoch=0;
    static constexpr double never=-std::numeric_limits<double>::infinity();
    bool enabled() const { return period>0 && std::isfinite(period); }
    double base(unsigned lane) const { return epoch+phase*period+lane*period; }
    double lanePeriod() const { return lanes*period; }
    // First slot of the lane strictly after t; never in the past when t is now.
    double nextDue(unsigned lane,double t) const {
        if(!enabled())return std::numeric_limits<double>::infinity();
        const double n=std::floor((t-base(lane))/lanePeriod()+1e-6)+1;
        return base(lane)+n*lanePeriod();
    }
    // The lane's pending slot after the slot it last fired (never: -infinity). A slot up to one
    // period late still fires, so vblank jitter does not shift the cadence; a longer stall skips the
    // missed slots and fires once, never a backlog (the DesktopCapture rule).
    double due(unsigned lane,double now,double last) const { return nextDue(lane,std::max(last,now-period)); }
    // A rate change keeps the epoch, so windows that share a rate keep their spread.
    void retune(unsigned fps,unsigned inFlight,double newPhase){
        period=fps ? 1./fps : 0;
        lanes=std::clamp(inFlight,1u,2u);
        phase=newPhase-std::floor(newPhase);
    }
    static double spread(std::size_t index,std::size_t count){ return count ? double(index)/double(count) : 0; }
};
