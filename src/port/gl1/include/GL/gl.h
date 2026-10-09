/*
	OpenGL 1.x for gasm: the fixed-function API the BaboViolent 2 code calls,
	emulated over gasm:gl (OpenGL ES 3.0 / WebGL 2) by src/port/gl1.
	Part of openbv, GPL-3.0-or-later.
*/

#ifndef OPENBV_GL1_GL_H
#define OPENBV_GL1_GL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef GLAPI
#define GLAPI extern
#endif
#ifndef WINGDIAPI
#define WINGDIAPI
#endif

typedef unsigned int	GLenum;
typedef unsigned char	GLboolean;
typedef unsigned int	GLbitfield;
typedef void			GLvoid;
typedef signed char		GLbyte;
typedef short			GLshort;
typedef int				GLint;
typedef unsigned char	GLubyte;
typedef unsigned short	GLushort;
typedef unsigned int	GLuint;
typedef int				GLsizei;
typedef float			GLfloat;
typedef float			GLclampf;
typedef double			GLdouble;
typedef double			GLclampd;
typedef char			GLchar;
typedef ptrdiff_t		GLintptr;
typedef ptrdiff_t		GLsizeiptr;

#define GL_VERSION_1_1 1

/* Boolean */
#define GL_FALSE						0
#define GL_TRUE							1

/* Data types */
#define GL_BYTE							0x1400
#define GL_UNSIGNED_BYTE				0x1401
#define GL_SHORT						0x1402
#define GL_UNSIGNED_SHORT				0x1403
#define GL_INT							0x1404
#define GL_UNSIGNED_INT					0x1405
#define GL_FLOAT						0x1406
#define GL_2_BYTES						0x1407
#define GL_3_BYTES						0x1408
#define GL_4_BYTES						0x1409
#define GL_DOUBLE						0x140A

/* Primitives */
#define GL_POINTS						0x0000
#define GL_LINES						0x0001
#define GL_LINE_LOOP					0x0002
#define GL_LINE_STRIP					0x0003
#define GL_TRIANGLES					0x0004
#define GL_TRIANGLE_STRIP				0x0005
#define GL_TRIANGLE_FAN					0x0006
#define GL_QUADS						0x0007
#define GL_QUAD_STRIP					0x0008
#define GL_POLYGON						0x0009

/* Attribute bits */
#define GL_CURRENT_BIT					0x00000001
#define GL_POINT_BIT					0x00000002
#define GL_LINE_BIT						0x00000004
#define GL_POLYGON_BIT					0x00000008
#define GL_POLYGON_STIPPLE_BIT			0x00000010
#define GL_PIXEL_MODE_BIT				0x00000020
#define GL_LIGHTING_BIT					0x00000040
#define GL_FOG_BIT						0x00000080
#define GL_DEPTH_BUFFER_BIT				0x00000100
#define GL_ACCUM_BUFFER_BIT				0x00000200
#define GL_STENCIL_BUFFER_BIT			0x00000400
#define GL_VIEWPORT_BIT					0x00000800
#define GL_TRANSFORM_BIT				0x00001000
#define GL_ENABLE_BIT					0x00002000
#define GL_COLOR_BUFFER_BIT				0x00004000
#define GL_HINT_BIT						0x00008000
#define GL_EVAL_BIT						0x00010000
#define GL_LIST_BIT						0x00020000
#define GL_TEXTURE_BIT					0x00040000
#define GL_SCISSOR_BIT					0x00080000
#define GL_ALL_ATTRIB_BITS				0x000FFFFF
#define GL_CLIENT_PIXEL_STORE_BIT		0x00000001
#define GL_CLIENT_VERTEX_ARRAY_BIT		0x00000002
#define GL_CLIENT_ALL_ATTRIB_BITS		0xFFFFFFFF

/* Matrices */
#define GL_MATRIX_MODE					0x0BA0
#define GL_MODELVIEW					0x1700
#define GL_PROJECTION					0x1701
#define GL_TEXTURE						0x1702
#define GL_MODELVIEW_STACK_DEPTH		0x0BA3
#define GL_PROJECTION_STACK_DEPTH		0x0BA4
#define GL_TEXTURE_STACK_DEPTH			0x0BA5
#define GL_MODELVIEW_MATRIX				0x0BA6
#define GL_PROJECTION_MATRIX			0x0BA7
#define GL_TEXTURE_MATRIX				0x0BA8

/* Capabilities */
#define GL_POINT_SMOOTH					0x0B10
#define GL_LINE_SMOOTH					0x0B20
#define GL_LINE_STIPPLE					0x0B24
#define GL_POLYGON_SMOOTH				0x0B41
#define GL_POLYGON_STIPPLE				0x0B42
#define GL_CULL_FACE					0x0B44
#define GL_LIGHTING						0x0B50
#define GL_COLOR_MATERIAL				0x0B57
#define GL_FOG							0x0B60
#define GL_DEPTH_TEST					0x0B71
#define GL_STENCIL_TEST					0x0B90
#define GL_NORMALIZE					0x0BA1
#define GL_ALPHA_TEST					0x0BC0
#define GL_DITHER						0x0BD0
#define GL_BLEND						0x0BE2
#define GL_INDEX_LOGIC_OP				0x0BF1
#define GL_COLOR_LOGIC_OP				0x0BF2
#define GL_SCISSOR_TEST					0x0C11
#define GL_TEXTURE_GEN_S				0x0C60
#define GL_TEXTURE_GEN_T				0x0C61
#define GL_TEXTURE_GEN_R				0x0C62
#define GL_TEXTURE_GEN_Q				0x0C63
#define GL_AUTO_NORMAL					0x0D80
#define GL_TEXTURE_1D					0x0DE0
#define GL_TEXTURE_2D					0x0DE1
#define GL_POLYGON_OFFSET_POINT			0x2A01
#define GL_POLYGON_OFFSET_LINE			0x2A02
#define GL_POLYGON_OFFSET_FILL			0x8037
#define GL_RESCALE_NORMAL				0x803A
#define GL_CLIP_PLANE0					0x3000
#define GL_CLIP_PLANE1					0x3001
#define GL_CLIP_PLANE2					0x3002
#define GL_CLIP_PLANE3					0x3003
#define GL_CLIP_PLANE4					0x3004
#define GL_CLIP_PLANE5					0x3005
#define GL_LIGHT0						0x4000
#define GL_LIGHT1						0x4001
#define GL_LIGHT2						0x4002
#define GL_LIGHT3						0x4003
#define GL_LIGHT4						0x4004
#define GL_LIGHT5						0x4005
#define GL_LIGHT6						0x4006
#define GL_LIGHT7						0x4007

/* Client arrays */
#define GL_VERTEX_ARRAY					0x8074
#define GL_NORMAL_ARRAY					0x8075
#define GL_COLOR_ARRAY					0x8076
#define GL_INDEX_ARRAY					0x8077
#define GL_TEXTURE_COORD_ARRAY			0x8078
#define GL_EDGE_FLAG_ARRAY				0x8079

/* Faces and polygons */
#define GL_NONE							0
#define GL_FRONT_LEFT					0x0400
#define GL_FRONT_RIGHT					0x0401
#define GL_BACK_LEFT					0x0402
#define GL_BACK_RIGHT					0x0403
#define GL_FRONT						0x0404
#define GL_BACK							0x0405
#define GL_LEFT							0x0406
#define GL_RIGHT						0x0407
#define GL_FRONT_AND_BACK				0x0408
#define GL_CW							0x0900
#define GL_CCW							0x0901
#define GL_POLYGON_MODE					0x0B40
#define GL_CULL_FACE_MODE				0x0B45
#define GL_FRONT_FACE					0x0B46
#define GL_POINT						0x1B00
#define GL_LINE							0x1B01
#define GL_FILL							0x1B02
#define GL_POLYGON_OFFSET_UNITS			0x2A00
#define GL_POLYGON_OFFSET_FACTOR		0x8038

/* Blending */
#define GL_ZERO							0
#define GL_ONE							1
#define GL_SRC_COLOR					0x0300
#define GL_ONE_MINUS_SRC_COLOR			0x0301
#define GL_SRC_ALPHA					0x0302
#define GL_ONE_MINUS_SRC_ALPHA			0x0303
#define GL_DST_ALPHA					0x0304
#define GL_ONE_MINUS_DST_ALPHA			0x0305
#define GL_DST_COLOR					0x0306
#define GL_ONE_MINUS_DST_COLOR			0x0307
#define GL_SRC_ALPHA_SATURATE			0x0308
#define GL_BLEND_DST					0x0BE0
#define GL_BLEND_SRC					0x0BE1

/* Comparisons */
#define GL_NEVER						0x0200
#define GL_LESS							0x0201
#define GL_EQUAL						0x0202
#define GL_LEQUAL						0x0203
#define GL_GREATER						0x0204
#define GL_NOTEQUAL						0x0205
#define GL_GEQUAL						0x0206
#define GL_ALWAYS						0x0207
#define GL_DEPTH_RANGE					0x0B70
#define GL_DEPTH_WRITEMASK				0x0B72
#define GL_DEPTH_CLEAR_VALUE			0x0B73
#define GL_DEPTH_FUNC					0x0B74
#define GL_ALPHA_TEST_FUNC				0x0BC1
#define GL_ALPHA_TEST_REF				0x0BC2

/* Logic ops */
#define GL_LOGIC_OP_MODE				0x0BF0
#define GL_CLEAR						0x1500
#define GL_AND							0x1501
#define GL_AND_REVERSE					0x1502
#define GL_COPY							0x1503
#define GL_AND_INVERTED					0x1504
#define GL_NOOP							0x1505
#define GL_XOR							0x1506
#define GL_OR							0x1507
#define GL_NOR							0x1508
#define GL_EQUIV						0x1509
#define GL_INVERT						0x150A
#define GL_OR_REVERSE					0x150B
#define GL_COPY_INVERTED				0x150C
#define GL_OR_INVERTED					0x150D
#define GL_NAND							0x150E
#define GL_SET							0x150F

/* Lighting */
#define GL_LIGHT_MODEL_LOCAL_VIEWER		0x0B51
#define GL_LIGHT_MODEL_TWO_SIDE			0x0B52
#define GL_LIGHT_MODEL_AMBIENT			0x0B53
#define GL_SHADE_MODEL					0x0B54
#define GL_COLOR_MATERIAL_FACE			0x0B55
#define GL_COLOR_MATERIAL_PARAMETER		0x0B56
#define GL_AMBIENT						0x1200
#define GL_DIFFUSE						0x1201
#define GL_SPECULAR						0x1202
#define GL_POSITION						0x1203
#define GL_SPOT_DIRECTION				0x1204
#define GL_SPOT_EXPONENT				0x1205
#define GL_SPOT_CUTOFF					0x1206
#define GL_CONSTANT_ATTENUATION			0x1207
#define GL_LINEAR_ATTENUATION			0x1208
#define GL_QUADRATIC_ATTENUATION		0x1209
#define GL_EMISSION						0x1600
#define GL_SHININESS					0x1601
#define GL_AMBIENT_AND_DIFFUSE			0x1602
#define GL_COLOR_INDEXES				0x1603
#define GL_FLAT							0x1D00
#define GL_SMOOTH						0x1D01

/* Fog */
#define GL_FOG_INDEX					0x0B61
#define GL_FOG_DENSITY					0x0B62
#define GL_FOG_START					0x0B63
#define GL_FOG_END						0x0B64
#define GL_FOG_MODE						0x0B65
#define GL_FOG_COLOR					0x0B66
#define GL_EXP							0x0800
#define GL_EXP2							0x0801

/* Textures */
#define GL_TEXTURE_WIDTH				0x1000
#define GL_TEXTURE_HEIGHT				0x1001
#define GL_TEXTURE_INTERNAL_FORMAT		0x1003
#define GL_TEXTURE_COMPONENTS			0x1003
#define GL_TEXTURE_BORDER_COLOR			0x1004
#define GL_TEXTURE_BORDER				0x1005
#define GL_TEXTURE_ENV_MODE				0x2200
#define GL_TEXTURE_ENV_COLOR			0x2201
#define GL_TEXTURE_ENV					0x2300
#define GL_MODULATE						0x2100
#define GL_DECAL						0x2101
#define GL_ADD							0x0104
#define GL_REPLACE						0x1E01
#define GL_NEAREST						0x2600
#define GL_LINEAR						0x2601
#define GL_NEAREST_MIPMAP_NEAREST		0x2700
#define GL_LINEAR_MIPMAP_NEAREST		0x2701
#define GL_NEAREST_MIPMAP_LINEAR		0x2702
#define GL_LINEAR_MIPMAP_LINEAR			0x2703
#define GL_TEXTURE_MAG_FILTER			0x2800
#define GL_TEXTURE_MIN_FILTER			0x2801
#define GL_TEXTURE_WRAP_S				0x2802
#define GL_TEXTURE_WRAP_T				0x2803
#define GL_CLAMP						0x2900
#define GL_REPEAT						0x2901
#define GL_TEXTURE_BINDING_1D			0x8068
#define GL_TEXTURE_BINDING_2D			0x8069
#define GL_S							0x2000
#define GL_T							0x2001
#define GL_R							0x2002
#define GL_Q							0x2003
#define GL_EYE_LINEAR					0x2400
#define GL_OBJECT_LINEAR				0x2401
#define GL_SPHERE_MAP					0x2402
#define GL_TEXTURE_GEN_MODE				0x2500
#define GL_OBJECT_PLANE					0x2501
#define GL_EYE_PLANE					0x2502

/* Pixel formats */
#define GL_COLOR_INDEX					0x1900
#define GL_STENCIL_INDEX				0x1901
#define GL_DEPTH_COMPONENT				0x1902
#define GL_RED							0x1903
#define GL_GREEN						0x1904
#define GL_BLUE							0x1905
#define GL_ALPHA						0x1906
#define GL_RGB							0x1907
#define GL_RGBA							0x1908
#define GL_LUMINANCE					0x1909
#define GL_LUMINANCE_ALPHA				0x190A
#define GL_R3_G3_B2						0x2A10
#define GL_ALPHA4						0x803B
#define GL_ALPHA8						0x803C
#define GL_LUMINANCE4					0x803F
#define GL_LUMINANCE8					0x8040
#define GL_LUMINANCE8_ALPHA8			0x8045
#define GL_INTENSITY					0x8049
#define GL_INTENSITY8					0x804B
#define GL_RGB4							0x804F
#define GL_RGB5							0x8050
#define GL_RGB8							0x8051
#define GL_RGBA4						0x8056
#define GL_RGB5_A1						0x8057
#define GL_RGBA8						0x8058

/* Pixel store */
#define GL_UNPACK_SWAP_BYTES			0x0CF0
#define GL_UNPACK_LSB_FIRST				0x0CF1
#define GL_UNPACK_ROW_LENGTH			0x0CF2
#define GL_UNPACK_SKIP_ROWS				0x0CF3
#define GL_UNPACK_SKIP_PIXELS			0x0CF4
#define GL_UNPACK_ALIGNMENT				0x0CF5
#define GL_PACK_SWAP_BYTES				0x0D00
#define GL_PACK_LSB_FIRST				0x0D01
#define GL_PACK_ROW_LENGTH				0x0D02
#define GL_PACK_SKIP_ROWS				0x0D03
#define GL_PACK_SKIP_PIXELS				0x0D04
#define GL_PACK_ALIGNMENT				0x0D05

/* Display lists */
#define GL_COMPILE						0x1300
#define GL_COMPILE_AND_EXECUTE			0x1301
#define GL_LIST_MODE					0x0B30
#define GL_MAX_LIST_NESTING				0x0B31
#define GL_LIST_BASE					0x0B32
#define GL_LIST_INDEX					0x0B33

/* Queries */
#define GL_CURRENT_COLOR				0x0B00
#define GL_CURRENT_INDEX				0x0B01
#define GL_CURRENT_NORMAL				0x0B02
#define GL_CURRENT_TEXTURE_COORDS		0x0B03
#define GL_POINT_SIZE					0x0B11
#define GL_LINE_WIDTH					0x0B21
#define GL_VIEWPORT						0x0BA2
#define GL_SCISSOR_BOX					0x0C10
#define GL_DRAW_BUFFER					0x0C01
#define GL_READ_BUFFER					0x0C02
#define GL_COLOR_CLEAR_VALUE			0x0C22
#define GL_COLOR_WRITEMASK				0x0C23
#define GL_MAX_LIGHTS					0x0D31
#define GL_MAX_CLIP_PLANES				0x0D32
#define GL_MAX_TEXTURE_SIZE				0x0D33
#define GL_MAX_ATTRIB_STACK_DEPTH		0x0D35
#define GL_MAX_MODELVIEW_STACK_DEPTH	0x0D36
#define GL_MAX_PROJECTION_STACK_DEPTH	0x0D38
#define GL_MAX_TEXTURE_STACK_DEPTH		0x0D39
#define GL_MAX_VIEWPORT_DIMS			0x0D3A
#define GL_RED_BITS						0x0D52
#define GL_GREEN_BITS					0x0D53
#define GL_BLUE_BITS					0x0D54
#define GL_ALPHA_BITS					0x0D55
#define GL_DEPTH_BITS					0x0D56
#define GL_STENCIL_BITS					0x0D57

/* Hints */
#define GL_PERSPECTIVE_CORRECTION_HINT	0x0C50
#define GL_POINT_SMOOTH_HINT			0x0C51
#define GL_LINE_SMOOTH_HINT				0x0C52
#define GL_POLYGON_SMOOTH_HINT			0x0C53
#define GL_FOG_HINT						0x0C54
#define GL_DONT_CARE					0x1100
#define GL_FASTEST						0x1101
#define GL_NICEST						0x1102

/* Strings */
#define GL_VENDOR						0x1F00
#define GL_RENDERER						0x1F01
#define GL_VERSION						0x1F02
#define GL_EXTENSIONS					0x1F03

/* Errors */
#define GL_NO_ERROR						0
#define GL_INVALID_ENUM					0x0500
#define GL_INVALID_VALUE				0x0501
#define GL_INVALID_OPERATION			0x0502
#define GL_STACK_OVERFLOW				0x0503
#define GL_STACK_UNDERFLOW				0x0504
#define GL_OUT_OF_MEMORY				0x0505

/* Immediate mode */
GLAPI void APIENTRY glBegin(GLenum mode);
GLAPI void APIENTRY glEnd(void);
GLAPI void APIENTRY glVertex2i(GLint x, GLint y);
GLAPI void APIENTRY glVertex2f(GLfloat x, GLfloat y);
GLAPI void APIENTRY glVertex2d(GLdouble x, GLdouble y);
GLAPI void APIENTRY glVertex2fv(const GLfloat *v);
GLAPI void APIENTRY glVertex2iv(const GLint *v);
GLAPI void APIENTRY glVertex3i(GLint x, GLint y, GLint z);
GLAPI void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z);
GLAPI void APIENTRY glVertex3d(GLdouble x, GLdouble y, GLdouble z);
GLAPI void APIENTRY glVertex3fv(const GLfloat *v);
GLAPI void APIENTRY glVertex3iv(const GLint *v);
GLAPI void APIENTRY glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
GLAPI void APIENTRY glVertex4fv(const GLfloat *v);
GLAPI void APIENTRY glTexCoord2i(GLint s, GLint t);
GLAPI void APIENTRY glTexCoord2f(GLfloat s, GLfloat t);
GLAPI void APIENTRY glTexCoord2d(GLdouble s, GLdouble t);
GLAPI void APIENTRY glTexCoord2fv(const GLfloat *v);
GLAPI void APIENTRY glColor3f(GLfloat r, GLfloat g, GLfloat b);
GLAPI void APIENTRY glColor3d(GLdouble r, GLdouble g, GLdouble b);
GLAPI void APIENTRY glColor3fv(const GLfloat *v);
GLAPI void APIENTRY glColor3ub(GLubyte r, GLubyte g, GLubyte b);
GLAPI void APIENTRY glColor3ubv(const GLubyte *v);
GLAPI void APIENTRY glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
GLAPI void APIENTRY glColor4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a);
GLAPI void APIENTRY glColor4fv(const GLfloat *v);
GLAPI void APIENTRY glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
GLAPI void APIENTRY glColor4ubv(const GLubyte *v);
GLAPI void APIENTRY glNormal3f(GLfloat x, GLfloat y, GLfloat z);
GLAPI void APIENTRY glNormal3fv(const GLfloat *v);

/* Matrices */
GLAPI void APIENTRY glMatrixMode(GLenum mode);
GLAPI void APIENTRY glPushMatrix(void);
GLAPI void APIENTRY glPopMatrix(void);
GLAPI void APIENTRY glLoadIdentity(void);
GLAPI void APIENTRY glLoadMatrixf(const GLfloat *m);
GLAPI void APIENTRY glLoadMatrixd(const GLdouble *m);
GLAPI void APIENTRY glMultMatrixf(const GLfloat *m);
GLAPI void APIENTRY glMultMatrixd(const GLdouble *m);
GLAPI void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z);
GLAPI void APIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z);
GLAPI void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
GLAPI void APIENTRY glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
GLAPI void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z);
GLAPI void APIENTRY glScaled(GLdouble x, GLdouble y, GLdouble z);
GLAPI void APIENTRY glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
GLAPI void APIENTRY glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
GLAPI void APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
GLAPI void APIENTRY glDepthRange(GLclampd n, GLclampd f);

/* State */
GLAPI void APIENTRY glEnable(GLenum cap);
GLAPI void APIENTRY glDisable(GLenum cap);
GLAPI GLboolean APIENTRY glIsEnabled(GLenum cap);
GLAPI void APIENTRY glPushAttrib(GLbitfield mask);
GLAPI void APIENTRY glPopAttrib(void);
GLAPI void APIENTRY glPushClientAttrib(GLbitfield mask);
GLAPI void APIENTRY glPopClientAttrib(void);
GLAPI void APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor);
GLAPI void APIENTRY glAlphaFunc(GLenum func, GLclampf ref);
GLAPI void APIENTRY glDepthFunc(GLenum func);
GLAPI void APIENTRY glDepthMask(GLboolean flag);
GLAPI void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
GLAPI void APIENTRY glCullFace(GLenum mode);
GLAPI void APIENTRY glFrontFace(GLenum mode);
GLAPI void APIENTRY glPolygonMode(GLenum face, GLenum mode);
GLAPI void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units);
GLAPI void APIENTRY glLineWidth(GLfloat width);
GLAPI void APIENTRY glPointSize(GLfloat size);
GLAPI void APIENTRY glLogicOp(GLenum opcode);
GLAPI void APIENTRY glShadeModel(GLenum mode);
GLAPI void APIENTRY glHint(GLenum target, GLenum mode);
GLAPI void APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
GLAPI void APIENTRY glClear(GLbitfield mask);
GLAPI void APIENTRY glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
GLAPI void APIENTRY glClearDepth(GLclampd depth);
GLAPI void APIENTRY glFlush(void);
GLAPI void APIENTRY glFinish(void);
GLAPI GLenum APIENTRY glGetError(void);
GLAPI const GLubyte * APIENTRY glGetString(GLenum name);
GLAPI void APIENTRY glGetBooleanv(GLenum pname, GLboolean *params);
GLAPI void APIENTRY glGetIntegerv(GLenum pname, GLint *params);
GLAPI void APIENTRY glGetFloatv(GLenum pname, GLfloat *params);
GLAPI void APIENTRY glGetDoublev(GLenum pname, GLdouble *params);

/* Lighting and fog */
GLAPI void APIENTRY glLightf(GLenum light, GLenum pname, GLfloat param);
GLAPI void APIENTRY glLightfv(GLenum light, GLenum pname, const GLfloat *params);
GLAPI void APIENTRY glLightModelf(GLenum pname, GLfloat param);
GLAPI void APIENTRY glLightModelfv(GLenum pname, const GLfloat *params);
GLAPI void APIENTRY glLightModeli(GLenum pname, GLint param);
GLAPI void APIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param);
GLAPI void APIENTRY glMateriali(GLenum face, GLenum pname, GLint param);
GLAPI void APIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
GLAPI void APIENTRY glColorMaterial(GLenum face, GLenum mode);
GLAPI void APIENTRY glFogf(GLenum pname, GLfloat param);
GLAPI void APIENTRY glFogi(GLenum pname, GLint param);
GLAPI void APIENTRY glFogfv(GLenum pname, const GLfloat *params);
GLAPI void APIENTRY glFogiv(GLenum pname, const GLint *params);

/* Textures */
GLAPI void APIENTRY glGenTextures(GLsizei n, GLuint *textures);
GLAPI void APIENTRY glDeleteTextures(GLsizei n, const GLuint *textures);
GLAPI GLboolean APIENTRY glIsTexture(GLuint texture);
GLAPI void APIENTRY glBindTexture(GLenum target, GLuint texture);
GLAPI void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param);
GLAPI void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param);
GLAPI void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
GLAPI void APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param);
GLAPI void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param);
GLAPI void APIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
GLAPI void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
GLAPI void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
GLAPI void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
GLAPI void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
GLAPI void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
GLAPI void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params);
GLAPI void APIENTRY glPixelStorei(GLenum pname, GLint param);
GLAPI void APIENTRY glReadBuffer(GLenum mode);
GLAPI void APIENTRY glDrawBuffer(GLenum mode);
GLAPI void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);

/* Display lists */
GLAPI GLuint APIENTRY glGenLists(GLsizei range);
GLAPI void APIENTRY glDeleteLists(GLuint list, GLsizei range);
GLAPI GLboolean APIENTRY glIsList(GLuint list);
GLAPI void APIENTRY glNewList(GLuint list, GLenum mode);
GLAPI void APIENTRY glEndList(void);
GLAPI void APIENTRY glCallList(GLuint list);
GLAPI void APIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid *lists);
GLAPI void APIENTRY glListBase(GLuint base);

/* Vertex arrays */
GLAPI void APIENTRY glEnableClientState(GLenum array);
GLAPI void APIENTRY glDisableClientState(GLenum array);
GLAPI void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
GLAPI void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer);
GLAPI void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
GLAPI void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
GLAPI void APIENTRY glArrayElement(GLint i);
GLAPI void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count);
GLAPI void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);

#ifdef __cplusplus
}
#endif

#endif
