#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Convert native-endian wl_shm 8888 words to opaque, top-down RGBA.
// Input may include row padding and inverted rows; destination is tightly packed.
inline void copyRgba(const void* source, std::uint8_t* destination,
                     unsigned width, unsigned height, unsigned stride,
                     bool blueHigh, bool inverted) {
    for (unsigned y = 0; y < height; ++y) {
        const unsigned sy = inverted ? height - y - 1 : y;
        const auto* row = static_cast<const std::uint8_t*>(source) + std::size_t(sy) * stride;
        auto* dst = destination + std::size_t(y) * width * 4;
        for (unsigned x = 0; x < width; ++x) {
            std::uint32_t pixel;
            std::memcpy(&pixel, row + x * 4, 4);
            dst[4*x] = (pixel >> (blueHigh ? 0 : 16)) & 255;
            dst[4*x+1] = (pixel >> 8) & 255;
            dst[4*x+2] = (pixel >> (blueHigh ? 16 : 0)) & 255;
            dst[4*x+3] = 255;
        }
    }
}
