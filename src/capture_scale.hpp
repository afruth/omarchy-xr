#pragma once
#include <algorithm>
#include <utility>
#include <vector>

struct ScalePass { unsigned width = 0, height = 0; };

// The GPU copy of a native buffer for a demand (the projected size with 1.25 headroom, in a bucket):
// the native buffer halved k times, the largest k that keeps it at or above the demand without its
// headroom, so it never falls below screen density. Exact halvings are box filters; a copy at the
// demand itself ended in a bilinear blit by a ratio like 0.93 or 0.75, which smeared one-pixel glyph
// strokes before the scene sampled them (docs/architecture.md, "Text crispness").
inline std::pair<unsigned, unsigned> captureCopySize(unsigned nativeW, unsigned nativeH, unsigned demandW, unsigned demandH) {
    nativeW = std::max(1u, nativeW); nativeH = std::max(1u, nativeH);
    int k = 0;
    while (k < 16 && (nativeW >> (k + 1)) && (nativeH >> (k + 1)) &&
           (nativeW >> (k + 1)) * 1.25f >= demandW && (nativeH >> (k + 1)) * 1.25f >= demandH) ++k;
    return {nativeW >> k, nativeH >> k};
}

// Halve until the source is within 2× of the destination, then finish at the exact size.
inline std::vector<ScalePass> scalePasses(unsigned sourceWidth, unsigned sourceHeight, unsigned destWidth, unsigned destHeight) {
    destWidth = std::max(1u, destWidth);
    destHeight = std::max(1u, destHeight);
    std::vector<ScalePass> passes;
    unsigned width = std::max(1u, sourceWidth), height = std::max(1u, sourceHeight);
    while (width > destWidth * 2 || height > destHeight * 2) {
        width = std::max(destWidth, width / 2);
        height = std::max(destHeight, height / 2);
        passes.push_back({width, height});
    }
    if (passes.empty() || passes.back().width != destWidth || passes.back().height != destHeight)
        passes.push_back({destWidth, destHeight});
    return passes;
}

// -1 writes the panel texture. 0 and 1 alternate scratch images so a pass never reads the image it reallocates.
inline int scalePassScratch(unsigned index, unsigned count) {
    if (count == 0 || index + 1 >= count) return -1;
    return static_cast<int>(index % 2);
}

// Next compositor buffer. The completed image stays reserved, busy or not.
inline int captureAllocateSlot(int shownSlot, bool busy0, bool busy1) {
    for (int i = 0; i < 2; ++i) {
        if (i == shownSlot || (i == 0 ? busy0 : busy1)) continue;
        return i;
    }
    return -1;
}

// A finished copy publishes the capture slot. A rebake stays on the completed image.
inline int capturePresentSlot(int captureSlot, int shownSlot, bool rebake) {
    if (rebake) return shownSlot;
    return captureSlot >= 0 ? captureSlot : shownSlot;
}
