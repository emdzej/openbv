/*
	GLU on the GL 1.x emulator: the projection helpers, image scaling and mipmaps, and
	sphere quadrics, following the SGI GLU 1.3 reference implementation's algorithms (the
	GLU that Windows' glu32 and Mesa both derive from), so that spheres get the same
	tessellation and texture coordinates and mipmaps the same filtering.
	Part of openbv, GPL-3.0-or-later.
*/

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>
#include "gl1_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace gl1;

struct GLUquadric
{
	GLenum drawStyle, normals, orientation;
	GLboolean textureCoords;
};

namespace {

const double PI = 3.14159265358979323846;

void identityd(double *m)
{
	memset(m, 0, 16 * sizeof(double));
	m[0] = m[5] = m[10] = m[15] = 1;
}

void xformd(double *out, const double *m, const double *in)
{
	for (int i = 0; i < 4; i++)
		out[i] = in[0] * m[0 * 4 + i] + in[1] * m[1 * 4 + i] + in[2] * m[2 * 4 + i] + in[3] * m[3 * 4 + i];
}

/* r = a * b, column-major */
void muld(double *r, const double *a, const double *b)
{
	for (int c = 0; c < 4; c++)
		for (int row = 0; row < 4; row++)
			r[c * 4 + row] = a[row] * b[c * 4] + a[4 + row] * b[c * 4 + 1] + a[8 + row] * b[c * 4 + 2] + a[12 + row] * b[c * 4 + 3];
}

bool invertd(const double *m, double *out)
{
	double inv[16];
	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
	double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (det == 0) return false;
	det = 1.0 / det;
	for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
	return true;
}

/* SGI GLU's nearestPower: the power of two closest to value, rounding at 1.5x */
int nearestPower(int value)
{
	int i = 1;
	if (value <= 0) return -1;
	for (;;)
	{
		if (value == 1) return i;
		if (value == 3) return i * 4;
		value >>= 1;
		i *= 2;
	}
}

/* Box filter: each output texel is the area-weighted mean of the input texels it covers */
void scaleRGBA(const uint8_t *in, int wi, int hi, uint8_t *out, int wo, int ho)
{
	double sx = (double)wi / wo, sy = (double)hi / ho;
	for (int y = 0; y < ho; y++)
	{
		double y0 = y * sy, y1 = y0 + sy;
		for (int x = 0; x < wo; x++)
		{
			double x0 = x * sx, x1 = x0 + sx;
			double acc[4] = { 0, 0, 0, 0 }, area = 0;
			for (int iy = (int)floor(y0); iy < (int)ceil(y1) && iy < hi; iy++)
			{
				double wy = (iy + 1 < y1 ? iy + 1 : y1) - (iy > y0 ? iy : y0);
				if (wy <= 0) continue;
				for (int ix = (int)floor(x0); ix < (int)ceil(x1) && ix < wi; ix++)
				{
					double wx = (ix + 1 < x1 ? ix + 1 : x1) - (ix > x0 ? ix : x0);
					if (wx <= 0) continue;
					const uint8_t *p = in + ((size_t)iy * wi + ix) * 4;
					double w = wx * wy;
					for (int c = 0; c < 4; c++) acc[c] += p[c] * w;
					area += w;
				}
			}
			uint8_t *d = out + ((size_t)y * wo + x) * 4;
			for (int c = 0; c < 4; c++)
			{
				double v = area > 0 ? acc[c] / area + .5 : 0;
				d[c] = (uint8_t)(v > 255 ? 255 : v);
			}
		}
	}
}

/* SGI halveImage / halve1Dimage for unsigned bytes */
void halveRGBA(const uint8_t *in, int w, int h, std::vector<uint8_t> &out, int &wo, int &ho)
{
	wo = w > 1 ? w / 2 : 1;
	ho = h > 1 ? h / 2 : 1;
	out.assign((size_t)wo * ho * 4, 0);
	if (w > 1 && h > 1)
	{
		for (int y = 0; y < ho; y++)
			for (int x = 0; x < wo; x++)
			{
				const uint8_t *a = in + ((size_t)(2 * y) * w + 2 * x) * 4, *b = a + 4, *c = a + (size_t)w * 4, *d = c + 4;
				uint8_t *o = &out[((size_t)y * wo + x) * 4];
				for (int k = 0; k < 4; k++) o[k] = (uint8_t)((a[k] + b[k] + c[k] + d[k] + 2) / 4);
			}
	}
	else
	{
		int n = w > 1 ? wo : ho;
		for (int i = 0; i < n; i++)
		{
			const uint8_t *a = in + (size_t)(2 * i) * 4, *b = a + 4;
			uint8_t *o = &out[(size_t)i * 4];
			for (int k = 0; k < 4; k++) o[k] = (uint8_t)((a[k] + b[k]) / 2);
		}
	}
}

} /* namespace */

extern "C" {

void APIENTRY gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
	double r = fovy / 2 * PI / 180;
	double dz = zFar - zNear, s = sin(r);
	if (dz == 0 || s == 0 || aspect == 0) return;
	double c = cos(r) / s;
	double m[16];
	identityd(m);
	m[0] = c / aspect;
	m[5] = c;
	m[10] = -(zFar + zNear) / dz;
	m[11] = -1;
	m[14] = -2 * zNear * zFar / dz;
	m[15] = 0;
	glMultMatrixd(m);
}

void APIENTRY gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top)
{
	glOrtho(left, right, bottom, top, -1, 1);
}

void APIENTRY gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez, GLdouble cx, GLdouble cy, GLdouble cz, GLdouble ux, GLdouble uy, GLdouble uz)
{
	float f[3] = { (float)(cx - ex), (float)(cy - ey), (float)(cz - ez) };
	float up[3] = { (float)ux, (float)uy, (float)uz };
	float l = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	if (l != 0) { f[0] /= l; f[1] /= l; f[2] /= l; }
	float s[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
	l = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	if (l != 0) { s[0] /= l; s[1] /= l; s[2] /= l; }
	float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
	float m[16] = {
		s[0], u[0], -f[0], 0,
		s[1], u[1], -f[1], 0,
		s[2], u[2], -f[2], 0,
		0, 0, 0, 1 };
	glMultMatrixf(m);
	glTranslated(-ex, -ey, -ez);
}

GLint APIENTRY gluProject(GLdouble ox, GLdouble oy, GLdouble oz, const GLdouble *model, const GLdouble *proj, const GLint *view, GLdouble *wx, GLdouble *wy, GLdouble *wz)
{
	double in[4] = { ox, oy, oz, 1 }, e[4], c[4];
	xformd(e, model, in);
	xformd(c, proj, e);
	if (c[3] == 0) return GL_FALSE;
	c[0] /= c[3]; c[1] /= c[3]; c[2] /= c[3];
	*wx = view[0] + (c[0] * .5 + .5) * view[2];
	*wy = view[1] + (c[1] * .5 + .5) * view[3];
	*wz = c[2] * .5 + .5;
	return GL_TRUE;
}

GLint APIENTRY gluUnProject(GLdouble wx, GLdouble wy, GLdouble wz, const GLdouble *model, const GLdouble *proj, const GLint *view, GLdouble *ox, GLdouble *oy, GLdouble *oz)
{
	double m[16], inv[16];
	muld(m, proj, model);
	if (!invertd(m, inv)) return GL_FALSE;
	double in[4] = { (wx - view[0]) / view[2] * 2 - 1, (wy - view[1]) / view[3] * 2 - 1, wz * 2 - 1, 1 }, out[4];
	xformd(out, inv, in);
	if (out[3] == 0) return GL_FALSE;
	*ox = out[0] / out[3]; *oy = out[1] / out[3]; *oz = out[2] / out[3];
	return GL_TRUE;
}

GLint APIENTRY gluScaleImage(GLenum format, GLsizei wIn, GLsizei hIn, GLenum typeIn, const void *dataIn, GLsizei wOut, GLsizei hOut, GLenum typeOut, GLvoid *dataOut)
{
	if (typeIn != GL_UNSIGNED_BYTE || typeOut != GL_UNSIGNED_BYTE) return GLU_INVALID_ENUM;
	if (wIn <= 0 || hIn <= 0 || wOut <= 0 || hOut <= 0) return GLU_INVALID_VALUE;
	std::vector<uint8_t> in, out((size_t)wOut * hOut * 4);
	unpackToRGBA(format, wIn, hIn, dataIn, GL_RGBA, in, unpackAlignment());
	scaleRGBA(&in[0], wIn, hIn, &out[0], wOut, hOut);
	int comps = formatComponents(format);
	uint8_t *d = (uint8_t *)dataOut;
	for (size_t i = 0; i < (size_t)wOut * hOut; i++, d += comps)
	{
		const uint8_t *s = &out[i * 4];
		switch (format)
		{
		case GL_RGB: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; break;
		case GL_RGBA: memcpy(d, s, 4); break;
		case GL_BGR_EXT: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; break;
		case GL_BGRA_EXT: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; break;
		case GL_ALPHA: d[0] = s[3]; break;
		case GL_LUMINANCE_ALPHA: d[0] = s[0]; d[1] = s[3]; break;
		default: d[0] = s[0]; break;
		}
	}
	return 0;
}

GLint APIENTRY gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *data)
{
	if (target != GL_TEXTURE_2D || type != GL_UNSIGNED_BYTE) return GLU_INVALID_ENUM;
	if (width < 1 || height < 1) return GLU_INVALID_VALUE;
	GLenum base = baseFormat(internalFormat);
	std::vector<uint8_t> img;
	unpackToRGBA(format, width, height, data, base, img, unpackAlignment());
	int w = nearestPower(width), h = nearestPower(height);
	GLint maxSize = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
	if (maxSize > 0) { while (w > maxSize) w /= 2; while (h > maxSize) h /= 2; }
	if (w != width || h != height)
	{
		std::vector<uint8_t> scaled((size_t)w * h * 4);
		scaleRGBA(&img[0], width, height, &scaled[0], w, h);
		img.swap(scaled);
	}
	for (int level = 0;; level++)
	{
		texImageRGBA(level, base, w, h, &img[0]);
		if (w == 1 && h == 1) break;
		std::vector<uint8_t> next;
		int nw, nh;
		halveRGBA(&img[0], w, h, next, nw, nh);
		img.swap(next);
		w = nw; h = nh;
	}
	return 0;
}

GLUquadric * APIENTRY gluNewQuadric(void)
{
	GLUquadric *q = (GLUquadric *)malloc(sizeof(GLUquadric));
	if (!q) return 0;
	q->drawStyle = GLU_FILL;
	q->normals = GLU_SMOOTH;
	q->orientation = GLU_OUTSIDE;
	q->textureCoords = GL_FALSE;
	return q;
}

void APIENTRY gluDeleteQuadric(GLUquadric *q) { free(q); }
void APIENTRY gluQuadricDrawStyle(GLUquadric *q, GLenum d) { if (q) q->drawStyle = d; }
void APIENTRY gluQuadricNormals(GLUquadric *q, GLenum n) { if (q) q->normals = n; }
void APIENTRY gluQuadricOrientation(GLUquadric *q, GLenum o) { if (q) q->orientation = o; }
void APIENTRY gluQuadricTexture(GLUquadric *q, GLboolean t) { if (q) q->textureCoords = t; }

/* SGI GLU's sphere: z is the axis, slices around it from +y, stacks from +z down. Smooth
   normals and the outside orientation (the defaults, and all the game uses); flat normals
   are drawn smooth, inside orientation flips the normals only. */
void APIENTRY gluSphere(GLUquadric *q, GLdouble radius, GLint slices, GLint stacks)
{
	enum { CACHE = 240 };
	if (!q) return;
	if (slices >= CACHE) slices = CACHE - 1;
	if (stacks >= CACHE) stacks = CACHE - 1;
	if (slices < 2 || stacks < 1 || radius < 0) return;
	float sin1a[CACHE], cos1a[CACHE], sin1b[CACHE], cos1b[CACHE], sin2a[CACHE], cos2a[CACHE], sin2b[CACHE], cos2b[CACHE];
	bool normals = q->normals != GLU_NONE;
	float ns = q->orientation == GLU_INSIDE ? -1.f : 1.f;
	for (int i = 0; i < slices; i++)
	{
		double a = 2 * PI * i / slices;
		sin1a[i] = sin2a[i] = (float)sin(a);
		cos1a[i] = cos2a[i] = (float)cos(a);
	}
	for (int j = 0; j <= stacks; j++)
	{
		double a = PI * j / stacks;
		sin2b[j] = (float)sin(a) * ns;
		cos2b[j] = (float)cos(a) * ns;
		sin1b[j] = (float)(radius * sin(a));
		cos1b[j] = (float)(radius * cos(a));
	}
	sin1b[0] = 0;
	sin1b[stacks] = 0;
	sin1a[slices] = sin1a[0]; cos1a[slices] = cos1a[0];
	sin2a[slices] = sin2a[0]; cos2a[slices] = cos2a[0];

	if (q->drawStyle == GLU_FILL)
	{
		int start, finish;
		if (!q->textureCoords)
		{
			start = 1;
			finish = stacks - 1;
			/* the ends as fans */
			float st2 = sin1b[1], zh = cos1b[1], st3 = sin2b[1], ct3 = cos2b[1];
			if (normals) glNormal3f(sin2a[0] * sin2b[0], cos2a[0] * sin2b[0], cos2b[0]);
			glBegin(GL_TRIANGLE_FAN);
			glVertex3f(0, 0, (float)radius);
			for (int i = slices; i >= 0; i--)
			{
				if (normals) glNormal3f(sin2a[i] * st3, cos2a[i] * st3, ct3);
				glVertex3f(st2 * sin1a[i], st2 * cos1a[i], zh);
			}
			glEnd();
			st2 = sin1b[stacks - 1]; zh = cos1b[stacks - 1]; st3 = sin2b[stacks - 1]; ct3 = cos2b[stacks - 1];
			int k = stacks <= slices ? stacks : 0;	/* GLU indexes the slice cache by stacks here */
			if (normals) glNormal3f(sin2a[k] * sin2b[stacks], cos2a[k] * sin2b[stacks], cos2b[stacks]);
			glBegin(GL_TRIANGLE_FAN);
			glVertex3f(0, 0, (float)-radius);
			for (int i = 0; i <= slices; i++)
			{
				if (normals) glNormal3f(sin2a[i] * st3, cos2a[i] * st3, ct3);
				glVertex3f(st2 * sin1a[i], st2 * cos1a[i], zh);
			}
			glEnd();
		}
		else
		{
			start = 0;
			finish = stacks;
		}
		for (int j = start; j < finish; j++)
		{
			float zl = cos1b[j], zh = cos1b[j + 1], st1 = sin1b[j], st2 = sin1b[j + 1];
			float st3 = sin2b[j + 1], ct3 = cos2b[j + 1], st4 = sin2b[j], ct4 = cos2b[j];
			glBegin(GL_QUAD_STRIP);
			for (int i = 0; i <= slices; i++)
			{
				if (normals) glNormal3f(sin2a[i] * st3, cos2a[i] * st3, ct3);
				if (q->textureCoords) glTexCoord2f(1 - (float)i / slices, (float)(j + 1) / stacks);
				glVertex3f(st2 * sin1a[i], st2 * cos1a[i], zh);
				if (normals) glNormal3f(sin2a[i] * st4, cos2a[i] * st4, ct4);
				if (q->textureCoords) glTexCoord2f(1 - (float)i / slices, (float)j / stacks);
				glVertex3f(st1 * sin1a[i], st1 * cos1a[i], zl);
			}
			glEnd();
		}
	}
	else if (q->drawStyle == GLU_LINE || q->drawStyle == GLU_SILHOUETTE)
	{
		for (int j = 1; j < stacks; j++)
		{
			float st1 = sin1b[j], ct1 = cos1b[j], st2 = sin2b[j], ct2 = cos2b[j];
			glBegin(GL_LINE_STRIP);
			for (int i = 0; i <= slices; i++)
			{
				if (normals) glNormal3f(sin2a[i] * st2, cos2a[i] * st2, ct2);
				if (q->textureCoords) glTexCoord2f(1 - (float)i / slices, (float)j / stacks);
				glVertex3f(st1 * sin1a[i], st1 * cos1a[i], ct1);
			}
			glEnd();
		}
		for (int i = 0; i < slices; i++)
		{
			float st1 = sin1a[i], ct1 = cos1a[i], st2 = sin2a[i], ct2 = cos2a[i];
			glBegin(GL_LINE_STRIP);
			for (int j = 0; j <= stacks; j++)
			{
				if (normals) glNormal3f(st2 * sin2b[j], ct2 * sin2b[j], cos2b[j]);
				if (q->textureCoords) glTexCoord2f(1 - (float)i / slices, (float)j / stacks);
				glVertex3f(st1 * sin1b[j], ct1 * sin1b[j], cos1b[j]);
			}
			glEnd();
		}
	}
	/* GLU_POINT: points aren't drawn by the emulator */
}

const GLubyte * APIENTRY gluErrorString(GLenum error)
{
	switch (error)
	{
	case GL_NO_ERROR: return (const GLubyte *)"no error";
	case GL_INVALID_ENUM: return (const GLubyte *)"invalid enumerant";
	case GL_INVALID_VALUE: return (const GLubyte *)"invalid value";
	case GL_INVALID_OPERATION: return (const GLubyte *)"invalid operation";
	case GL_STACK_OVERFLOW: return (const GLubyte *)"stack overflow";
	case GL_STACK_UNDERFLOW: return (const GLubyte *)"stack underflow";
	case GL_OUT_OF_MEMORY: return (const GLubyte *)"out of memory";
	case GLU_INVALID_ENUM: return (const GLubyte *)"invalid enumerant";
	case GLU_INVALID_VALUE: return (const GLubyte *)"invalid value";
	case GLU_OUT_OF_MEMORY: return (const GLubyte *)"out of memory";
	default: return (const GLubyte *)"unknown error";
	}
}

} /* extern "C" */
