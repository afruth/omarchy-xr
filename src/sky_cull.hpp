#pragma once
#include "targeting.hpp"
#include <array>
#include <cmath>
#include <utility>

// The sky is a full-screen draw behind everything. When one opaque panel covers the whole eye
// it is wasted, so the eye skips it. Only a flat panel qualifies: a curved edge bows towards the
// screen centre, so the quad through its corners would overstate what the surface covers.
namespace occlusion {
using targeting::Vec;

// corners: eye-space loop of a flat quad (camera at the origin looking down -z).
// tanH and tanV: tangents of the horizontal and vertical half fields of view.
inline bool quadCoversViewport(const std::array<Vec,4>& corners, float tanH, float tanV) {
    std::array<std::pair<double,double>,4> ndc;
    for (int i=0; i<4; ++i) {
        if (!(corners[i].z<-.1f)) return false;   // behind or on the near plane: no reliable projection
        ndc[i]={corners[i].x/(-corners[i].z*tanH), corners[i].y/(-corners[i].z*tanV)};
    }
    auto inside=[&](double px, double py) {
        int sign=0;
        for (int i=0; i<4; ++i) {
            const auto [ax, ay]=ndc[i]; const auto [bx, by]=ndc[(i+1)%4];
            const double cross=(bx-ax)*(py-ay)-(by-ay)*(px-ax);
            if (std::abs(cross)<1e-12) return false;   // on an edge counts as uncovered
            const int s=cross>0?1:-1;
            if (sign==0) sign=s; else if (s!=sign) return false;
        }
        return true;
    };
    for (double px:{-1.,1.}) for (double py:{-1.,1.}) if (!inside(px, py)) return false;
    return true;
}

// Does this panel's opaque rectangle cover the whole eye? eyeOffset is the stereo eye's x shift.
inline bool panelCoversEye(const PanelLayout& p, const spatial::Pose& pose, tracking::Quaternion view, Vec pan, float eyeOffset, float tanH, float tanV) {
    if (pose.surfaceBend!=0 || pose.spherical) return false;
    const float w=p.width/900, h=p.height/900;
    std::array<Vec,4> corners; int i=0;
    for (float y:{-h/2, h/2}) for (float x:{-w/2, w/2})
        corners[i++]=targeting::sub(targeting::rotate(view, targeting::add(spatial::vertex(pose, x, y), pan)), {eyeOffset, 0, 0});
    std::swap(corners[2], corners[3]);   // loop order: (-,-) (+,-) (+,+) (-,+)
    return quadCoversViewport(corners, tanH, tanV);
}
}
