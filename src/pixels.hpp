#pragma once
#include <algorithm>
#include <initializer_list>
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

// Four-tap footprint sampling for the portable SHM fallback. Work and upload
// scale with the requested texture, not with the native desktop dimensions.
inline void copyRgbaScaled(const void* source,std::uint8_t* destination,unsigned width,unsigned height,
                           unsigned stride,bool blueHigh,bool inverted,unsigned outWidth,unsigned outHeight){
    if(width==outWidth && height==outHeight){copyRgba(source,destination,width,height,stride,blueHigh,inverted);return;}
    for(unsigned y=0;y<outHeight;++y)for(unsigned x=0;x<outWidth;++x){
        unsigned sums[3]{};
        for(float oy:{.25f,.75f})for(float ox:{.25f,.75f}){
            unsigned sx=std::min(width-1,unsigned((x+ox)*width/outWidth)),sy=std::min(height-1,unsigned((y+oy)*height/outHeight));
            if(inverted)sy=height-1-sy;
            std::uint32_t pixel;std::memcpy(&pixel,static_cast<const std::uint8_t*>(source)+size_t(sy)*stride+sx*4,4);
            sums[0]+=(pixel>>(blueHigh?0:16))&255;sums[1]+=(pixel>>8)&255;sums[2]+=(pixel>>(blueHigh?16:0))&255;
        }
        auto* dst=destination+(size_t(y)*outWidth+x)*4;
        for(int c=0;c<3;++c)dst[c]=sums[c]/4;
        dst[3]=255;
    }
}
