#pragma once
#define GL_GLEXT_PROTOTYPES
#include <SDL_opengl.h>
#include <GL/glext.h>
#include <iostream>

// One feathered band per side instead of twelve stacked rings. The fragment shader measures the
// distance from the panel edge and fades alpha quadratically, which is the limit of the rings.
// Fixed-function state stays in charge of transforms, blending and depth.
class HaloShader {
    GLuint program=0;
    GLint colorLoc=-1, halfLoc=-1, extentLoc=-1, centerLoc=-1, patchLoc=-1, solidLoc=-1;
    bool failed=false;
    static GLuint compile(GLenum type, const char* source) {
        const GLuint shader=glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok=0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) { glDeleteShader(shader); return 0; }
        return shader;
    }
public:
    // Panel-local coordinates come from the shared surface() texcoords: s runs along the patch
    // width and t is 1-v along its height, so the patch origin and size restore them.
    static constexpr const char* vertex=
        "#version 120\n"
        "uniform vec4 patch;\n"
        "varying vec2 local;\n"
        "void main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;"
        "local=vec2(patch.x+gl_MultiTexCoord0.x*patch.z,patch.y+(1.0-gl_MultiTexCoord0.y)*patch.w);}\n";
    static constexpr const char* fragment=
        "#version 120\n"
        "uniform vec4 color;uniform vec2 halfSize;uniform vec2 center;uniform float extent;uniform float solid;\n"
        "varying vec2 local;\n"
        "void main(){vec2 d=max(abs(local-center)-halfSize,vec2(0.0));"
        "float fade=clamp(1.0-max(max(d.x,d.y)-solid,0.0)/extent,0.0,1.0);"
        "gl_FragColor=vec4(color.rgb,color.a*fade*fade);}\n";
    bool ready() {
        if (program || failed) return program!=0;
        const GLuint vs=compile(GL_VERTEX_SHADER, vertex), fs=compile(GL_FRAGMENT_SHADER, fragment);
        if (vs && fs) {
            program=glCreateProgram();
            glAttachShader(program, vs); glAttachShader(program, fs); glLinkProgram(program);
            GLint ok=0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
            if (!ok) { glDeleteProgram(program); program=0; }
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        if (!program) { failed=true; std::cerr << "Halo shader unavailable; drawing ring halos\n"; return false; }
        colorLoc=glGetUniformLocation(program, "color"); halfLoc=glGetUniformLocation(program, "halfSize");
        extentLoc=glGetUniformLocation(program, "extent"); centerLoc=glGetUniformLocation(program, "center");
        patchLoc=glGetUniformLocation(program, "patch"); solidLoc=glGetUniformLocation(program, "solid");
        return true;
    }
    // Called while the GL context is current; a replacement context starts over.
    void release() { if (program) glDeleteProgram(program); program=0; failed=false; }
    // solid: a band of full alpha at the edge before the quadratic fade over extent begins.
    void use(float r, float g, float b, float peakAlpha, float halfW, float halfH, float extent, float centerX, float centerY, float solid=0) {
        glUseProgram(program);
        glUniform4f(colorLoc, r, g, b, peakAlpha); glUniform2f(halfLoc, halfW, halfH);
        glUniform1f(extentLoc, extent); glUniform2f(centerLoc, centerX, centerY); glUniform1f(solidLoc, solid);
    }
    void patch(float x0, float y0, float w, float h) { glUniform4f(patchLoc, x0, y0, w, h); }
    void stop() { glUseProgram(0); }
};
