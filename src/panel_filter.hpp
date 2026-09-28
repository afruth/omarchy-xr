#pragma once
#include <GL/gl.h>
#include <GL/glext.h>
#include "capture_scale.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

// How a captured panel texture is sized and sampled (docs/architecture.md, "Text crispness";
// measured by `make check-crispness`).
namespace panel {
// The GPU copy's size (capture_scale.hpp captureCopySize).
inline std::pair<unsigned, unsigned> captureSize(unsigned nativeW, unsigned nativeH, unsigned demandW, unsigned demandH) {
    return captureCopySize(nativeW, nativeH, demandW, demandH);
}
// One mip level: the copy is at most about 2.5x denser than the screen (a second level measured no
// better), and trilinear filtering keeps a moving head from shimmering. Leaves the texture bound.
inline void mipmaps() {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
}
// Trilinear filtering blends toward the half-resolution level as soon as the copy is denser than
// the screen, softer than the ideal footprint. A negative bias that grows with the minification m
// (copy px per screen px) keeps the full level longer: 0 up to m 1.2, -0.25 near 1.4, -0.5 from 2.
inline float lodBias(float minification) {
    if (!(minification>0)) return 0;
    return -std::clamp(.7f*(std::log2(minification)-.25f), 0.f, .5f);
}
}
