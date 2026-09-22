#include "sky_cull.hpp"
#include <cassert>
#include <iostream>

int main() {
    using targeting::Vec;
    const float tanV=std::tan(28*spatial::pi/360), tanH=tanV*16/9;
    auto quad=[](float x0,float x1,float y0,float y1,float z){ return std::array<Vec,4>{Vec{x0,y0,z},Vec{x1,y0,z},Vec{x1,y1,z},Vec{x0,y1,z}}; };
    // At 1 unit the eye sees +-tanV vertically and +-tanH horizontally.
    assert(occlusion::quadCoversViewport(quad(-1,1,-.5f,.5f,-1),tanH,tanV));
    assert(!occlusion::quadCoversViewport(quad(-1,1,-.2f,.5f,-1),tanH,tanV));      // bottom edge visible
    assert(!occlusion::quadCoversViewport(quad(-.3f,1,-.5f,.5f,-1),tanH,tanV));    // left edge visible
    assert(!occlusion::quadCoversViewport(quad(-1,1,-.5f,.5f,.5f),tanH,tanV));     // behind the camera
    assert(!occlusion::quadCoversViewport({Vec{-1,-.5f,-1},Vec{1,-.5f,-1},Vec{1,.5f,-1},Vec{-1,.5f,.2f}},tanH,tanV)); // one corner crosses the near plane
    // A flat panel straight ahead: 3840x2160 at distance 5 covers a 28 degree eye once it is close enough.
    PanelLayout p{"a",0,0,3840,2160};
    spatial::Workspace flat;
    const auto pose=spatial::pose(0,0,p.width/900,p.width/900,5,flat,0);
    assert(!occlusion::panelCoversEye(p,pose,{},{0,0,0},0,tanH,tanV));            // from the overview it does not
    assert(occlusion::panelCoversEye(p,pose,{},{0,0,4},0,tanH,tanV));             // panned 4 units closer it does
    assert(!occlusion::panelCoversEye(p,pose,{},{2.5f,0,4},0,tanH,tanV));         // panned sideways it does not
    assert(occlusion::panelCoversEye(p,pose,{},{0,0,4},.032f,tanH,tanV));         // either stereo eye
    // Curved surfaces are never culled, whatever they cover.
    const auto curved=spatial::pose(0,0,p.width/900,p.width/900,5,flat,60);
    assert(curved.surfaceBend>0 && !occlusion::panelCoversEye(p,curved,{},{0,0,4},0,tanH,tanV));
    std::cout << "Sky culling: viewport coverage, near plane, stereo eyes and curved exclusion passed\n";
}
