#include "pixels.hpp"
#include <array>
#include <iostream>

int main() {
    // Two pixels per row plus padding; source alpha must not affect opacity.
    const std::array<std::uint32_t, 6> source{
        0x00112233, 0x77445566, 0xdeadbeef,
        0xff778899, 0x00aabbcc, 0xdeadbeef};
    const std::array<std::uint8_t, 16> expected{
        0x11,0x22,0x33,255, 0x44,0x55,0x66,255,
        0x77,0x88,0x99,255, 0xaa,0xbb,0xcc,255};
    for (bool blueHigh : {false, true}) for (bool inverted : {false, true}) {
        std::array<std::uint8_t, 16> actual{};
        copyRgba(source.data(), actual.data(), 2, 2, 12, blueHigh, inverted);
        for (unsigned y = 0; y < 2; ++y) for (unsigned x = 0; x < 2; ++x)
            for (unsigned c = 0; c < 4; ++c) {
                const unsigned row = inverted ? 1 - y : y;
                const unsigned channel = blueHigh && c != 1 && c != 3 ? 2 - c : c;
                if (actual[(y * 2 + x) * 4 + c] != expected[(row * 2 + x) * 4 + channel]) {
                    std::cerr << "Pixel conversion failed: channel order, padding, or inversion\n";
                    return 1;
                }
            }
    }
    std::cout << "Pixel conversion: channel order, padding, inversion, opacity passed\n";
}
