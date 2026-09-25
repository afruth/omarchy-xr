#pragma once
#include <SDL_opengl.h>

// 2D texture helpers shared by monitor panels, canvas windows, labels and the notification HUD.
namespace gltex {
// Linear filtering and clamped edges on the bound 2D texture.
inline void linearClamp() {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
inline void bind(GLuint texture) { glBindTexture(GL_TEXTURE_2D, texture); linearClamp(); }
// A new texture, left bound.
inline GLuint create() { GLuint texture=0; glGenTextures(1, &texture); bind(texture); return texture; }
// A Cairo ARGB32 raster (BGRA bytes) into a new texture; pixel store state and the binding are restored.
inline GLuint uploadBgra(const unsigned char* pixels, int width, int height) {
    GLuint texture=0; glGenTextures(1, &texture);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPushAttrib(GL_TEXTURE_BIT); bind(texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glPopAttrib(); glPopClientAttrib(); return texture;
}
}
