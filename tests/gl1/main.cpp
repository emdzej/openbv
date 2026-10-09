/*
	gl1test: six panels drawn through the GL 1.x emulator, plus checks logged as PASS/FAIL.
	  1 2D ortho: textures (a non-power-of-two RGB through gluBuild2DMipmaps, luminance), blending
	  2 a lit, textured gluSphere (a babo), colour material
	  3 linear fog over receding quads
	  4 display lists called with glCallLists and a list base (as CFont does)
	  5 client arrays: glDrawArrays with vertex, colour, texture and normal arrays
	  6 polygon mode GL_LINE, wide lines, line loops, alpha test, logic op invert
	Part of openbv, GPL-3.0-or-later.
*/

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>
#include "gl1.h"
#include "gasm.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

GASM_EXPORT("gasm_abi_version") int32_t abi_version(void) { return GASM_ABI_VERSION; }

static GLuint texChecker, texLum, texSkin, fontBase;
static GLUquadric *quad;
static unsigned frameNo;
static int failures;

static void check(const char *what, bool ok)
{
	if (frameNo) { if (!ok) failures++; return; }
	char msg[160];
	snprintf(msg, sizeof msg, "%s %s", ok ? "PASS" : "FAIL", what);
	gasm_log_str(msg);
	if (!ok) failures++;
}

static void makeTextures()
{
	/* 48x40 RGB, rows of 144 bytes (a multiple of 4: GL's default unpack alignment) */
	static unsigned char rgb[48 * 40 * 3];
	for (int y = 0; y < 40; y++)
		for (int x = 0; x < 48; x++)
		{
			unsigned char *p = rgb + (y * 48 + x) * 3;
			bool on = ((x / 6) + (y / 5)) & 1;
			p[0] = on ? 240 : 30; p[1] = on ? 200 : 60; p[2] = (unsigned char)(x * 5);
		}
	glGenTextures(1, &texChecker);
	glBindTexture(GL_TEXTURE_2D, texChecker);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gluBuild2DMipmaps(GL_TEXTURE_2D, 3, 48, 40, GL_RGB, GL_UNSIGNED_BYTE, rgb);

	static unsigned char lum[32 * 32];
	for (int i = 0; i < 32 * 32; i++) lum[i] = (unsigned char)(((i % 32) ^ (i / 32)) * 8);
	glGenTextures(1, &texLum);
	glBindTexture(GL_TEXTURE_2D, texLum);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gluBuild2DMipmaps(GL_TEXTURE_2D, 1, 32, 32, GL_LUMINANCE, GL_UNSIGNED_BYTE, lum);

	/* a 64x32 babo skin: stripes in BGRA (TGA order) */
	static unsigned char bgra[64 * 32 * 4];
	for (int y = 0; y < 32; y++)
		for (int x = 0; x < 64; x++)
		{
			unsigned char *p = bgra + (y * 64 + x) * 4;
			int band = (x / 8) & 1;
			p[0] = band ? 40 : 230; p[1] = band ? 40 : 120; p[2] = band ? 220 : 30; p[3] = 255;
			if (y < 4 || y >= 28) { p[0] = p[1] = p[2] = 250; }
		}
	glGenTextures(1, &texSkin);
	glBindTexture(GL_TEXTURE_2D, texSkin);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	gluBuild2DMipmaps(GL_TEXTURE_2D, 4, 64, 32, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bgra);

	/* glGetTexImage gives level 0 back: the skin's first texel, as RGB */
	unsigned char back[64 * 32 * 3];
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, back);
	check("glGetTexImage RGB from BGRA upload", back[0] == 250 && back[1] == 250 && back[2] == 250 &&
		  back[(10 * 64 + 9) * 3 + 0] == 220 && back[(10 * 64 + 9) * 3 + 2] == 40);
	unsigned char alpha[64 * 32];
	glGetTexImage(GL_TEXTURE_2D, 0, GL_ALPHA, GL_UNSIGNED_BYTE, alpha);
	check("glGetTexImage ALPHA", alpha[0] == 255 && alpha[64 * 32 - 1] == 255);
}

static void makeLists()
{
	/* glyph-like lists: 'A'..'Z' each a coloured box that advances, plus a colour change list */
	fontBase = glGenLists(128);
	for (int c = 'A'; c <= 'Z'; c++)
	{
		glNewList(fontBase + c, GL_COMPILE);
		glBegin(GL_QUADS);
		float h = .3f + .7f * ((c * 37) % 10) / 10.f;
		glVertex2f(0, 1 - h); glVertex2f(.8f, 1 - h); glVertex2f(.8f, 1); glVertex2f(0, 1);
		glEnd();
		glTranslatef(1, 0, 0);
		glEndList();
	}
	glNewList(fontBase + 1, GL_COMPILE);
	glColor3f(.25f, 1, .25f);
	glEndList();
	glNewList(fontBase + 2, GL_COMPILE);
	glColor3f(1, .25f, .25f);
	glEndList();
	check("glIsList", glIsList(fontBase + 'A') && !glIsList(fontBase + 300));
}

/* a panel: viewport and a cleared background, 3 x 2 across the drawable */
static void panel(int i, float r, float g, float b, int &x, int &y, int &w, int &h)
{
	int W = (int)gasm_gl_width(), H = (int)gasm_gl_height();
	w = W / 3; h = H / 2;
	x = (i % 3) * w; y = (1 - i / 3) * h;	/* panel 0 top left */
	glViewport(x, y, w, h);
	glScissor(x + 2, y + 2, w - 4, h - 4);
	glEnable(GL_SCISSOR_TEST);
	glClearColor(r, g, b, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_SCISSOR_TEST);
}

static void ortho(int w, int h)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, w, h, 0, -9999, 9999);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

static void panel2D(int w, int h)
{
	ortho(w, h);
	glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texChecker);
	glColor3f(1, 1, 1);
	glBegin(GL_QUADS);
	glTexCoord2i(0, 0); glVertex2i(20, 20);
	glTexCoord2i(1, 0); glVertex2i(180, 20);
	glTexCoord2i(1, 1); glVertex2i(180, 160);
	glTexCoord2i(0, 1); glVertex2i(20, 160);
	glEnd();
	glBindTexture(GL_TEXTURE_2D, texLum);
	glColor3f(.4f, .8f, 1);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(200, 20);
	glTexCoord2f(2, 0); glVertex2f(300, 20);
	glTexCoord2f(2, 2); glVertex2f(300, 120);
	glTexCoord2f(0, 2); glVertex2f(200, 120);
	glEnd();
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	const float cols[3][4] = { { 1, 0, 0, .5f }, { 0, 1, 0, .5f }, { 0, 0, 1, .5f } };
	for (int i = 0; i < 3; i++)
	{
		glColor4fv(cols[i]);
		glBegin(GL_QUADS);
		glVertex2i(60 + i * 50, 180); glVertex2i(160 + i * 50, 180); glVertex2i(160 + i * 50, 300); glVertex2i(60 + i * 50, 300);
		glEnd();
	}
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glBegin(GL_TRIANGLE_FAN);
	glColor4f(1, 1, 0, 1); glVertex2i(280, 240);
	glColor4f(1, .5f, 0, 0);
	for (int i = 0; i <= 16; i++) glVertex2f(280 + 30 * cosf(i * 6.2831853f / 16), 240 + 30 * sinf(i * 6.2831853f / 16));
	glEnd();
	glPopAttrib();
	check("glPopAttrib restores enables", !glIsEnabled(GL_BLEND) && !glIsEnabled(GL_TEXTURE_2D));
}

static void panelSphere(int w, int h)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(45, (double)w / h, .1, 100);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(0, -.2, 1.2, 0, 0, 0, 0, 1, 0);
	glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_CULL_FACE);
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	float pos[] = { 1, 1, 2, 1 }, amb[] = { .2f, .2f, .2f, 1 }, dif[] = { 1, 1, 1, 1 }, spe[] = { 1, 1, 1, 1 };
	glLightfv(GL_LIGHT0, GL_POSITION, pos);
	glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
	glLightfv(GL_LIGHT0, GL_SPECULAR, spe);
	float mspe[] = { .6f, .6f, .6f, 1 };
	glMaterialfv(GL_FRONT, GL_SPECULAR, mspe);
	glMateriali(GL_FRONT, GL_SHININESS, 40);
	glEnable(GL_COLOR_MATERIAL);
	glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
	glColor3f(1, 1, 1);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texSkin);
	glPushMatrix();
	glRotatef(30 + frameNo * 2.f, 0, 1, 0);
	glRotatef(-70, 1, 0, 0);
	glPolygonMode(GL_FRONT, GL_FILL);
	gluQuadricTexture(quad, GL_TRUE);
	gluSphere(quad, .25f, 16, 16);
	glPopMatrix();
	/* an untextured, smaller one: the fans at the poles */
	glDisable(GL_TEXTURE_2D);
	glColor3f(.3f, .9f, .3f);
	glPushMatrix();
	glTranslatef(.38f, .22f, 0);
	gluQuadricTexture(quad, GL_FALSE);
	gluSphere(quad, .1f, 8, 4);
	glPopMatrix();
	glPopAttrib();
}

static void panelFog(int w, int h)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(60, (double)w / h, .1, 100);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(0, 1.5, 3, 0, 0, -4, 0, 1, 0);
	glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_FOG);
	float fogc[] = { .55f, .6f, .7f, 1 };
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glFogfv(GL_FOG_COLOR, fogc);
	glFogf(GL_FOG_START, 2);
	glFogf(GL_FOG_END, 12);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texChecker);
	glColor3f(1, 1, 1);
	for (int i = 0; i < 8; i++)
	{
		float z = -i * 1.5f;
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex3f(-1.5f, 0, z);
		glTexCoord2f(1, 0); glVertex3f(1.5f, 0, z);
		glTexCoord2f(1, 1); glVertex3f(1.5f, 0, z - 1.3f);
		glTexCoord2f(0, 1); glVertex3f(-1.5f, 0, z - 1.3f);
		glEnd();
	}
	glPopAttrib();
}

static void panelLists(int w, int h)
{
	ortho(w, h);
	glPushMatrix();
	glTranslatef(20, 30, 0);
	glScalef(24, 24, 24);
	glPushMatrix();
	glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT | GL_LIST_BIT);
	glListBase(fontBase);
	glColor3f(1, 1, 1);
	const char *t1 = "HELLO\x01WORLD";
	glCallLists((GLsizei)strlen(t1), GL_UNSIGNED_BYTE, t1);
	glPopAttrib();
	glPopMatrix();
	glTranslatef(0, 2, 0);
	glPushAttrib(GL_CURRENT_BIT | GL_LIST_BIT);
	glListBase(fontBase);
	glColor3f(1, 1, 0);
	const char *t2 = "BABO\x02VIOLENT";
	glCallLists((GLsizei)strlen(t2), GL_UNSIGNED_BYTE, t2);
	glPopAttrib();
	glPopMatrix();
	float c[4];
	glGetFloatv(GL_CURRENT_COLOR, c);
	check("GL_CURRENT_BIT restores the colour after lists", c[0] == 1 && c[1] == 1 && c[2] == 1);
	/* a list compiled with a nested call and a matrix, called twice */
	static GLuint nested;
	if (!nested)
	{
		nested = glGenLists(1);
		glNewList(nested, GL_COMPILE);
		glPushMatrix();
		glColor3f(.6f, .3f, 1);
		glCallList(fontBase + 'M');
		glCallList(fontBase + 'W');
		glPopMatrix();
		glEndList();
	}
	glPushMatrix();
	glTranslatef(30, 200, 0);
	glScalef(40, 40, 1);
	glCallList(nested);
	glTranslatef(3, 0, 0);
	glCallList(nested);
	glPopMatrix();
}

static void panelArrays(int w, int h)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(50, (double)w / h, .1, 100);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0, 0, -3);
	glRotatef(25, 1, 0, 0);
	glRotatef(frameNo * 3.f + 20, 0, 0, 1);
	/* a pyramid as triangles with interleaved arrays like CMaterial's SVertex */
	struct V { float x, y, z, u, v, nx, ny, nz, r, g, b, a; };
	static V verts[12];
	const float apex[3] = { 0, 0, 1 }, base[4][3] = { { -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { -1, 1, 0 } };
	for (int f = 0; f < 4; f++)
	{
		const float *p[3] = { base[f], base[(f + 1) % 4], apex };
		for (int k = 0; k < 3; k++)
		{
			V &v = verts[f * 3 + k];
			v.x = p[k][0]; v.y = p[k][1]; v.z = p[k][2];
			v.u = k == 2 ? .5f : (float)k; v.v = k == 2 ? 1.f : 0.f;
			v.nx = 0; v.ny = 0; v.nz = 1;
			v.r = f == 0 ? 1.f : .3f; v.g = f == 1 ? 1.f : .3f; v.b = f == 2 ? 1.f : .3f; v.a = 1;
		}
	}
	glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texLum);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(3, GL_FLOAT, sizeof(V), &verts[0].x);
	glTexCoordPointer(2, GL_FLOAT, sizeof(V), &verts[0].u);
	glNormalPointer(GL_FLOAT, sizeof(V), &verts[0].nx);
	glColorPointer(4, GL_FLOAT, sizeof(V), &verts[0].r);
	glDrawArrays(GL_TRIANGLES, 0, 12);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glPopAttrib();
}

static void panelLines(int w, int h)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(45, (double)w / h, .1, 100);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(-.35f, .1f, -1.3f);
	glRotatef(frameNo * 2.f, 0, 0, 1);
	glPushAttrib(GL_POLYGON_BIT | GL_LINE_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT);
	glEnable(GL_CULL_FACE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glLineWidth(1);
	glColor3f(.5f, 1, .5f);
	gluSphere(quad, .3f, 12, 8);
	glPopAttrib();

	ortho(w, h);
	glPushAttrib(GL_LINE_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT);
	glLineWidth(3);
	glColor3f(1, .8f, .2f);
	glBegin(GL_LINE_LOOP);
	glVertex2i(180, 30); glVertex2i(300, 50); glVertex2i(280, 160); glVertex2i(200, 120);
	glEnd();
	glLineWidth(2);
	glColor3f(1, 1, 1);
	glBegin(GL_LINES);
	for (int i = 0; i < 6; i++) { glVertex2i(180 + i * 20, 190); glVertex2i(190 + i * 20, 240); }
	glEnd();
	/* alpha test: a gradient quad, the lower-alpha half cut */
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, .5f);
	glBegin(GL_QUADS);
	glColor4f(1, 0, 1, 0); glVertex2i(20, 250);
	glColor4f(1, 0, 1, 1); glVertex2i(150, 250);
	glColor4f(1, 0, 1, 1); glVertex2i(150, 300);
	glColor4f(1, 0, 1, 0); glVertex2i(20, 300);
	glEnd();
	glDisable(GL_ALPHA_TEST);
	/* logic op invert over the panel's lower right */
	glEnable(GL_COLOR_LOGIC_OP);
	glLogicOp(GL_INVERT);
	glBegin(GL_QUADS);
	glVertex2i(240, 140); glVertex2i(310, 140); glVertex2i(310, 300); glVertex2i(240, 300);
	glEnd();
	glDisable(GL_COLOR_LOGIC_OP);
	glPopAttrib();
}

static void checks()
{
	/* gluProject / gluUnProject round trip */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(60, 4.0 / 3, 1, 50);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(0, 0, 7, 0, 0, 0, 0, 1, 0);
	double mv[16], pr[16];
	GLint vp[4] = { 0, 0, 640, 480 };
	glGetDoublev(GL_MODELVIEW_MATRIX, mv);
	glGetDoublev(GL_PROJECTION_MATRIX, pr);
	double wx, wy, wz, ox, oy, oz;
	gluProject(1.5, -2, .5, mv, pr, vp, &wx, &wy, &wz);
	gluUnProject(wx, wy, wz, mv, pr, vp, &ox, &oy, &oz);
	check("gluProject/gluUnProject round trip", fabs(ox - 1.5) < 1e-3 && fabs(oy + 2) < 1e-3 && fabs(oz - .5) < 1e-3);
	check("gluLookAt eye at origin", fabs(mv[14] + 7) < 1e-5 && fabs(mv[0] - 1) < 1e-6);
	glPushMatrix();
	glTranslatef(1, 2, 3);
	glPopMatrix();
	glGetDoublev(GL_MODELVIEW_MATRIX, mv);
	check("glPushMatrix/glPopMatrix", fabs(mv[12]) < 1e-6 && fabs(mv[14] + 7) < 1e-5);
	GLint v[4];
	glGetIntegerv(GL_VIEWPORT, v);
	check("default viewport is the drawable", v[2] == (GLint)gasm_gl_width() && v[3] == (GLint)gasm_gl_height());
	check("no GL error", glGetError() == GL_NO_ERROR);
}

GASM_EXPORT("gasm_init") int32_t init(void)
{
	gasm_set_frame_rate(60);
	gl1_begin_frame((int)gasm_gl_width(), (int)gasm_gl_height());
	quad = gluNewQuadric();
	checks();
	makeTextures();
	makeLists();
	return 0;
}

GASM_EXPORT("gasm_frame") void frame(void)
{
	gl1_begin_frame((int)gasm_gl_width(), (int)gasm_gl_height());
	int x, y, w, h;
	panel(0, .15f, .15f, .2f, x, y, w, h); panel2D(w, h);
	panel(1, .1f, .1f, .12f, x, y, w, h); panelSphere(w, h);
	panel(2, .55f, .6f, .7f, x, y, w, h); panelFog(w, h);
	panel(3, .12f, .2f, .15f, x, y, w, h); panelLists(w, h);
	panel(4, .2f, .15f, .12f, x, y, w, h); panelArrays(w, h);
	panel(5, .1f, .1f, .25f, x, y, w, h); panelLines(w, h);
	glViewport(0, 0, (GLsizei)gasm_gl_width(), (GLsizei)gasm_gl_height());
	if (frameNo == 0)
	{
		char msg[64];
		snprintf(msg, sizeof msg, "gl1test: %d failures, %ux%u", failures, gasm_gl_width(), gasm_gl_height());
		gasm_log_str(msg);
	}
	gl1_end_frame();
	if (frameNo == 1)
	{
		uint32_t e = gasm_gl_get_error();
		char msg[64];
		snprintf(msg, sizeof msg, "%s no gasm:gl error (0x%x)", e ? "FAIL" : "PASS", e);
		gasm_log_str(msg);
	}
	frameNo++;
}
