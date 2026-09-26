#pragma once
#include "layout.hpp"
#include "curvature.hpp"
#include <string>
#include <string_view>

// The shared cylinder every surface bends onto: layout centre, arc span (world units), radius and wrap.
struct Cylinder {
    float cx=0, cy=0, span=0, distance=5; spatial::Workspace workspace;
    spatial::Pose pose(const PanelLayout& p) const { return spatial::pose((p.x+p.width/2-cx)/900, -(p.y+p.height/2-cy)/900, p.width/900, span, distance, workspace, p.curvature); }
};
// Per-surface draw data in either scene mode (a monitor panel today, a canvas window later). Read-only.
struct SurfaceView {
    const PanelLayout* layout=nullptr; unsigned texture=0, width=0, height=0, sourceWidth=0, sourceHeight=0;
    const std::string* status=nullptr; float halo=0; bool visible=true; float alpha=1; std::string_view label;
};
