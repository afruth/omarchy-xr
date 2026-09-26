#pragma once
#include "frame_source.hpp"
#include <array>
#include <cstddef>
#include <utility>

// The region source's two screencopy lanes (RegionCapture): one request per period across the lanes,
// strictly in turn. A lane whose answer is late holds its turn, so the lanes never fall into the same
// output frame. Answers are ordered by their request time: when both lanes answer in one tick the newer
// one is shown, and an answer older than the shown frame is dropped (a re-blit of the lane's retained
// image counts as its latest request). Lane: hold(bool), update(CapturedFrame&) (answers the lane's
// previous request, then may request again) and requests().
template<class Lane> struct RegionTurns {
    static constexpr double never=-1e9;
    double period=1000./60, lastRequest=never, shownAt=never;   // milliseconds
    std::array<double,2> askedAt{never, never};
    std::array<CapturedFrame,2> scratch;   // per-lane buffers, swapped with the shown frame
    size_t turn=0, last=0;
    bool step(const std::array<Lane*,2>& lanes, CapturedFrame& frame, double now) {
        bool updated=false, due=now-lastRequest>=period-1;
        for(size_t i=0;i<lanes.size();++i) {
            auto& lane=*lanes[i];
            const unsigned before=lane.requests(); const double answers=askedAt[i];
            lane.hold(!due || i!=turn);
            if(lane.update(scratch[i]) && answers>=shownAt) { std::swap(frame, scratch[i]); shownAt=answers; last=i; updated=true; }
            if(lane.requests()!=before) { askedAt[i]=lastRequest=now; turn=(turn+1)%lanes.size(); due=false; }
        }
        return updated;
    }
};
