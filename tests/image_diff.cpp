// Pixel-exact PNG comparison for the renderer baseline: image-diff BASELINE_DIR CANDIDATE_DIR NAME...
#include <cairo.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

// RGB PNGs load as RGB24 and RGBA ones as ARGB32; both are four bytes per pixel.
struct Image{cairo_surface_t* s=nullptr;int w=0,h=0,stride=0,channels=4;const unsigned char* px=nullptr;
    ~Image(){if(s)cairo_surface_destroy(s);}};

bool load(const std::filesystem::path& path,Image& image){
    image.s=cairo_image_surface_create_from_png(path.c_str());
    if(cairo_surface_status(image.s)!=CAIRO_STATUS_SUCCESS)return false;
    cairo_surface_flush(image.s);
    const cairo_format_t format=cairo_image_surface_get_format(image.s);
    if(format!=CAIRO_FORMAT_ARGB32&&format!=CAIRO_FORMAT_RGB24)return false;
    image.channels=format==CAIRO_FORMAT_ARGB32?4:3; // RGB24's high byte is unused
    image.w=cairo_image_surface_get_width(image.s);image.h=cairo_image_surface_get_height(image.s);
    image.stride=cairo_image_surface_get_stride(image.s);image.px=cairo_image_surface_get_data(image.s);
    return image.px!=nullptr;
}

// Dimensions and format first, then width*4 bytes per row since the two strides may differ.
bool differs(const Image& a,const Image& b,unsigned& pixels,unsigned& maxDelta){
    pixels=0;maxDelta=0;
    if(a.w!=b.w||a.h!=b.h||a.channels!=b.channels){pixels=unsigned(std::max(a.w*a.h,b.w*b.h));maxDelta=255;return true;}
    for(int y=0;y<a.h;++y){
        const unsigned char* ra=a.px+std::size_t(y)*a.stride;const unsigned char* rb=b.px+std::size_t(y)*b.stride;
        for(int x=0;x<a.w;++x){
            unsigned delta=0;
            for(int c=0;c<a.channels;++c)delta=std::max(delta,unsigned(std::abs(int(ra[x*4+c])-int(rb[x*4+c]))));
            if(delta){++pixels;maxDelta=std::max(maxDelta,delta);}
        }
    }
    return pixels!=0;
}

int main(int argc,char** argv){
    if(argc<4){std::cerr<<"Usage: image-diff BASELINE_DIR CANDIDATE_DIR NAME...\n";return 2;}
    const std::filesystem::path baseline=argv[1],candidate=argv[2];int status=0;
    for(int i=3;i<argc;++i){
        const std::string name=argv[i];Image a,b;
        if(!load(baseline/(name+".png"),a)||!load(candidate/(name+".png"),b)){
            std::cout<<name<<": cannot load\n";status=1;continue;}
        unsigned pixels,delta;
        if(!differs(a,b,pixels,delta)){std::cout<<name<<": identical\n";continue;}
        if(a.w!=b.w||a.h!=b.h||a.channels!=b.channels)
            std::cout<<name<<": size or format "<<a.w<<'x'<<a.h<<'/'<<a.channels<<" vs "<<b.w<<'x'<<b.h<<'/'<<b.channels<<'\n';
        else std::cout<<name<<": "<<pixels<<" pixels differ (max channel delta "<<delta<<")\n";
        status=1;
    }
    return status;
}
