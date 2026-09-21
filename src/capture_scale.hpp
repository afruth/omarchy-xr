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
