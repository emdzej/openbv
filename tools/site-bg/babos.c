/*
	babos: two babos shooting each other, the website's background (openbv.emdzej.pl).

	Top-down as in the game: a red and a blue babo roll around, dodge, aim at each other and fire
	bursts (muzzle flash, tracers, sparks, blood on the ground), now and then lob a grenade. A babo
	that runs out of health pops into gibs and rolls back in a moment later. Everything is drawn
	here, procedurally: no game data.

	Params `w`, `h`: the frame size (default 640x360), everything scales with the height. `fg`: the
	colour of the guns and grenades as RRGGBB (default f2f4f8, light on dark; a light page passes a
	dark one). 2D frames (RGBA, straight alpha) with a transparent background, so the page shows
	through. Own random numbers and plain float maths: the same frames on every runner.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#include "gasm.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRACERS 32
#define MAX_PARTS 160
#define MAX_SPLATS 48
#define TWO_PI 6.2831853f

typedef struct { float x, y; } V2;

typedef struct
{
	float x, y, vx, vy;
	float tx, ty;              /* where it's heading */
	int retarget;              /* frames until a new heading */
	float aim;                 /* gun angle */
	float m[9];                /* the ball's orientation (rotation matrix, row-major) */
	int hp;
	int dead;                  /* frames until respawn, 0: alive */
	int burst, cooldown;       /* shots left in this burst, frames to the next shot */
	int flash;                 /* muzzle flash frames */
	int nade;                  /* frames until it may throw a grenade */
	unsigned char color[3];
} Babo;

typedef struct { float x0, y0, x1, y1; int life; } Tracer;
typedef struct { float x, y, vx, vy, size; int life, max; unsigned char c[3]; float drag; } Part;
typedef struct { float x, y, r; int life; } Splat;
typedef struct { float x, y, z, vx, vy, vz; int fuse; int owner; int active; } Grenade;

static int W = 640, H = 360;
static unsigned char *fb;
static unsigned char fg[3] = {0xf2, 0xf4, 0xf8};
static unsigned rng = 20111221u;
static float R;                /* babo radius */
static Babo babo[2];
static Tracer tracers[MAX_TRACERS];
static Part parts[MAX_PARTS];
static Splat splats[MAX_SPLATS];
static Grenade grenade;
static int explosion, explosionX, explosionY;

static float rnd(void)
{
	rng = rng * 1664525u + 1013904223u;
	return (float)(rng >> 8) / 16777216.0f;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* --- drawing: "over" with straight alpha */

static void blend(int x, int y, const unsigned char *c, float a)
{
	if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
	unsigned char *p = fb + ((size_t)y * W + x) * 4;
	float under = p[3] / 255.0f;
	float out = a + under * (1.0f - a);
	if (out <= 0) return;
	for (int k = 0; k < 3; k++) p[k] = (unsigned char)((c[k] * a + p[k] * under * (1.0f - a)) / out);
	p[3] = (unsigned char)(out * 255.0f);
}

static void disc(float cx, float cy, float r, const unsigned char *c, float alpha)
{
	int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
	int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
		{
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float a = clampf(r - sqrtf(dx * dx + dy * dy) + 0.5f, 0, 1) * alpha;
			if (a > 0) blend(x, y, c, a);
		}
}

/* a soft blob: alpha falls off to the edge (shadows, smoke) */
static void blob(float cx, float cy, float rx, float ry, const unsigned char *c, float alpha)
{
	int x0 = (int)floorf(cx - rx), x1 = (int)ceilf(cx + rx);
	int y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
		{
			float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
			float d = dx * dx + dy * dy;
			if (d < 1) blend(x, y, c, (1 - d) * alpha);
		}
}

/* a capsule from (x0,y0) to (x1,y1) of half-width hw */
static void line(float x0, float y0, float x1, float y1, float hw, const unsigned char *c, float alpha)
{
	int bx0 = (int)floorf(fminf(x0, x1) - hw - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + hw + 1);
	int by0 = (int)floorf(fminf(y0, y1) - hw - 1), by1 = (int)ceilf(fmaxf(y0, y1) + hw + 1);
	float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
	for (int y = by0; y <= by1; y++)
		for (int x = bx0; x <= bx1; x++)
		{
			float px = x + 0.5f - x0, py = y + 0.5f - y0;
			float t = len2 > 0 ? clampf((px * dx + py * dy) / len2, 0, 1) : 0;
			float ex = px - t * dx, ey = py - t * dy;
			float a = clampf(hw - sqrtf(ex * ex + ey * ey) + 0.5f, 0, 1) * alpha;
			if (a > 0) blend(x, y, c, a);
		}
}

/* A babo: a shaded sphere lit from the top left, with a band around it that turns as it rolls. */
static void drawBabo(const Babo *b)
{
	static const unsigned char white[3] = {250, 250, 250}, dark[3] = {20, 22, 28};
	int x0 = (int)floorf(b->x - R - 1), x1 = (int)ceilf(b->x + R + 1);
	int y0 = (int)floorf(b->y - R - 1), y1 = (int)ceilf(b->y + R + 1);
	const float lx = -0.45f, ly = -0.55f, lz = 0.70f;
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
		{
			float nx = (x + 0.5f - b->x) / R, ny = (y + 0.5f - b->y) / R;
			float d2 = nx * nx + ny * ny;
			float edge = clampf((1.0f - sqrtf(d2)) * R + 0.5f, 0, 1);
			if (edge <= 0) continue;
			float nz = d2 < 1 ? sqrtf(1 - d2) : 0;
			/* the surface point in the ball's own frame (transpose of the orientation) */
			const float *m = b->m;
			float bxv = m[0] * nx + m[3] * ny + m[6] * nz;
			float byv = m[1] * nx + m[4] * ny + m[7] * nz;
			float bzv = m[2] * nx + m[5] * ny + m[8] * nz;
			unsigned char c[3];
			const unsigned char *base = b->color;
			if (fabsf(bzv) < 0.22f) base = white;                       /* the band */
			else if (bxv > 0.55f && fabsf(byv) < 0.28f) base = dark;     /* a patch, so it reads as rolling */
			float diff = clampf(nx * lx + ny * ly + nz * lz, 0, 1);
			float shade = 0.35f + 0.75f * diff;
			float spec = diff > 0.93f ? (diff - 0.93f) / 0.07f : 0;
			for (int k = 0; k < 3; k++)
			{
				float v = base[k] * shade + 255.0f * spec * 0.6f;
				c[k] = (unsigned char)clampf(v, 0, 255);
			}
			blend(x, y, c, edge);
		}
}

/* --- the game */

static void identity(float *m) { memset(m, 0, 9 * sizeof(float)); m[0] = m[4] = m[8] = 1; }

/* Roll by the distance moved, about the axis across the motion (as Player.cpp does), then
   keep the matrix orthonormal. */
static void roll(Babo *b, float dx, float dy)
{
	float dist = sqrtf(dx * dx + dy * dy);
	if (dist < 1e-4f) return;
	float ax = -dy / dist, ay = dx / dist;   /* cross(motion, z), screen y points down */
	float ang = dist / R;
	float c = cosf(ang), s = sinf(ang), t = 1 - c;
	float r[9] = {
		t * ax * ax + c, t * ax * ay, s * ay,
		t * ax * ay, t * ay * ay + c, -s * ax,
		-s * ay, s * ax, c,
	};
	float n[9];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			n[i * 3 + j] = r[i * 3] * b->m[j] + r[i * 3 + 1] * b->m[3 + j] + r[i * 3 + 2] * b->m[6 + j];
	/* Gram-Schmidt on the rows */
	float *a0 = n, *a1 = n + 3, *a2 = n + 6;
	float l = sqrtf(a0[0] * a0[0] + a0[1] * a0[1] + a0[2] * a0[2]);
	for (int k = 0; k < 3; k++) a0[k] /= l;
	float d = a0[0] * a1[0] + a0[1] * a1[1] + a0[2] * a1[2];
	for (int k = 0; k < 3; k++) a1[k] -= d * a0[k];
	l = sqrtf(a1[0] * a1[0] + a1[1] * a1[1] + a1[2] * a1[2]);
	for (int k = 0; k < 3; k++) a1[k] /= l;
	a2[0] = a0[1] * a1[2] - a0[2] * a1[1];
	a2[1] = a0[2] * a1[0] - a0[0] * a1[2];
	a2[2] = a0[0] * a1[1] - a0[1] * a1[0];
	memcpy(b->m, n, sizeof(n));
}

/* each babo keeps to its own side, with a margin from the edges */
static void pickTarget(Babo *b, int side)
{
	float lo = side == 0 ? 0.06f : 0.56f, hi = side == 0 ? 0.44f : 0.94f;
	b->tx = W * (lo + (hi - lo) * rnd());
	b->ty = H * (0.15f + 0.7f * rnd());
	b->retarget = 40 + (int)(rnd() * 90);
}

static void spawn(Babo *b, int side)
{
	pickTarget(b, side);
	b->x = b->tx; b->y = b->ty;
	b->vx = b->vy = 0;
	b->hp = 100;
	b->dead = 0;
	b->burst = 0;
	b->cooldown = 30 + (int)(rnd() * 30);
	b->nade = 200 + (int)(rnd() * 300);
	identity(b->m);
	pickTarget(b, side);
}

static void addPart(float x, float y, float vx, float vy, float size, int life, const unsigned char *c, float drag)
{
	for (int i = 0; i < MAX_PARTS; i++)
		if (parts[i].life <= 0)
		{
			Part *p = &parts[i];
			p->x = x; p->y = y; p->vx = vx; p->vy = vy; p->size = size;
			p->life = p->max = life; p->drag = drag;
			memcpy(p->c, c, 3);
			return;
		}
}

static void addSplat(float x, float y, float r)
{
	int oldest = 0;
	for (int i = 0; i < MAX_SPLATS; i++)
	{
		if (splats[i].life <= 0) { oldest = i; break; }
		if (splats[i].life < splats[oldest].life) oldest = i;
	}
	splats[oldest].x = x; splats[oldest].y = y; splats[oldest].r = r; splats[oldest].life = 240;
}

static void kill(int who)
{
	static const unsigned char blood[3] = {150, 12, 18};
	Babo *b = &babo[who];
	b->dead = 70;
	for (int i = 0; i < 12; i++)
	{
		float a = rnd() * TWO_PI, s = H * (0.004f + 0.01f * rnd());
		addPart(b->x, b->y, cosf(a) * s, sinf(a) * s, R * (0.12f + 0.14f * rnd()), 35 + (int)(rnd() * 25), i % 3 == 0 ? blood : b->color, 0.92f);
	}
	for (int i = 0; i < 3; i++) addSplat(b->x + (rnd() - 0.5f) * R * 1.6f, b->y + (rnd() - 0.5f) * R * 1.6f, R * (0.2f + 0.25f * rnd()));
}

static void damage(int who, int amount, float x, float y)
{
	static const unsigned char blood[3] = {170, 16, 22};
	Babo *b = &babo[who];
	if (b->dead) return;
	b->hp -= amount;
	for (int i = 0; i < 4; i++)
	{
		float a = rnd() * TWO_PI, s = H * 0.006f * rnd();
		addPart(x, y, cosf(a) * s, sinf(a) * s, R * 0.12f, 18, blood, 0.85f);
	}
	if (rnd() < 0.25f) addSplat(x + (rnd() - 0.5f) * R, y + (rnd() - 0.5f) * R, R * (0.1f + 0.12f * rnd()));
	if (b->hp <= 0) kill(who);
}

static void shoot(int who)
{
	static const unsigned char spark[3] = {255, 214, 120};
	Babo *a = &babo[who], *t = &babo[1 - who];
	float mx = a->x + cosf(a->aim) * R * 1.7f, my = a->y + sinf(a->aim) * R * 1.7f;
	/* spread: most shots near the target, some wide */
	float dx = t->x - mx, dy = t->y - my, dist = sqrtf(dx * dx + dy * dy);
	float spread = (rnd() - 0.5f) * 0.32f;
	float ang = a->aim + spread;
	float ex = mx + cosf(ang) * dist, ey = my + sinf(ang) * dist;
	float missX = ex - t->x, missY = ey - t->y;
	int hit = !t->dead && missX * missX + missY * missY < R * R;
	if (!hit) { ex = mx + cosf(ang) * (dist + H * 0.4f); ey = my + sinf(ang) * (dist + H * 0.4f); }
	for (int i = 0; i < MAX_TRACERS; i++)
		if (tracers[i].life <= 0)
		{
			tracers[i].x0 = mx; tracers[i].y0 = my; tracers[i].x1 = ex; tracers[i].y1 = ey; tracers[i].life = 7;
			break;
		}
	a->flash = 3;
	/* a shell flies out to the side */
	float side = a->aim + 1.5708f;
	addPart(a->x + cosf(side) * R * 0.6f, a->y + sinf(side) * R * 0.6f, cosf(side) * H * 0.006f, sinf(side) * H * 0.006f, R * 0.08f, 30, spark, 0.9f);
	if (hit) damage(1 - who, 4 + (int)(rnd() * 4), ex, ey);
}

static void throwGrenade(int who)
{
	Babo *a = &babo[who], *t = &babo[1 - who];
	float dx = t->x - a->x + (rnd() - 0.5f) * R * 3, dy = t->y - a->y + (rnd() - 0.5f) * R * 3;
	int flight = 60;
	grenade.active = 1;
	grenade.owner = who;
	grenade.x = a->x; grenade.y = a->y; grenade.z = R;
	grenade.vx = dx / flight; grenade.vy = dy / flight;
	grenade.vz = H * 0.012f;
	grenade.fuse = 80;
}

static void explode(void)
{
	static const unsigned char fire[3] = {255, 170, 60}, smoke[3] = {90, 90, 96};
	grenade.active = 0;
	explosion = 24;
	explosionX = (int)grenade.x; explosionY = (int)grenade.y;
	for (int i = 0; i < 18; i++)
	{
		float a = rnd() * TWO_PI, s = H * (0.002f + 0.01f * rnd());
		addPart(grenade.x, grenade.y, cosf(a) * s, sinf(a) * s, R * (0.3f + 0.4f * rnd()), 40 + (int)(rnd() * 30), i & 1 ? fire : smoke, 0.92f);
	}
	for (int k = 0; k < 2; k++)
	{
		float dx = babo[k].x - grenade.x, dy = babo[k].y - grenade.y;
		float d = sqrtf(dx * dx + dy * dy);
		if (d < R * 3) damage(k, (int)(50 * (1 - d / (R * 3))) + 10, babo[k].x, babo[k].y);
	}
}

static void updateBabo(int who)
{
	Babo *b = &babo[who], *t = &babo[1 - who];
	if (b->dead)
	{
		if (--b->dead == 0) spawn(b, who);
		return;
	}
	if (--b->retarget <= 0) pickTarget(b, who);
	float speed = H * 0.0045f;
	float dx = b->tx - b->x, dy = b->ty - b->y, d = sqrtf(dx * dx + dy * dy);
	if (d < R) pickTarget(b, who);
	else { b->vx += dx / d * speed * 0.08f; b->vy += dy / d * speed * 0.08f; }
	float v = sqrtf(b->vx * b->vx + b->vy * b->vy);
	if (v > speed) { b->vx *= speed / v; b->vy *= speed / v; }
	b->vx *= 0.97f; b->vy *= 0.97f;
	b->x = clampf(b->x + b->vx, R, W - R);
	b->y = clampf(b->y + b->vy, R, H - R);
	roll(b, b->vx, b->vy);

	/* the gun turns towards the other babo, a little behind */
	float want = t->dead ? b->aim : atan2f(t->y - b->y, t->x - b->x);
	float diff = want - b->aim;
	while (diff > 3.14159265f) diff -= TWO_PI;
	while (diff < -3.14159265f) diff += TWO_PI;
	b->aim += diff * 0.18f;

	if (b->flash > 0) b->flash--;
	if (t->dead) { b->burst = 0; return; }
	if (b->cooldown > 0) b->cooldown--;
	else if (b->burst > 0)
	{
		shoot(who);
		b->burst--;
		b->cooldown = b->burst ? 5 : 30 + (int)(rnd() * 50);
	}
	else b->burst = 4 + (int)(rnd() * 6);
	if (--b->nade <= 0)
	{
		b->nade = 300 + (int)(rnd() * 400);
		if (!grenade.active) throwGrenade(who);
	}
}

static void step(void)
{
	updateBabo(0);
	updateBabo(1);
	if (grenade.active)
	{
		grenade.x += grenade.vx; grenade.y += grenade.vy; grenade.z += grenade.vz;
		grenade.vz -= H * 0.0004f;
		if (grenade.z < 0) { grenade.z = 0; grenade.vz = -grenade.vz * 0.4f; grenade.vx *= 0.6f; grenade.vy *= 0.6f; }
		if (--grenade.fuse <= 0) explode();
	}
	for (int i = 0; i < MAX_TRACERS; i++) if (tracers[i].life > 0) tracers[i].life--;
	for (int i = 0; i < MAX_PARTS; i++)
		if (parts[i].life > 0)
		{
			Part *p = &parts[i];
			p->x += p->vx; p->y += p->vy;
			p->vx *= p->drag; p->vy *= p->drag;
			p->life--;
		}
	for (int i = 0; i < MAX_SPLATS; i++) if (splats[i].life > 0) splats[i].life--;
	if (explosion > 0) explosion--;
}

static void draw(void)
{
	static const unsigned char shadow[3] = {0, 0, 0}, blood[3] = {120, 8, 14};
	static const unsigned char tracer[3] = {255, 186, 60}, flash[3] = {255, 210, 90};
	static const unsigned char fire[3] = {255, 190, 80};
	memset(fb, 0, (size_t)W * H * 4);

	for (int i = 0; i < MAX_SPLATS; i++)
		if (splats[i].life > 0)
			disc(splats[i].x, splats[i].y, splats[i].r, blood, 0.4f * clampf(splats[i].life / 90.0f, 0, 1));

	for (int k = 0; k < 2; k++)
		if (!babo[k].dead) blob(babo[k].x + R * 0.35f, babo[k].y + R * 0.45f, R * 1.15f, R * 0.9f, shadow, 0.35f);
	if (grenade.active)
		blob(grenade.x + grenade.z * 0.3f, grenade.y + grenade.z * 0.4f, R * 0.35f, R * 0.28f, shadow, 0.35f);

	for (int i = 0; i < MAX_PARTS; i++)
		if (parts[i].life > 0)
			disc(parts[i].x, parts[i].y, parts[i].size, parts[i].c, clampf((float)parts[i].life / parts[i].max * 1.5f, 0, 1));

	for (int i = 0; i < MAX_TRACERS; i++)
		if (tracers[i].life > 0)
		{
			const Tracer *t = &tracers[i];
			/* the bright head moves along the path as it fades */
			float f = 1 - t->life / 7.0f;
			float hx = t->x0 + (t->x1 - t->x0) * f, hy = t->y0 + (t->y1 - t->y0) * f;
			line(t->x0, t->y0, hx, hy, H * 0.0025f, tracer, 0.25f * t->life / 7.0f);
			float tx = hx + (t->x1 - t->x0) * 0.15f, ty = hy + (t->y1 - t->y0) * 0.15f;
			line(hx, hy, tx, ty, H * 0.004f, tracer, 0.9f);
		}

	for (int k = 0; k < 2; k++)
	{
		const Babo *b = &babo[k];
		if (b->dead) continue;
		float gx = b->x + cosf(b->aim) * R * 0.4f, gy = b->y + sinf(b->aim) * R * 0.4f;
		float ex = b->x + cosf(b->aim) * R * 1.6f, ey = b->y + sinf(b->aim) * R * 1.6f;
		drawBabo(b);
		line(gx, gy, ex, ey, R * 0.16f, fg, 1.0f);
		if (b->flash > 0)
			disc(b->x + cosf(b->aim) * R * 1.9f, b->y + sinf(b->aim) * R * 1.9f, R * 0.32f * b->flash / 3.0f, flash, 0.9f);
	}
	if (grenade.active)
	{
		float s = R * (0.2f + grenade.z / (H * 1.5f));
		disc(grenade.x, grenade.y - grenade.z * 0.2f, s, fg, 1.0f);
	}
	if (explosion > 0)
	{
		float f = 1 - explosion / 24.0f;
		blob((float)explosionX, (float)explosionY, R * (1 + 3 * f), R * (1 + 3 * f), fire, 0.8f * (1 - f));
	}
}

static int hex(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }

static int param(const char *name, int def, int lo, int hi)
{
	char v[32];
	if (!gasm_param_str(name, v, sizeof(v)) || !v[0]) return def;
	int n = atoi(v);
	return n < lo ? lo : n > hi ? hi : n;
}

GASM_EXPORT("gasm_abi_version") int32_t babos_abi_version(void) { return GASM_ABI_VERSION; }

GASM_EXPORT("gasm_init") int32_t babos_init(void)
{
	W = param("w", 640, 160, 2400);
	H = param("h", 360, 120, 1200);
	char c[16];
	if (gasm_param_str("fg", c, sizeof(c)) && strlen(c) == 6)
	{
		int ok = 1;
		for (int i = 0; i < 6; i++) if (hex(c[i]) < 0) ok = 0;
		if (ok) for (int i = 0; i < 3; i++) fg[i] = (unsigned char)(hex(c[2 * i]) * 16 + hex(c[2 * i + 1]));
	}
	fb = (unsigned char *)malloc((size_t)W * H * 4);
	if (!fb) return 1;
	R = H * 0.05f;
	static const unsigned char red[3] = {214, 52, 44}, blue[3] = {44, 104, 226};
	memcpy(babo[0].color, red, 3);
	memcpy(babo[1].color, blue, 3);
	spawn(&babo[0], 0);
	spawn(&babo[1], 1);
	gasm_set_frame_rate(60);
	return 0;
}

GASM_EXPORT("gasm_frame") void babos_frame(void)
{
	step();
	draw();
	gasm_video_present(fb, (uint32_t)W, (uint32_t)H, (uint32_t)W * 4);
}
