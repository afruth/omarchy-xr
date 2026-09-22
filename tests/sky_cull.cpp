#include "sky_cull.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>

int main() {
    using occlusion::Screen;
    const float tanV=std::tan(28*spatial::pi/360), tanH=tanV*16/9;
    // Segment versus the viewport square: crossing, touching a border, and passing clear of it.
    assert(occlusion::segmentTouchesSquare({-2,0},{2,0}));
    assert(occlusion::segmentTouchesSquare({-2,-1},{2,-1}));
    assert(!occlusion::segmentTouchesSquare({-2,1.01},{2,1.01}));
    assert(!occlusion::segmentTouchesSquare({-2,-2},{-1.5,2}));
    assert(occlusion::segmentTouchesSquare({-2,-2},{2,2}));

    // A flat 3840x2160 panel straight ahead at distance 5 covers a 28 degree eye once it is close enough.
    PanelLayout p{"a",0,0,3840,2160};
    spatial::Workspace flat;
    const auto pose=spatial::pose(0,0,p.width/900,p.width/900,5,flat,0);
    assert(!occlusion::panelCoversEye(p,pose,{},{0,0,0},0,tanH,tanV));            // from the overview it does not
    assert(occlusion::panelCoversEye(p,pose,{},{0,0,4},0,tanH,tanV));             // panned 4 units closer it does
    assert(!occlusion::panelCoversEye(p,pose,{},{2.5f,0,4},0,tanH,tanV));         // panned sideways it does not
    assert(occlusion::panelCoversEye(p,pose,{},{0,0,4},.032f,tanH,tanV));         // either stereo eye
    assert(!occlusion::panelCoversEye(p,pose,{},{0,0,5.5f},0,tanH,tanV));         // camera past the panel

    // A curved panel is culled exactly like a flat one when it fills the view.
    const auto curved=spatial::pose(0,0,p.width/900,p.width/900,5,flat,60);
    assert(curved.surfaceBend>0);
    assert(!occlusion::panelCoversEye(p,curved,{},{0,0,0},0,tanH,tanV));
    assert(occlusion::panelCoversEye(p,curved,{},{0,0,4},0,tanH,tanV));
    assert(!occlusion::panelCoversEye(p,curved,{},{2.5f,0,4},0,tanH,tanV));
    // Looking up at the top edge of the curved panel exposes sky above it: the edge crosses the viewport.
    const auto up=tracking::conjugate(tracking::orientation(0,-40,0));
    assert(!occlusion::panelCoversEye(p,curved,up,{0,0,4},0,tanH,tanV));
    // A 160 degree panel wrapping the viewer: its ends sit beside the head, behind the near plane,
    // yet the part in front covers the eye. Looking towards its rim exposes sky again.
    const auto wrap=spatial::pose(0,0,p.width/900,p.width/900,1.2f,flat,100);
    assert(wrap.surfaceBend*p.width/900>2.7f);
    assert(occlusion::panelCoversEye(p,wrap,{},{0,0,0},0,tanH,tanV));
    const auto aside=tracking::conjugate(tracking::orientation(0,0,85));
    assert(!occlusion::panelCoversEye(p,wrap,aside,{0,0,0},0,tanH,tanV));
    // Clipping: a quad straddling the near plane keeps its front part, and one entirely behind is dropped.
    occlusion::ClippedQuad q;
    assert(occlusion::clipQuad({targeting::Vec{-1,-1,-2},{1,-1,-2},{1,1,.5f},{-1,1,.5f}},tanH,tanV,q) && q.count==4);
    assert(std::count(q.edge.begin(),q.edge.begin()+q.count,-1)==1);
    assert(!occlusion::clipQuad({targeting::Vec{-1,-1,1},{1,-1,1},{1,1,2},{-1,1,2}},tanH,tanV,q));

    // A workspace-following surface (bend taken from the shared cylinder) is handled the same way.
    spatial::Workspace follow; follow.follow=true; follow.degrees=180;
    const auto sphere=spatial::pose(0,0,p.width/900,p.width/900*2,5,follow,0);
    assert(sphere.surfaceBend>0);
    assert(occlusion::panelCoversEye(p,sphere,{},{0,0,4},0,tanH,tanV));
    assert(!occlusion::panelCoversEye(p,sphere,{},{0,0,0},0,tanH,tanV));
    std::cout << "Sky culling: viewport coverage for flat, curved, wrapped and following panels, edges and stereo eyes passed\n";
}
