/*
	GLU for gasm: the part of GLU 1.3 the BaboViolent 2 code calls, on src/port/gl1.
	Part of openbv, GPL-3.0-or-later.
*/

#ifndef OPENBV_GL1_GLU_H
#define OPENBV_GL1_GLU_H

#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLU_FALSE						0
#define GLU_TRUE						1

#define GLU_VERSION						100800
#define GLU_EXTENSIONS					100801

#define GLU_INVALID_ENUM				100900
#define GLU_INVALID_VALUE				100901
#define GLU_OUT_OF_MEMORY				100902

/* Quadrics */
#define GLU_SMOOTH						100000
#define GLU_FLAT						100001
#define GLU_NONE						100002
#define GLU_POINT						100010
#define GLU_LINE						100011
#define GLU_FILL						100012
#define GLU_SILHOUETTE					100013
#define GLU_OUTSIDE						100020
#define GLU_INSIDE						100021

typedef struct GLUquadric GLUquadric;
typedef GLUquadric GLUquadricObj;

GLAPI void APIENTRY gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
GLAPI void APIENTRY gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top);
GLAPI void APIENTRY gluLookAt(GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ, GLdouble centerX, GLdouble centerY, GLdouble centerZ, GLdouble upX, GLdouble upY, GLdouble upZ);
GLAPI GLint APIENTRY gluProject(GLdouble objX, GLdouble objY, GLdouble objZ, const GLdouble *model, const GLdouble *proj, const GLint *view, GLdouble *winX, GLdouble *winY, GLdouble *winZ);
GLAPI GLint APIENTRY gluUnProject(GLdouble winX, GLdouble winY, GLdouble winZ, const GLdouble *model, const GLdouble *proj, const GLint *view, GLdouble *objX, GLdouble *objY, GLdouble *objZ);

GLAPI GLint APIENTRY gluScaleImage(GLenum format, GLsizei wIn, GLsizei hIn, GLenum typeIn, const void *dataIn, GLsizei wOut, GLsizei hOut, GLenum typeOut, GLvoid *dataOut);
GLAPI GLint APIENTRY gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *data);

GLAPI GLUquadric * APIENTRY gluNewQuadric(void);
GLAPI void APIENTRY gluDeleteQuadric(GLUquadric *quad);
GLAPI void APIENTRY gluQuadricDrawStyle(GLUquadric *quad, GLenum draw);
GLAPI void APIENTRY gluQuadricNormals(GLUquadric *quad, GLenum normal);
GLAPI void APIENTRY gluQuadricOrientation(GLUquadric *quad, GLenum orientation);
GLAPI void APIENTRY gluQuadricTexture(GLUquadric *quad, GLboolean texture);
GLAPI void APIENTRY gluSphere(GLUquadric *quad, GLdouble radius, GLint slices, GLint stacks);

GLAPI const GLubyte * APIENTRY gluErrorString(GLenum error);

#ifdef __cplusplus
}
#endif

#endif
