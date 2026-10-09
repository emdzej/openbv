/*
	What glu.cpp needs from the emulator core (gl1.cpp).
	Part of openbv, GPL-3.0-or-later.
*/

#ifndef OPENBV_GL1_INTERNAL_H
#define OPENBV_GL1_INTERNAL_H

#include <GL/gl.h>
#include <stdint.h>
#include <vector>

namespace gl1 {

GLenum baseFormat(GLint internal);
int formatComponents(GLenum format);
int unpackAlignment();
void unpackToRGBA(GLenum format, int w, int h, const void *pixels, GLenum base, std::vector<uint8_t> &out, int align);
void texImageRGBA(GLint level, GLenum base, int w, int h, const uint8_t *rgba);

}

#endif
