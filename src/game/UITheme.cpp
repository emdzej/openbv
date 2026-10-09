/*
	openbv: the menus' look (see UITheme.h).

	Rounded rectangles are nine-slice quads over two small alpha textures made on first use: a
	filled rounded square and its outline, both with 32-pixel corners. Drawing maps the corner to the
	radius asked for, so one texture serves every size.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#ifndef CONSOLE

#include "UITheme.h"
#include "Zeven.h"
#include <math.h>
#include <string.h>

namespace ui
{
	static UIColor rgb(int hex, float a) { return UIColor{((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, a}; }

	const UIColor panel       = rgb(0x0f141b, .82f);
	const UIColor panelLine   = rgb(0xffffff, .08f);
	const UIColor field       = rgb(0x070a0e, .80f);
	const UIColor fieldLine   = rgb(0xffffff, .12f);
	const UIColor button      = rgb(0x1c2430, .95f);
	const UIColor buttonHover = rgb(0x2a3646, .95f);
	const UIColor buttonDown  = rgb(0x141a23, .95f);
	const UIColor buttonLine  = rgb(0xffffff, .07f);
	const UIColor accent      = rgb(0xff8c1a, 1);
	const UIColor accentHover = rgb(0xffa448, 1);
	const UIColor accentDim   = rgb(0xff8c1a, .18f);
	const UIColor text        = rgb(0xe9edf2, 1);
	const UIColor textMuted   = rgb(0x8b96a5, 1);
	const UIColor textOnAccent = rgb(0x16120c, 1);
	const UIColor hoverTint   = rgb(0xffffff, .06f);
	const UIColor scrim       = rgb(0x05080c, .45f);
	const UIColor danger      = rgb(0xe5484d, 1);

	UIColor withAlpha(const UIColor & c, float a) { return UIColor{c.r, c.g, c.b, a}; }
	UIColor mix(const UIColor & a, const UIColor & b, float t)
	{
		return UIColor{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
	}
	void setColor(const UIColor & c) { glColor4f(c.r, c.g, c.b, c.a); }

	static const int TEX = 64, CORNER = 32;
	static GLuint texFill = 0, texLine = 0;

	// Coverage of the rounded square (corner radius CORNER) at pixel (x, y), 4x4 supersampled.
	// line: only the band within thickness of the edge.
	static float coverage(int x, int y, bool line)
	{
		const float thickness = 6.4f;   // 1 unit at the usual 5-unit radius
		int hits = 0;
		for (int sy = 0; sy < 4; sy++)
			for (int sx = 0; sx < 4; sx++)
			{
				float px = x + (sx + .5f) / 4, py = y + (sy + .5f) / 4;
				// distance outside the rounded square's inner rectangle, per axis
				float cx = px < CORNER ? CORNER - px : px > TEX - CORNER ? px - (TEX - CORNER) : 0;
				float cy = py < CORNER ? CORNER - py : py > TEX - CORNER ? py - (TEX - CORNER) : 0;
				float d = sqrtf(cx * cx + cy * cy);   // from the inner rectangle
				float inside = CORNER - d;            // depth inside the shape
				bool in = inside >= 0 && (!line || inside < thickness);
				if (in) hits++;
			}
		return hits / 16.0f;
	}

	static GLuint makeTexture(bool line)
	{
		static unsigned char px[TEX * TEX * 4];
		for (int y = 0; y < TEX; y++)
			for (int x = 0; x < TEX; x++)
			{
				unsigned char *p = px + (y * TEX + x) * 4;
				p[0] = p[1] = p[2] = 255;
				p[3] = (unsigned char)(coverage(x, y, line) * 255.0f + .5f);
			}
		GLuint t;
		glGenTextures(1, &t);
		glBindTexture(GL_TEXTURE_2D, t);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX, TEX, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
		return t;
	}

	static void nineSlice(GLuint tex, float x, float y, float w, float h, float r)
	{
		if (r * 2 > w) r = w / 2;
		if (r * 2 > h) r = h / 2;
		float xs[4] = {x, x + r, x + w - r, x + w};
		float ys[4] = {y, y + r, y + h - r, y + h};
		float us[4] = {0, (float)CORNER / TEX, 1 - (float)CORNER / TEX, 1};
		glPushAttrib(GL_ENABLE_BIT);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, tex);
		glBegin(GL_QUADS);
		for (int j = 0; j < 3; j++)
			for (int i = 0; i < 3; i++)
			{
				glTexCoord2f(us[i], us[j]);         glVertex2f(xs[i], ys[j]);
				glTexCoord2f(us[i], us[j + 1]);     glVertex2f(xs[i], ys[j + 1]);
				glTexCoord2f(us[i + 1], us[j + 1]); glVertex2f(xs[i + 1], ys[j + 1]);
				glTexCoord2f(us[i + 1], us[j]);     glVertex2f(xs[i + 1], ys[j]);
			}
		glEnd();
		glPopAttrib();
	}

	void fillRect(float x, float y, float w, float h, const UIColor & fill, float radius)
	{
		if (fill.a <= 0 || w <= 0 || h <= 0) return;
		glPushAttrib(GL_CURRENT_BIT);
		setColor(fill);
		if (radius <= 0) bar(x, y, w, h, fill);
		else
		{
			if (!texFill) texFill = makeTexture(false);
			nineSlice(texFill, x, y, w, h, radius);
		}
		glPopAttrib();
	}

	void lineRect(float x, float y, float w, float h, const UIColor & line, float radius)
	{
		if (line.a <= 0 || w <= 0 || h <= 0) return;
		glPushAttrib(GL_CURRENT_BIT);
		setColor(line);
		if (!texLine) texLine = makeTexture(true);
		// the outline's band is a fifth of the corner: 1 unit at radius 5
		nineSlice(texLine, x, y, w, h, radius > 0 ? radius : 5);
		glPopAttrib();
	}

	void rect(float x, float y, float w, float h, const UIColor & fill, const UIColor & line, float radius)
	{
		fillRect(x, y, w, h, fill, radius);
		lineRect(x, y, w, h, line, radius);
	}

	void bar(float x, float y, float w, float h, const UIColor & c)
	{
		if (c.a <= 0 || w <= 0 || h <= 0) return;
		glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT);
		glDisable(GL_TEXTURE_2D);
		setColor(c);
		glBegin(GL_QUADS);
			glVertex2f(x, y);
			glVertex2f(x, y + h);
			glVertex2f(x + w, y + h);
			glVertex2f(x + w, y);
		glEnd();
		glPopAttrib();
	}

	// A stroke from (x0, y0) to (x1, y1), t units thick, as a quad.
	static void stroke(float x0, float y0, float x1, float y1, float t)
	{
		float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
		if (l <= 0) return;
		float nx = -dy / l * t / 2, ny = dx / l * t / 2;
		glVertex2f(x0 + nx, y0 + ny);
		glVertex2f(x0 - nx, y0 - ny);
		glVertex2f(x1 - nx, y1 - ny);
		glVertex2f(x1 + nx, y1 + ny);
	}

	void checkMark(float x, float y, float s, const UIColor & c)
	{
		glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT);
		glDisable(GL_TEXTURE_2D);
		setColor(c);
		float t = s * .14f;
		glBegin(GL_QUADS);
			stroke(x + s * .24f, y + s * .52f, x + s * .43f, y + s * .70f, t);
			stroke(x + s * .40f, y + s * .71f, x + s * .77f, y + s * .30f, t);
		glEnd();
		glPopAttrib();
	}

	void vignette(float w, float h)
	{
		glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT);
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		bar(0, 0, w, h, scrim);
		const float edge = .6f;
		float ex = w * .22f, ey = h * .28f;
		glBegin(GL_QUADS);
			// left, right
			glColor4f(0, 0, 0, edge); glVertex2f(0, 0); glVertex2f(0, h);
			glColor4f(0, 0, 0, 0);    glVertex2f(ex, h); glVertex2f(ex, 0);
			glColor4f(0, 0, 0, 0);    glVertex2f(w - ex, 0); glVertex2f(w - ex, h);
			glColor4f(0, 0, 0, edge); glVertex2f(w, h); glVertex2f(w, 0);
			// top, bottom
			glColor4f(0, 0, 0, edge); glVertex2f(0, 0);
			glColor4f(0, 0, 0, 0);    glVertex2f(0, ey); glVertex2f(w, ey);
			glColor4f(0, 0, 0, edge); glVertex2f(w, 0);
			glColor4f(0, 0, 0, 0);    glVertex2f(0, h - ey);
			glColor4f(0, 0, 0, edge); glVertex2f(0, h); glVertex2f(w, h);
			glColor4f(0, 0, 0, 0);    glVertex2f(w, h - ey);
		glEnd();
		glPopAttrib();
	}
}

#endif
