#pragma once
#include "targeting.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// The sky is a full-screen draw behind everything. When one opaque panel covers the whole eye
// it is wasted, so the eye skips it. The test works on the same tessellated grid the renderer
// draws, flat, cylindrical or spherical. A connected patch can only leave part of the viewport
// uncovered where its boundary passes through, so if no boundary edge touches the viewport
// square, the square is either entirely covered or entirely clear, and one point decides which.
// Quads are clipped against the near plane first; the cut is part of the boundary too, which is
// what lets a panel wrapping around the head qualify.
namespace occlusion {
using targeting::Vec;
struct Screen { double x, y; };
constexpr float nearPlane=.1f;

inline bool segmentTouchesSquare(Screen a, Screen b) {
    // Liang-Barsky against [-1,1]^2; touching the border counts.
    double t0=0, t1=1;
    const double dx=b.x-a.x, dy=b.y-a.y;
    const double p[4]={-dx, dx, -dy, dy}, q[4]={a.x+1, 1-a.x, a.y+1, 1-a.y};
    for (int i=0; i<4; ++i) {
        if (p[i]==0) { if (q[i]<0) return false; continue; }
        const double t=q[i]/p[i];
        if (p[i]<0) t0=std::max(t0, t); else t1=std::min(t1, t);
        if (t0>t1) return false;
    }
    return true;
}
inline bool insideConvex(Screen p, const Screen* poly, int n) {
    int sign=0;
    for (int i=0; i<n; ++i) {
        const Screen u=poly[i], v=poly[(i+1)%n];
        const double cross=(v.x-u.x)*(p.y-u.y)-(v.y-u.y)*(p.x-u.x);
        if (cross==0) continue;
        const int s=cross>0?1:-1;
        if (sign==0) sign=s; else if (s!=sign) return false;
    }
    return true;
}

struct ClippedQuad {
    // Up to five vertices after clipping one convex quad against a plane. edge[k] is the
    // original quad edge that the segment from vertex k to k+1 lies on, or -1 for the cut.
    std::array<Screen,5> screen{}; std::array<int,5> edge{}; int count=0;
};
// corners in eye space, in loop order; boundary[k] says whether quad edge k (corner k to k+1)
// is on the mesh boundary. Returns false when nothing is in front of the near plane.
inline bool clipQuad(const std::array<Vec,4>& corners, float tanH, float tanV, ClippedQuad& out) {
    out.count=0;
    auto inside=[](const Vec& v){ return v.z<-nearPlane; };
    auto project=[&](const Vec& v){ return Screen{v.x/(-v.z*tanH), v.y/(-v.z*tanV)}; };
    auto push=[&](Screen s, int edge){ out.screen[out.count]=s; out.edge[out.count]=edge; ++out.count; };
    for (int k=0; k<4; ++k) {
        const Vec a=corners[k], b=corners[(k+1)%4];
        const bool ia=inside(a), ib=inside(b);
        if (ia) push(project(a), k);
        if (ia!=ib) {
            const float t=(-nearPlane-a.z)/(b.z-a.z);
            const Vec c{a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, -nearPlane};
            // Leaving the front half-space: the next segment is the cut, entering: it continues edge k.
            push(project(c), ia ? -1 : k);
        }
    }
    return out.count>=3;
}

// Does this panel's opaque surface cover the whole eye? eyeOffset is the stereo eye's x shift;
// tanH and tanV are the tangents of the half fields of view.
inline bool panelCoversEye(const PanelLayout& p, const spatial::Pose& pose, tracking::Quaternion view, Vec pan, float eyeOffset, float tanH, float tanV) {
    const float w=p.width/900, h=p.height/900;
    const int segments=spatial::surfaceSegments(w, pose.surfaceBend), rows=spatial::verticalSegments(h, pose);
    const int columns=segments+1;
    std::vector<Vec> grid(size_t(columns)*(rows+1));
    for (int j=0; j<=rows; ++j) for (int i=0; i<=segments; ++i)
        grid[size_t(j)*columns+i]=targeting::sub(targeting::rotate(view, targeting::add(spatial::vertex(pose, -w/2+w*i/segments, -h/2+h*j/rows), pan)), {eyeOffset, 0, 0});
    auto at=[&](int i, int j){ return grid[size_t(j)*columns+i]; };
    std::vector<ClippedQuad> quads; quads.reserve(size_t(segments)*rows);
    double minX=1e9, maxX=-1e9, minY=1e9, maxY=-1e9;
    for (int j=0; j<rows; ++j) for (int i=0; i<segments; ++i) {
        // Loop order bottom-left, bottom-right, top-right, top-left: edges bottom, right, top, left.
        const std::array<Vec,4> corners{at(i, j), at(i+1, j), at(i+1, j+1), at(i, j+1)};
        const std::array<bool,4> boundary{j==0, i==segments-1, j==rows-1, i==0};
        ClippedQuad q;
        if (!clipQuad(corners, tanH, tanV, q)) continue;
        for (int k=0; k<q.count; ++k) {
            const Screen s=q.screen[k];
            minX=std::min(minX, s.x); maxX=std::max(maxX, s.x); minY=std::min(minY, s.y); maxY=std::max(maxY, s.y);
            const int e=q.edge[k];
            if (e<0 || boundary[e]) if (segmentTouchesSquare(s, q.screen[(k+1)%q.count])) return false;
        }
        quads.push_back(q);
    }
    if (quads.empty() || minX>-1 || maxX<1 || minY>-1 || maxY<1) return false;   // nothing drawn, or the outline does not span the viewport
    // No boundary crosses the viewport, so one covered point means the whole viewport is covered.
    const Screen centre{0, 0};
    for (const auto& q:quads) if (insideConvex(centre, q.screen.data(), q.count)) return true;
    return false;
}
}
