#pragma once
#include <algorithm>
#include <cmath>

namespace spatial {
constexpr float pi = 3.14159265358979323846f;
struct Point { float x, y, z; };
struct Pose { Point center; float yaw; float surfaceBend; };

// Curvature is inverse radius. Stable limits avoid cancellation near zero.
inline float bendX(float x, float k) {
    const float angle = x*k;
    return std::abs(angle)<1e-4f ? x : std::sin(angle)/k;
}
inline float bendZ(float x, float k) {
    const float half = x*k/2;
    return k == 0 ? 0 : 2*std::sin(half)*std::sin(half)/k;
}
// Horizontal cylindrical arrangement, referenced to the neutral camera position.
// 100% uses the camera distance as radius, unless the sweep limit requires more.
inline Pose pose(float x, float y, float width, float span, float distance,
                 float workspacePercent, float surfacePercent, Point eye = {0,0,0}) {
    const float radius = std::max(distance, span/(5*pi/3)); // <= 300 degrees
    const float k = workspacePercent/100/radius;
    const Point center{eye.x+bendX(x,k), eye.y+y, eye.z-distance+bendZ(x,k)};
    const float range = std::sqrt(std::pow(center.x-eye.x,2)+std::pow(center.y-eye.y,2)+std::pow(center.z-eye.z,2));
    const float surfaceK = surfacePercent/100 * std::min(1/std::max(range,.1f), (8*pi/9)/width); // <= 160 degrees
    return {center, -x*k, surfaceK};
}
// Bent screen keeps its center/tangent and horizontal arc length. Borders,
// texture and placeholder artwork all use the same surface function.
inline Point vertex(const Pose& p, float x, float y, float offset = 0) {
    const float lx=bendX(x,p.surfaceBend), lz=bendZ(x,p.surfaceBend)+offset;
    const float c=std::cos(p.yaw),s=std::sin(p.yaw);
    return {p.center.x+c*lx+s*lz,p.center.y+y,p.center.z-s*lx+c*lz};
}
}
