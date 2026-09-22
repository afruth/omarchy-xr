#pragma once
#define GL_GLEXT_PROTOTYPES
#include <SDL_opengl.h>
#include <GL/glext.h>
#include "theme.hpp"
#include <cmath>

// A stationary, world-oriented floor and distant light architecture. No images,
// render targets, scrolling UVs or per-frame geometry. Only the accent glow breathes.
class TronEnvironment {
    GLuint program=0, horizon=0;
    GLint colorLocation=-1;
    bool failed=false;
    static constexpr float pi=3.14159265358979323846f;
    static GLuint compile(GLenum type,const char* source) {
        const GLuint shader=glCreateShader(type);
        glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
        GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
        if(!ok){glDeleteShader(shader);return 0;}
        return shader;
    }
    bool ready() {
        if(program || failed)return program!=0;
        const char* vertex=R"GLSL(#version 120
varying vec2 floorPosition;
void main(){floorPosition=gl_Vertex.xz;gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;}
)GLSL";
        const char* fragment=R"GLSL(#version 120
varying vec2 floorPosition;
uniform vec3 accent;
void main(){
    vec2 p=floorPosition;
    vec2 footprint=max(fwidth(p),vec2(0.0001));
    vec2 d=abs(mod(p+2.0,4.0)-2.0);
    vec2 core=clamp((0.020+footprint-d)/footprint,0.0,1.0);
    vec2 glow=clamp((0.12+footprint-d)/(0.10+footprint),0.0,1.0);
    vec2 fade=clamp(1.0-footprint*1.25,0.0,1.0);
    float lines=max(core.x*fade.x,core.y*fade.y);
    float halo=max(glow.x*fade.x,glow.y*fade.y);
    float fog=1.0/(1.0+dot(p,p)*0.0012);
    gl_FragColor=vec4(accent*(0.005+lines*0.86+halo*0.14)*fog,1.0);
}
)GLSL";
        const GLuint vs=compile(GL_VERTEX_SHADER,vertex),fs=compile(GL_FRAGMENT_SHADER,fragment);
        if(vs && fs){
            program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
            GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
            if(!ok){glDeleteProgram(program);program=0;}
        }
        if(vs)glDeleteShader(vs);
        if(fs)glDeleteShader(fs);
        if(!program){failed=true;return false;}
        colorLocation=glGetUniformLocation(program,"accent");
        return true;
    }
    static void vertex(float angle,float height,float light,float radius=160) {
        glColor3f(light,light,light);glVertex3f(std::sin(angle)*radius,height,-std::cos(angle)*radius);
    }
    static void ribbon(float x,float z,float width,float height,float light) {
        // Vertex gradients soften the edges without a bloom buffer or line rasterization.
        constexpr float offsets[]={-.22f,-.045f,.045f,.22f};
        constexpr float intensities[]={0,1,1,0};
        for(int band=0;band<3;++band){
            const float a=offsets[band],b=offsets[band+1];
            const float low=light*intensities[band],high=light*intensities[band+1];
            for(float edge:{x-width,x+width}){
                glColor3f(low,low,low);glVertex3f(edge+a,-3.5f,z);glVertex3f(edge+a,height,z);
                glColor3f(high,high,high);glVertex3f(edge+b,height,z);glVertex3f(edge+b,-3.5f,z);
            }
            glColor3f(low,low,low);glVertex3f(x-width,height+a,z);glVertex3f(x+width,height+a,z);
            glColor3f(high,high,high);glVertex3f(x+width,height+b,z);glVertex3f(x-width,height+b,z);
        }
    }
    void buildHorizon() {
        horizon=glGenLists(1);glNewList(horizon,GL_COMPILE);
        constexpr float heights[]={-6,-3,-1,1,3,6,12};
        constexpr float lights[]={0,.025f,.075f,.11f,.060f,.013f,0};
        for(int band=0;band<6;++band){
            glBegin(GL_QUAD_STRIP);
            for(int i=0;i<=128;++i){
                const float angle=i*2*pi/128;
                vertex(angle,heights[band],lights[band]);vertex(angle,heights[band+1],lights[band+1]);
            }
            glEnd();
        }
        // An open skyline, leaving a quiet centre for monitor content in every direction.
        for(int i=0;i<32;++i){
            const float angle=i*2*pi/32, radius=65+(i*17%29);
            glPushMatrix();glRotatef(angle*180/pi,0,1,0);
            glBegin(GL_QUADS);
            ribbon(0,-radius,1.1f+(i*7%5)*.3f,1.5f+(i*13%11),.32f+(i%3)*.06f);
            // Offset return edge gives the distant structures depth without solid walls.
            ribbon(.65f,-radius-2,1.1f+(i*7%5)*.3f,1.5f+(i*13%11),.085f);
            glEnd();glPopMatrix();
        }
        glEndList();
    }
public:
    bool draw(const theme::Rgb& accent,float brightness,double now,bool animated) {
        if(!ready())return false;
        if(!horizon)buildHorizon();
        const float pulse=animated?float(.96+.04*std::sin(now*2*pi/24)):1.f;
        GLint previousProgram=0;glGetIntegerv(GL_CURRENT_PROGRAM,&previousProgram);
        glPushAttrib(GL_COLOR_BUFFER_BIT|GL_ENABLE_BIT|GL_CURRENT_BIT|GL_TEXTURE_BIT);
        glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
        glUseProgram(program);
        glUniform3f(colorLocation,accent[0]*brightness,accent[1]*brightness,accent[2]*brightness);
        glBegin(GL_QUADS);
        glVertex3f(-600,-3.5f,-600);glVertex3f(600,-3.5f,-600);
        glVertex3f(600,-3.5f,600);glVertex3f(-600,-3.5f,600);
        glEnd();
        glUseProgram(0);glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);
        glBlendColor(accent[0]*brightness*pulse,accent[1]*brightness*pulse,accent[2]*brightness*pulse,1);
        glBlendFunc(GL_CONSTANT_COLOR,GL_ONE);glCallList(horizon);
        glPopAttrib();glUseProgram(GLuint(previousProgram));
        return true;
    }
    void release() {
        if(program)glDeleteProgram(program);
        if(horizon)glDeleteLists(horizon,1);
        program=0;horizon=0;failed=false;
    }
};
