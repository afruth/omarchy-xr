#pragma once
#include <algorithm>
#include <vector>

struct ScalePass { unsigned width = 0, height = 0; };

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
