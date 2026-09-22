#include "environment.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <numeric>
#include <unistd.h>
#include <vector>

namespace {
constexpr int width=256,height=192;
constexpr float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
using Image=std::vector<unsigned char>;

struct Fixture {
    SDL_Window* window=nullptr;
    SDL_GLContext context=nullptr;
    std::string folder,config,bitmap;
    double now=0;
    Fixture() {
        assert(SDL_Init(SDL_INIT_VIDEO)==0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
        window=SDL_CreateWindow("Tron regression",0,0,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        assert(window);context=SDL_GL_CreateContext(window);assert(context);
        char directory[]="/tmp/xr-tron-test-XXXXXX";assert(mkdtemp(directory));
        folder=directory;config=folder+"/environment.tsv";bitmap=folder+"/panorama.bmp";
        auto* surface=SDL_CreateRGBSurfaceWithFormat(0,64,32,24,SDL_PIXELFORMAT_RGB24);
        assert(surface);SDL_FillRect(surface,nullptr,SDL_MapRGB(surface->format,0,255,0));
        assert(SDL_SaveBMP(surface,bitmap.c_str())==0);SDL_FreeSurface(surface);
        std::cout<<"Tron GL: "<<glGetString(GL_RENDERER)<<'\n';
    }
    ~Fixture() {
        SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
        std::filesystem::remove_all(folder);
    }
    void settings(SkyEnvironment& sky,const std::string& line) {
        {std::ofstream file(config+".tmp");file<<line<<'\n';}
        std::filesystem::rename(config+".tmp",config);sky.update(now+=1);
    }
    void waitForImage(SkyEnvironment& sky) {
        for(int i=0;sky.loadingImage() && i<500;++i){SDL_Delay(2);sky.update(now+=.3);}
        assert(!sky.loadingImage() && sky.error.empty());
    }
};

Image render(SkyEnvironment& sky,const theme::Rgb& accent,bool flipped=false,float eye=0) {
    glViewport(0,0,width,height);glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glFrustum(-.1,.1,flipped?.075:-.075,flipped?-.075:.075,.1,1000);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(13,17,23);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
    sky.draw(identity,accent,eye);
    assert(glIsEnabled(GL_DEPTH_TEST));
    GLboolean depth;glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);assert(depth);
    std::array<float,16> matrix{};glGetFloatv(GL_MODELVIEW_MATRIX,matrix.data());
    assert(matrix[12]==13 && matrix[13]==17 && matrix[14]==23);
    Image pixels(width*height*3);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    assert(glGetError()==GL_NO_ERROR);return pixels;
}

unsigned long channelSum(const Image& image,int channel) {
    unsigned long result=0;
    for(size_t i=size_t(channel);i<image.size();i+=3)result+=image[i];
    return result;
}

void testColorAndProjection(Fixture& f,SkyEnvironment& sky) {
    f.settings(sky,"100 0 \"builtin:tron\" 0");
    assert(sky.visible() && !sky.loadingImage());
    const auto red=render(sky,{1,0,0}),blue=render(sky,{0,0,1});
    assert(sky.error.empty()); // Includes shader compilation/linking on the real GL driver.
    assert(channelSum(red,0)>1000 && channelSum(red,1)==0 && channelSum(red,2)==0);
    assert(channelSum(blue,2)==channelSum(red,0) && channelSum(blue,0)==0);
    unsigned long floor=0;
    for(int y=0;y<height/3;++y)for(int x=0;x<width;++x)floor+=red[(y*width+x)*3];
    assert(floor>1000); // The shader grid renders, not just the fixed-function skyline.
    const auto flipped=render(sky,{1,0,0},true);
    unsigned long difference=0;
    for(int y=0;y<height;++y)for(int x=0;x<width*3;++x)
        difference+=std::abs(int(red[y*width*3+x])-int(flipped[(height-1-y)*width*3+x]));
    assert(difference<red.size()/5);
    f.settings(sky,"50 0 \"builtin:tron\" 0");
    const auto dim=render(sky,{1,0,0});
    const double ratio=double(channelSum(dim,0))/channelSum(red,0);
    assert(ratio>.40 && ratio<.60 && !sky.loadingImage());
    f.settings(sky,"0 0 \"builtin:tron\" 0");
    assert(!sky.visible() && channelSum(render(sky,{1,1,1}),0)==0);
}

void testAnimation(Fixture& f,SkyEnvironment& sky) {
    f.settings(sky,"100 0 \"builtin:tron\" 0");
    const auto still=render(sky,{.2f,.7f,1});
    sky.update(f.now+=31);
    assert(render(sky,{.2f,.7f,1})==still && !sky.loadingImage());
    f.settings(sky,"100 0 \"builtin:tron\" 1");
    f.now=std::ceil(f.now/24)*24+6;sky.update(f.now);
    const auto bright=render(sky,{.2f,.7f,1});
    sky.update(f.now+=12);const auto dark=render(sky,{.2f,.7f,1});
    assert(channelSum(bright,2)>channelSum(dark,2));
    unsigned long difference=0;
    for(size_t i=0;i<bright.size();++i){
        const int delta=std::abs(int(bright[i])-int(dark[i]));
        assert(delta<=double(bright[i])*.09+2);difference+=delta;
    }
    assert(difference>0 && difference<double(channelSum(bright,2))*.1);
    assert(!sky.loadingImage() && sky.error.empty());
}

void testInvalidSettings(Fixture& f,SkyEnvironment& sky) {
    f.settings(sky,"100 0 \"builtin:tron\" 0");
    const auto previous=render(sky,{.2f,.7f,1});
    for(const char* suffix:{"", "2", "-1", "true", "0 trailing", "0.5"}){
        f.settings(sky,std::string("20 90 \"builtin:tron\" ")+suffix);
        assert(!sky.error.empty() && sky.visible() && !sky.loadingImage());
        assert(render(sky,{.2f,.7f,1})==previous);
    }
    f.settings(sky,"100 0 \"builtin:tron\" 0");
    assert(sky.error.empty());
}

GLuint callerProgram() {
    const char* vertex="#version 120\nvoid main(){gl_Position=ftransform();}";
    const char* fragment="#version 120\nvoid main(){gl_FragColor=vec4(0.0);}";
    const GLuint vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs,1,&vertex,nullptr);glCompileShader(vs);
    glShaderSource(fs,1,&fragment,nullptr);glCompileShader(fs);
    const GLuint program=glCreateProgram();
    glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(linked);
    glDeleteShader(vs);glDeleteShader(fs);return program;
}

struct GlState {
    std::array<GLboolean,5> flags{};
    std::array<GLint,8> values{};
    std::array<GLfloat,40> floats{};
    GlState() {
        constexpr GLenum enables[]={GL_DEPTH_TEST,GL_CULL_FACE,GL_BLEND,GL_TEXTURE_2D};
        for(size_t i=0;i<4;++i)flags[i]=glIsEnabled(enables[i]);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&flags[4]);
        constexpr GLenum integers[]={GL_CURRENT_PROGRAM,GL_TEXTURE_BINDING_2D,GL_BLEND_SRC_RGB,
            GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_BLEND_EQUATION_RGB,GL_MATRIX_MODE};
        for(size_t i=0;i<8;++i)glGetIntegerv(integers[i],&values[i]);
        glGetFloatv(GL_BLEND_COLOR,floats.data());glGetFloatv(GL_CURRENT_COLOR,floats.data()+4);
        glGetFloatv(GL_MODELVIEW_MATRIX,floats.data()+8);glGetFloatv(GL_PROJECTION_MATRIX,floats.data()+24);
    }
    bool operator==(const GlState&) const=default;
};

void testGlState(Fixture& f,SkyEnvironment& sky) {
    f.settings(sky,"100 20 \"builtin:tron\" 1");
    const GLuint program=callerProgram();GLuint texture=0;glGenTextures(1,&texture);
    glBindTexture(GL_TEXTURE_2D,texture);glUseProgram(program);
    glEnable(GL_TEXTURE_2D);glEnable(GL_BLEND);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);glColor4f(.2f,.3f,.4f,.5f);glBlendColor(.1f,.2f,.3f,.4f);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ZERO);
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-.1,.1,-.075,.075,.1,1000);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(2,3,4);glRotatef(17,0,1,0);
    const GlState previous;
    sky.draw(identity,{.4f,.6f,.8f},.032f);
    assert(GlState()==previous && glGetError()==GL_NO_ERROR);
    glUseProgram(0);glDeleteProgram(program);glDeleteTextures(1,&texture);
    glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    glBlendEquation(GL_FUNC_ADD);glColor4f(1,1,1,1);
}

void testPanoramaTransitions(Fixture& f,SkyEnvironment& sky) {
    f.settings(sky,"100 0 \""+f.bitmap+"\"");f.waitForImage(sky);
    auto panorama=render(sky,{1,0,0});
    assert(channelSum(panorama,1)==255ul*width*height && channelSum(panorama,0)==0);
    f.settings(sky,"100 0 \"builtin:tron\" 0");
    assert(!sky.loadingImage() && sky.visible());
    assert(channelSum(render(sky,{1,0,0}),1)==0);
    f.settings(sky,"100 0 \""+f.bitmap+"\"");f.waitForImage(sky);
    assert(render(sky,{1,0,0})==panorama);
    f.settings(sky,"100 0 \"builtin:tron\" 0");
    f.settings(sky,"100 0 \"\"");
    assert(!sky.loadingImage() && !sky.visible());
    assert(channelSum(render(sky,{1,1,1}),0)==0);
}

void testStereoAndLifecycle(Fixture& f,SkyEnvironment& sky) {
    f.settings(sky,"100 0 \"builtin:tron\" 0");
    const theme::Rgb accent={.3f,.7f,1};
    const auto left=render(sky,accent,false,-.032f);
    const auto right=render(sky,accent,false,.032f);
    assert(left!=right); // Eye translation must produce real stereo parallax.
    assert(render(sky,accent,false,-.032f)==left);
    const auto before=render(sky,accent);
    for(int cycle=0;cycle<3;++cycle){
        sky.release();sky.release(); // Cleanup must be idempotent.
        assert(glGetError()==GL_NO_ERROR);
        assert(render(sky,accent)==before); // Rebuild shader and display list lazily.
        assert(render(sky,accent,false,.032f)==right);
        assert(sky.error.empty() && !sky.loadingImage());
    }
    SkyEnvironment recreated(f.config);
    recreated.update(f.now+=1);
    assert(render(recreated,accent)==before && recreated.error.empty());
    recreated.release();assert(glGetError()==GL_NO_ERROR);
}
}

int main() {
    Fixture fixture;SkyEnvironment sky(fixture.config);
    testColorAndProjection(fixture,sky);testAnimation(fixture,sky);
    testGlState(fixture,sky);testInvalidSettings(fixture,sky);testPanoramaTransitions(fixture,sky);
    testStereoAndLifecycle(fixture,sky);
    sky.release();assert(glGetError()==GL_NO_ERROR);
    std::cout<<"Tron: actual GL shader, theme color, projection, brightness, animation, settings, transitions, stereo and lifecycle passed\n";
}
