#pragma once
#include "notification_space.hpp"
#include "notification_content.hpp"
#include <SDL_opengl.h>

namespace notifications {
inline void vertex(space::Vec p){glVertex3f(p.x,p.y,p.z);}
inline space::Vec local(space::Vec center,const space::Basis& b,float x,float y,float depth=0){
    return space::add(center,space::add(space::mul(b.right,x),space::add(space::mul(b.up,y),space::mul(b.forward,depth))));
}
inline std::array<space::Vec,32> outline(space::Vec center,const space::Basis& b,float width,float height,float depth){
    std::array<space::Vec,32> result{};const float radius=std::min(.055f,height*.15f);
    for(int corner=0;corner<4;++corner)for(int i=0;i<8;++i){
        const float angle=(corner+float(i)/7)*spatial::pi/2;
        const float x=(corner==0 || corner==3?1:-1)*(width/2-radius)+radius*std::cos(angle);
        const float y=(corner<2?1:-1)*(height/2-radius)+radius*std::sin(angle);
        result[corner*8+i]=local(center,b,x,y,depth);
    }
    return result;
}
inline void tint(const Color& c,float alpha,float light=1){glColor4f(c[0]*alpha*light,c[1]*alpha*light,c[2]*alpha*light,alpha);}
inline void drawCard(GLuint texture,const Card& card,const space::Scene& scene,space::Vec center,float width,float height,float alpha){
    const auto b=space::facing(center,scene.eye);
    const auto front=outline(center,b,width+.035f,height+.035f,0),back=outline(center,b,width+.035f,height+.035f,.045f);
    glDisable(GL_TEXTURE_2D);
    tint(card.background,alpha,.65f);
    glBegin(GL_TRIANGLE_FAN);vertex(local(center,b,0,0,.045f));for(auto p:back)vertex(p);vertex(back[0]);glEnd();
    // A shallow physical rim catches the theme accent; it has real thickness
    // and parallax, with no animated bobbing or billboard rotation on head turns.
    glBegin(GL_QUADS);
    for(size_t i=0;i<front.size();++i){const size_t j=(i+1)%front.size();
        tint(card.accent,alpha,.27f);vertex(back[i]);vertex(back[j]);
        tint(card.accent,alpha,.65f);vertex(front[j]);vertex(front[i]);
    }
    glEnd();
    tint(card.accent,alpha,.60f);glBegin(GL_TRIANGLE_FAN);vertex(center);for(auto p:front)vertex(p);vertex(front[0]);glEnd();
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,texture);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glColor4f(alpha,alpha,alpha,alpha);glBegin(GL_QUADS);
    glTexCoord2f(0,0);vertex(local(center,b,-width/2,height/2,-.002f));
    glTexCoord2f(1,0);vertex(local(center,b,width/2,height/2,-.002f));
    glTexCoord2f(1,1);vertex(local(center,b,width/2,-height/2,-.002f));
    glTexCoord2f(0,1);vertex(local(center,b,-width/2,-height/2,-.002f));
    glEnd();
}
inline void drawCue(const Card& card,const space::Scene& scene,space::Vec position,float alpha,double now){
    const auto cue=space::cue(scene,position);const float radius=space::length(space::sub(cue.center,scene.eye));
    const float size=radius*.009f; // ~1 degree; never a second text panel
    const float pulse=.55f+.15f*std::sin(float(now)*2*spatial::pi/3.2f);
    const auto along=space::add(space::mul(cue.right,std::cos(cue.angle)),space::mul(cue.up,std::sin(cue.angle)));
    const auto across=space::add(space::mul(cue.right,-std::sin(cue.angle)),space::mul(cue.up,std::cos(cue.angle)));
    auto point=[&](float x,float y){return space::add(cue.center,space::add(space::mul(along,x*size),space::mul(across,y*size)));};
    glDisable(GL_TEXTURE_2D);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    // Soft under-glow and a small solid chevron, placed at the card's depth in
    // both eyes. The direction survives cards behind the user's head as well.
    for(int pass=0;pass<2;++pass){
        const float scale=pass==0?1.5f:1.f;tint(card.accent,alpha*pulse*(pass==0?.16f:1.f));
        glBegin(GL_TRIANGLES);
        vertex(point(1.0f*scale,0));vertex(point(-.65f*scale,.8f*scale));vertex(point(-.18f*scale,0));
        vertex(point(1.0f*scale,0));vertex(point(-.18f*scale,0));vertex(point(-.65f*scale,-.8f*scale));
        glEnd();
    }
}
}
