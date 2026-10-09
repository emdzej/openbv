/*
	OpenGL 1.x fixed function over gasm:gl (OpenGL ES 3.0 with WebGL 2's rules).

	Transform and lighting run on the CPU, as GL 1.x specifies them: every vertex is taken
	to eye space, lit, fogged and projected here, and the GPU only rasterises clip-space
	triangles and lines with one small shader (texture, fog colour, alpha test). Matrix and
	lighting changes therefore never break a batch; only rasteriser state does.

	The calls go straight to the gasm:gl imports (gasm.h), never through the C SDK's GLES
	wrappers, whose glEnable/glBindTexture/... would clash with the GL 1.x entry points
	defined here.

	Part of openbv, GPL-3.0-or-later.
*/

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glu.h>
#include "gl1.h"
#include "gl1_internal.h"
#include "gasm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>

namespace gl1 {

/* GLES 3 enums the GL 1.x header doesn't have */
enum {
	ES_ARRAY_BUFFER = 0x8892,
	ES_STREAM_DRAW = 0x88E0,
	ES_FRAGMENT_SHADER = 0x8B30,
	ES_VERTEX_SHADER = 0x8B31,
	ES_COMPILE_STATUS = 0x8B81,
	ES_LINK_STATUS = 0x8B82,
	ES_FRAMEBUFFER = 0x8D40,
	ES_RENDERBUFFER = 0x8D41,
	ES_READ_FRAMEBUFFER = 0x8CA8,
	ES_DRAW_FRAMEBUFFER = 0x8CA9,
	ES_COLOR_ATTACHMENT0 = 0x8CE0,
	ES_DEPTH_STENCIL_ATTACHMENT = 0x821A,
	ES_DEPTH24_STENCIL8 = 0x88F0,
	ES_TEXTURE0 = 0x84C0,
	ES_MAX_TEXTURE_SIZE = 0x0D33,
	ES_FUNC_ADD = 0x8006
};

/* ---- maths ------------------------------------------------------------------------------ */

static void mat_identity(float *m)
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1;
}

/* r = a * b, column-major (r may alias neither) */
static void mat_mul(float *r, const float *a, const float *b)
{
	for (int c = 0; c < 4; c++)
		for (int row = 0; row < 4; row++)
			r[c * 4 + row] = a[0 * 4 + row] * b[c * 4 + 0] + a[1 * 4 + row] * b[c * 4 + 1] +
							 a[2 * 4 + row] * b[c * 4 + 2] + a[3 * 4 + row] * b[c * 4 + 3];
}

static void mat_xform(float *r, const float *m, const float *v)
{
	for (int row = 0; row < 4; row++)
		r[row] = m[row] * v[0] + m[4 + row] * v[1] + m[8 + row] * v[2] + m[12 + row] * v[3];
}

/* ---- state ------------------------------------------------------------------------------ */

struct Light
{
	float amb[4], dif[4], spe[4], pos[4];	/* pos in eye space */
	float spotDir[3], spotExp, spotCut, att[3];
};

struct Material
{
	float amb[4], dif[4], spe[4], emi[4], shin;
};

struct Enables
{
	bool tex2d, blend, depth, cull, lighting, light[8], fog, alpha, colorMat, rescale, normalize,
		 scissor, offFill, offLine, offPoint, logic, dither, lineSmooth, lineStipple, pointSmooth,
		 polySmooth, polyStipple, stencil, tex1d, texGen[4], clip[6];
};

struct TexParams
{
	GLint minF, magF, wrapS, wrapT;
};

/* Everything glPushAttrib can save */
struct State
{
	Enables en;
	/* current */
	float color[4], normal[3], tex[4];
	/* polygon */
	GLenum cullMode, frontFace, polyMode[2];
	float offFactor, offUnits;
	/* lighting */
	Material mat[2];
	Light light[8];
	float modelAmb[4];
	bool localViewer, twoSide;
	GLenum cmFace, cmMode, shadeModel;
	/* line, point */
	float lineWidth, pointSize;
	/* fog */
	GLenum fogMode;
	float fogDensity, fogStart, fogEnd, fogColor[4];
	/* depth buffer */
	GLenum depthFunc;
	bool depthMask;
	double clearDepth;
	/* colour buffer */
	GLenum alphaFunc, blendSrc, blendDst, logicOp;
	float alphaRef, clearColor[4];
	bool colorMask[4];
	/* viewport, scissor */
	int viewport[4], scissor[4];
	float depthRange[2];
	/* transform */
	GLenum matrixMode;
	/* texture */
	GLuint texBinding;
	GLenum texEnvMode;
	float texEnvColor[4];
	/* list */
	GLuint listBase;
};

struct AttribFrame
{
	GLbitfield mask;
	State s;
	TexParams boundParams;	/* TEXTURE_BIT: the bound texture's parameters */
};

struct Tex
{
	uint32_t es;				/* gasm:gl name, 0 until it has an image */
	int w[16], h[16];
	bool lvl[16];
	GLenum base;				/* GL_RGB, GL_RGBA, GL_LUMINANCE, GL_ALPHA, ... */
	TexParams p;
	std::vector<uint8_t> rgba0;	/* level 0 as RGBA8, for glGetTexImage */
	bool cpuValid;
};

struct ClientArray
{
	bool on;
	GLint size;
	GLenum type;
	GLsizei stride;
	const uint8_t *ptr;
};

struct Vtx
{
	float x, y, z, w, r, g, b, a, s, t, fog;
};

/* Rasteriser state of a batch: a change ends the batch */
struct Key
{
	int cls;			/* 0 triangles, 1 lines, 2 wide-line quads (no culling, no offset) */
	uint32_t texEs;		/* 0: texturing off */
	int swz, env, alphaFunc, fogOn, invert;
	float alphaRef, fogColor[4];
	int blend, bsrc, bdst;
	int depthTest, depthFunc, depthMask;
	int cull, cullMode, frontFace;
	int scissorOn, sc[4], vp[4];
	int cmask[4];
	int offFill;
	float offF, offU, dr0, dr1;
};

enum { CLS_TRI, CLS_LINE, CLS_WIDE };

static State S;
static std::vector<AttribFrame> attribStack;
static float stackM[3][64][16];
static int depthM[3];
static float mvp[16], normalM[9], rescaleF;
static bool mvpDirty = true, normalDirty = true, texIdentity = true;
static std::map<GLuint, Tex> texs;
static GLuint nextTexName = 1;
static ClientArray caVertex, caNormal, caColor, caTex;
static GLenum glError;
static int unpackAlign = 4, packAlign = 4;

/* immediate mode */
static bool inBegin;
static GLenum beginMode;
static std::vector<Vtx> prim;

/* batches */
static std::vector<Vtx> batch;
static Key batchKey;
static bool batchHas;

/* GPU objects */
static bool ready;
static uint32_t prog, vao, vbo, fbo, fboColor, fboDepth;
static int fboW, fboH;
static bool viewportSet, scissorSet;
static int32_t uTex, uTexOn, uSwz, uEnv, uAlphaFunc, uAlphaRef, uFogOn, uFogColor, uInvert;
static Key applied;
static bool appliedValid;
static uint32_t appliedProgramTex = (uint32_t)-1;

/* display lists */
static std::map<GLuint, std::vector<float> > lists;
static GLuint nextList = 1;
static bool compiling;
static GLuint compileName;
static GLenum compileMode;
static std::vector<float> compileBuf;
static int callDepth;

static float *curM() { return stackM[S.matrixMode == GL_MODELVIEW ? 0 : S.matrixMode == GL_PROJECTION ? 1 : 2][depthM[S.matrixMode == GL_MODELVIEW ? 0 : S.matrixMode == GL_PROJECTION ? 1 : 2]]; }
static float *MV() { return stackM[0][depthM[0]]; }
static float *PR() { return stackM[1][depthM[1]]; }
static float *TX() { return stackM[2][depthM[2]]; }

static void setError(GLenum e) { if (!glError) glError = e; }

static void matrixChanged()
{
	if (S.matrixMode == GL_MODELVIEW) { mvpDirty = true; normalDirty = true; }
	else if (S.matrixMode == GL_PROJECTION) mvpDirty = true;
	else
	{
		const float *t = TX();
		texIdentity = true;
		for (int i = 0; i < 16; i++) if (t[i] != ((i % 5) == 0 ? 1.f : 0.f)) texIdentity = false;
	}
}

static void set4(float *d, float a, float b, float c, float e) { d[0] = a; d[1] = b; d[2] = c; d[3] = e; }

static void initState()
{
	memset(&S, 0, sizeof S);
	S.en.dither = true;
	set4(S.color, 1, 1, 1, 1);
	S.normal[2] = 1;
	set4(S.tex, 0, 0, 0, 1);
	S.cullMode = GL_BACK; S.frontFace = GL_CCW;
	S.polyMode[0] = S.polyMode[1] = GL_FILL;
	for (int f = 0; f < 2; f++)
	{
		set4(S.mat[f].amb, .2f, .2f, .2f, 1);
		set4(S.mat[f].dif, .8f, .8f, .8f, 1);
		set4(S.mat[f].spe, 0, 0, 0, 1);
		set4(S.mat[f].emi, 0, 0, 0, 1);
		S.mat[f].shin = 0;
	}
	for (int i = 0; i < 8; i++)
	{
		Light &l = S.light[i];
		set4(l.amb, 0, 0, 0, 1);
		if (i == 0) { set4(l.dif, 1, 1, 1, 1); set4(l.spe, 1, 1, 1, 1); }
		else { set4(l.dif, 0, 0, 0, 1); set4(l.spe, 0, 0, 0, 1); }
		set4(l.pos, 0, 0, 1, 0);
		l.spotDir[2] = -1; l.spotExp = 0; l.spotCut = 180;
		l.att[0] = 1; l.att[1] = 0; l.att[2] = 0;
	}
	set4(S.modelAmb, .2f, .2f, .2f, 1);
	S.cmFace = GL_FRONT_AND_BACK; S.cmMode = GL_AMBIENT_AND_DIFFUSE; S.shadeModel = GL_SMOOTH;
	S.lineWidth = 1; S.pointSize = 1;
	S.fogMode = GL_EXP; S.fogDensity = 1; S.fogStart = 0; S.fogEnd = 1;
	S.depthFunc = GL_LESS; S.depthMask = true; S.clearDepth = 1;
	S.alphaFunc = GL_ALWAYS; S.blendSrc = GL_ONE; S.blendDst = GL_ZERO; S.logicOp = GL_COPY;
	for (int i = 0; i < 4; i++) S.colorMask[i] = true;
	S.depthRange[0] = 0; S.depthRange[1] = 1;
	S.matrixMode = GL_MODELVIEW;
	S.texEnvMode = GL_MODULATE;
	for (int i = 0; i < 3; i++) { depthM[i] = 0; mat_identity(stackM[i][0]); }
}

/* ---- GPU side ----------------------------------------------------------------------------- */

static const char *VS =
	"#version 300 es\n"
	"layout(location = 0) in vec4 a_pos;\n"
	"layout(location = 1) in vec4 a_col;\n"
	"layout(location = 2) in vec2 a_uv;\n"
	"layout(location = 3) in float a_fog;\n"
	"out vec4 v_col;\n"
	"out vec2 v_uv;\n"
	"out float v_fog;\n"
	"void main() { gl_Position = a_pos; v_col = a_col; v_uv = a_uv; v_fog = a_fog; }\n";

static const char *FS =
	"#version 300 es\n"
	"precision highp float;\n"
	"uniform sampler2D u_tex;\n"
	"uniform int u_texOn, u_swz, u_env, u_alphaFunc, u_fogOn, u_invert;\n"
	"uniform float u_alphaRef;\n"
	"uniform vec4 u_fogColor;\n"
	"in vec4 v_col;\n"
	"in vec2 v_uv;\n"
	"in float v_fog;\n"
	"out vec4 o;\n"
	"void main() {\n"
	"  vec4 c = v_col;\n"
	"  if (u_texOn == 1) {\n"
	"    vec4 t = texture(u_tex, v_uv);\n"
	"    if (u_swz == 1) t.a = 1.0;\n"
	"    else if (u_swz == 2) t = vec4(t.rrr, 1.0);\n"
	"    else if (u_swz == 3) t = vec4(1.0, 1.0, 1.0, t.a);\n"
	"    else if (u_swz == 4) t = vec4(t.rrr, t.a);\n"
	"    else if (u_swz == 5) t = t.rrrr;\n"
	"    bool hasC = u_swz != 3, hasA = u_swz == 0 || u_swz >= 3;\n"
	"    if (u_env == 1) c = vec4(hasC ? t.rgb : c.rgb, hasA ? t.a : c.a);\n"	/* REPLACE */
	"    else if (u_env == 2) c = vec4(mix(c.rgb, t.rgb, t.a), c.a);\n"	/* DECAL */
	"    else if (u_env == 3) c = vec4(clamp(c.rgb + (hasC ? t.rgb : vec3(0.0)), 0.0, 1.0), c.a * t.a);\n"	/* ADD */
	"    else c *= t;\n"
	"  }\n"
	"  if (u_fogOn == 1) c.rgb = mix(u_fogColor.rgb, c.rgb, clamp(v_fog, 0.0, 1.0));\n"
	"  bool pass = true;\n"
	"  if (u_alphaFunc == 0) pass = false;\n"
	"  else if (u_alphaFunc == 1) pass = c.a < u_alphaRef;\n"
	"  else if (u_alphaFunc == 2) pass = c.a == u_alphaRef;\n"
	"  else if (u_alphaFunc == 3) pass = c.a <= u_alphaRef;\n"
	"  else if (u_alphaFunc == 4) pass = c.a > u_alphaRef;\n"
	"  else if (u_alphaFunc == 5) pass = c.a != u_alphaRef;\n"
	"  else if (u_alphaFunc == 6) pass = c.a >= u_alphaRef;\n"
	"  if (!pass) discard;\n"
	"  if (u_invert == 1) c = vec4(1.0);\n"
	"  o = c;\n"
	"}\n";

static uint32_t compileShader(uint32_t type, const char *src)
{
	uint32_t s = gasm_gl_create_shader(type);
	gasm_gl_shader_source(s, src, (uint32_t)strlen(src));
	gasm_gl_compile_shader(s);
	if (!gasm_gl_get_shaderiv(s, ES_COMPILE_STATUS))
	{
		char log[1024];
		int n = gasm_gl_get_shader_info_log(s, log, sizeof log - 1);
		if (n < 0) n = 0;
		if (n > (int)sizeof log - 1) n = sizeof log - 1;
		log[n] = 0;
		gasm_log_str("gl1: shader:");
		gasm_log_str(log);
	}
	return s;
}

static int32_t uniform(const char *name) { return gasm_gl_get_uniform_location(prog, name, (uint32_t)strlen(name)); }

static void makeFramebuffer(int w, int h)
{
	if (fbo)
	{
		gasm_gl_delete_framebuffer(fbo);
		gasm_gl_delete_texture(fboColor);
		gasm_gl_delete_renderbuffer(fboDepth);
	}
	fboW = w; fboH = h;
	fboColor = gasm_gl_create_texture();
	gasm_gl_bind_texture(GL_TEXTURE_2D, fboColor);
	gasm_gl_tex_storage_2d(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
	gasm_gl_tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	gasm_gl_tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	fboDepth = gasm_gl_create_renderbuffer();
	gasm_gl_bind_renderbuffer(ES_RENDERBUFFER, fboDepth);
	gasm_gl_renderbuffer_storage(ES_RENDERBUFFER, ES_DEPTH24_STENCIL8, w, h);
	fbo = gasm_gl_create_framebuffer();
	gasm_gl_bind_framebuffer(ES_FRAMEBUFFER, fbo);
	gasm_gl_framebuffer_texture_2d(ES_FRAMEBUFFER, ES_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboColor, 0);
	gasm_gl_framebuffer_renderbuffer(ES_FRAMEBUFFER, ES_DEPTH_STENCIL_ATTACHMENT, ES_RENDERBUFFER, fboDepth);
	appliedProgramTex = (uint32_t)-1;	/* the texture binding changed under us */
}

static void setup()
{
	if (ready) return;
	ready = true;
	uint32_t vs = compileShader(ES_VERTEX_SHADER, VS), fs = compileShader(ES_FRAGMENT_SHADER, FS);
	prog = gasm_gl_create_program();
	gasm_gl_attach_shader(prog, vs);
	gasm_gl_attach_shader(prog, fs);
	gasm_gl_link_program(prog);
	if (!gasm_gl_get_programiv(prog, ES_LINK_STATUS)) gasm_log_str("gl1: program does not link");
	gasm_gl_use_program(prog);
	uTex = uniform("u_tex"); uTexOn = uniform("u_texOn"); uSwz = uniform("u_swz"); uEnv = uniform("u_env");
	uAlphaFunc = uniform("u_alphaFunc"); uAlphaRef = uniform("u_alphaRef"); uFogOn = uniform("u_fogOn");
	uFogColor = uniform("u_fogColor"); uInvert = uniform("u_invert");
	gasm_gl_uniform1i(uTex, 0);
	vao = gasm_gl_create_vertex_array();
	gasm_gl_bind_vertex_array(vao);
	vbo = gasm_gl_create_buffer();
	gasm_gl_bind_buffer(ES_ARRAY_BUFFER, vbo);
	for (int i = 0; i < 4; i++) gasm_gl_enable_vertex_attrib_array(i);
	gasm_gl_vertex_attrib_pointer(0, 4, GL_FLOAT, 0, sizeof(Vtx), 0);
	gasm_gl_vertex_attrib_pointer(1, 4, GL_FLOAT, 0, sizeof(Vtx), 16);
	gasm_gl_vertex_attrib_pointer(2, 2, GL_FLOAT, 0, sizeof(Vtx), 32);
	gasm_gl_vertex_attrib_pointer(3, 1, GL_FLOAT, 0, sizeof(Vtx), 40);
	gasm_gl_active_texture(ES_TEXTURE0);
	appliedValid = false;
}

static void ensureFrame()
{
	setup();
	if (!fbo) gl1_begin_frame((int)gasm_gl_width(), (int)gasm_gl_height());
}

static int alphaFuncCode(GLenum f)
{
	return f >= GL_NEVER && f <= GL_ALWAYS ? (int)(f - GL_NEVER) : 7;
}

static Tex *boundTex()
{
	std::map<GLuint, Tex>::iterator it = texs.find(S.texBinding);
	return it == texs.end() ? 0 : &it->second;
}

/* GL 1.x: an incomplete texture behaves as if texturing were off */
static bool complete(const Tex &t)
{
	if (!t.es || !t.lvl[0]) return false;
	bool mip = t.p.minF != GL_NEAREST && t.p.minF != GL_LINEAR;
	if (!mip) return true;
	int w = t.w[0], h = t.h[0];
	for (int l = 1; l < 16 && (w > 1 || h > 1); l++)
	{
		w = w > 1 ? w / 2 : 1;
		h = h > 1 ? h / 2 : 1;
		if (!t.lvl[l] || t.w[l] != w || t.h[l] != h) return false;
	}
	return true;
}

static int swizzleOf(GLenum base)
{
	switch (base)
	{
	case GL_RGB: return 1;
	case GL_LUMINANCE: return 2;
	case GL_ALPHA: return 3;
	case GL_LUMINANCE_ALPHA: return 4;
	case GL_INTENSITY: return 5;
	default: return 0;
	}
}

static void currentKey(Key &k, int cls)
{
	memset(&k, 0, sizeof k);
	k.cls = cls;
	if (S.en.tex2d)
	{
		Tex *t = boundTex();
		if (t && complete(*t))
		{
			k.texEs = t->es;
			k.swz = swizzleOf(t->base);
			k.env = S.texEnvMode == GL_REPLACE ? 1 : S.texEnvMode == GL_DECAL ? 2 : S.texEnvMode == GL_ADD ? 3 : 0;
		}
	}
	k.alphaFunc = S.en.alpha ? alphaFuncCode(S.alphaFunc) : 7;
	k.alphaRef = S.en.alpha ? (S.alphaRef < 0 ? 0 : S.alphaRef > 1 ? 1 : S.alphaRef) : 0;
	if (S.en.fog) { k.fogOn = 1; memcpy(k.fogColor, S.fogColor, sizeof k.fogColor); }
	if (S.en.logic)
	{
		/* only the logic op the game uses is exact: INVERT = 1 - dst; COPY draws as usual */
		if (S.logicOp == GL_INVERT) { k.invert = 1; k.blend = 1; k.bsrc = GL_ONE_MINUS_DST_COLOR; k.bdst = GL_ZERO; }
		else if (S.logicOp == GL_CLEAR) { k.blend = 1; k.bsrc = GL_ZERO; k.bdst = GL_ZERO; }
		else if (S.logicOp == GL_NOOP) { k.blend = 1; k.bsrc = GL_ZERO; k.bdst = GL_ONE; }
		else { k.blend = 0; }
	}
	else if (S.en.blend) { k.blend = 1; k.bsrc = (int)S.blendSrc; k.bdst = (int)S.blendDst; }
	if (S.en.depth) { k.depthTest = 1; k.depthFunc = (int)S.depthFunc; }
	k.depthMask = S.depthMask;
	if (cls == CLS_TRI && S.en.cull) { k.cull = 1; k.cullMode = (int)S.cullMode; }
	k.frontFace = (int)S.frontFace;
	if (S.en.scissor) { k.scissorOn = 1; memcpy(k.sc, S.scissor, sizeof k.sc); }
	memcpy(k.vp, S.viewport, sizeof k.vp);
	for (int i = 0; i < 4; i++) k.cmask[i] = S.colorMask[i];
	if (cls == CLS_TRI && S.en.offFill) { k.offFill = 1; k.offF = S.offFactor; k.offU = S.offUnits; }
	k.dr0 = S.depthRange[0]; k.dr1 = S.depthRange[1];
}

static void applyKey(const Key &k)
{
	const Key &a = applied;
	bool all = !appliedValid;
	if (all || a.texEs != k.texEs || appliedProgramTex != k.texEs)
	{
		if (k.texEs) gasm_gl_bind_texture(GL_TEXTURE_2D, k.texEs);
		appliedProgramTex = k.texEs;
	}
	if (all || (a.texEs != 0) != (k.texEs != 0)) gasm_gl_uniform1i(uTexOn, k.texEs ? 1 : 0);
	if (all || a.swz != k.swz) gasm_gl_uniform1i(uSwz, k.swz);
	if (all || a.env != k.env) gasm_gl_uniform1i(uEnv, k.env);
	if (all || a.alphaFunc != k.alphaFunc) gasm_gl_uniform1i(uAlphaFunc, k.alphaFunc);
	if (all || a.alphaRef != k.alphaRef) gasm_gl_uniform1f(uAlphaRef, k.alphaRef);
	if (all || a.fogOn != k.fogOn) gasm_gl_uniform1i(uFogOn, k.fogOn);
	if (all || memcmp(a.fogColor, k.fogColor, sizeof k.fogColor)) gasm_gl_uniform4f(uFogColor, k.fogColor[0], k.fogColor[1], k.fogColor[2], k.fogColor[3]);
	if (all || a.invert != k.invert) gasm_gl_uniform1i(uInvert, k.invert);
	if (all || a.blend != k.blend) { if (k.blend) gasm_gl_enable(GL_BLEND); else gasm_gl_disable(GL_BLEND); }
	if (k.blend && (all || !a.blend || a.bsrc != k.bsrc || a.bdst != k.bdst)) gasm_gl_blend_func(k.bsrc, k.bdst);
	if (all || a.depthTest != k.depthTest) { if (k.depthTest) gasm_gl_enable(GL_DEPTH_TEST); else gasm_gl_disable(GL_DEPTH_TEST); }
	if (k.depthTest && (all || !a.depthTest || a.depthFunc != k.depthFunc)) gasm_gl_depth_func(k.depthFunc);
	if (all || a.depthMask != k.depthMask) gasm_gl_depth_mask(k.depthMask);
	if (all || a.cull != k.cull) { if (k.cull) gasm_gl_enable(GL_CULL_FACE); else gasm_gl_disable(GL_CULL_FACE); }
	if (k.cull && (all || !a.cull || a.cullMode != k.cullMode)) gasm_gl_cull_face(k.cullMode);
	if (all || a.frontFace != k.frontFace) gasm_gl_front_face(k.frontFace);
	if (all || a.scissorOn != k.scissorOn) { if (k.scissorOn) gasm_gl_enable(GL_SCISSOR_TEST); else gasm_gl_disable(GL_SCISSOR_TEST); }
	if (k.scissorOn && (all || !a.scissorOn || memcmp(a.sc, k.sc, sizeof k.sc))) gasm_gl_scissor(k.sc[0], k.sc[1], k.sc[2] < 0 ? 0 : k.sc[2], k.sc[3] < 0 ? 0 : k.sc[3]);
	if (all || memcmp(a.vp, k.vp, sizeof k.vp)) gasm_gl_viewport(k.vp[0], k.vp[1], k.vp[2] < 0 ? 0 : k.vp[2], k.vp[3] < 0 ? 0 : k.vp[3]);
	if (all || memcmp(a.cmask, k.cmask, sizeof k.cmask)) gasm_gl_color_mask(k.cmask[0], k.cmask[1], k.cmask[2], k.cmask[3]);
	if (all || a.offFill != k.offFill) { if (k.offFill) gasm_gl_enable(GL_POLYGON_OFFSET_FILL); else gasm_gl_disable(GL_POLYGON_OFFSET_FILL); }
	if (k.offFill && (all || !a.offFill || a.offF != k.offF || a.offU != k.offU)) gasm_gl_polygon_offset(k.offF, k.offU);
	if (all || a.dr0 != k.dr0 || a.dr1 != k.dr1) gasm_gl_depth_rangef(k.dr0, k.dr1);
	applied = k;
	appliedValid = true;
}

static void flushBatch()
{
	if (!batchHas) return;
	batchHas = false;
	if (batch.empty()) return;
	ensureFrame();
	applyKey(batchKey);
	gasm_gl_bind_buffer(ES_ARRAY_BUFFER, vbo);
	gasm_gl_buffer_data(ES_ARRAY_BUFFER, &batch[0], (uint32_t)(batch.size() * sizeof(Vtx)), ES_STREAM_DRAW);
	gasm_gl_draw_arrays(batchKey.cls == CLS_LINE ? GL_LINES : GL_TRIANGLES, 0, (int32_t)batch.size());
	batch.clear();
}

/* Make the batch match the current state for primitives of class cls */
static void useBatch(int cls)
{
	Key k;
	currentKey(k, cls);
	if (batchHas && !memcmp(&k, &batchKey, sizeof k)) return;
	flushBatch();
	batchKey = k;
	batchHas = true;
}

/* ---- per-vertex pipeline ------------------------------------------------------------------- */

static void updateMatrices()
{
	if (mvpDirty) { mat_mul(mvp, PR(), MV()); mvpDirty = false; }
	if (normalDirty)
	{
		const float *m = MV();
		/* inverse of the upper 3x3 (column-major a[col*4+row]) */
		float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
		float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
		float det = a * A + b * B + c * C;
		float inv[9];	/* row-major inverse */
		if (det == 0) { memset(inv, 0, sizeof inv); inv[0] = inv[4] = inv[8] = 1; }
		else
		{
			float r = 1 / det;
			inv[0] = A * r; inv[1] = -(b * i - c * h) * r; inv[2] = (b * f - c * e) * r;
			inv[3] = B * r; inv[4] = (a * i - c * g) * r; inv[5] = -(a * f - c * d) * r;
			inv[6] = C * r; inv[7] = -(a * h - b * g) * r; inv[8] = (a * e - b * d) * r;
		}
		/* n_eye = n_obj (row) * inv  ->  n_eye[j] = sum_k n[k] * inv[k][j] */
		memcpy(normalM, inv, sizeof normalM);
		float l = sqrtf(inv[6] * inv[6] + inv[7] * inv[7] + inv[8] * inv[8]);
		rescaleF = l > 0 ? 1 / l : 1;
		normalDirty = false;
	}
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static void lightVertex(const float *eye, float *out)
{
	const Material &m = S.mat[0];
	float n[3];
	for (int j = 0; j < 3; j++) n[j] = S.normal[0] * normalM[0 * 3 + j] + S.normal[1] * normalM[1 * 3 + j] + S.normal[2] * normalM[2 * 3 + j];
	if (S.en.normalize)
	{
		float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (l > 0) { n[0] /= l; n[1] /= l; n[2] /= l; }
	}
	else if (S.en.rescale) { n[0] *= rescaleF; n[1] *= rescaleF; n[2] *= rescaleF; }
	float ew = eye[3] != 0 ? eye[3] : 1;
	float v[3] = { eye[0] / ew, eye[1] / ew, eye[2] / ew };
	float c[3];
	for (int j = 0; j < 3; j++) c[j] = m.emi[j] + m.amb[j] * S.modelAmb[j];
	for (int i = 0; i < 8; i++)
	{
		if (!S.en.light[i]) continue;
		const Light &L = S.light[i];
		float vp[3], att = 1;
		if (L.pos[3] != 0)
		{
			for (int j = 0; j < 3; j++) vp[j] = L.pos[j] / L.pos[3] - v[j];
			float d = sqrtf(vp[0] * vp[0] + vp[1] * vp[1] + vp[2] * vp[2]);
			float den = L.att[0] + L.att[1] * d + L.att[2] * d * d;
			att = den != 0 ? 1 / den : 1;
			if (d > 0) { vp[0] /= d; vp[1] /= d; vp[2] /= d; }
		}
		else
		{
			for (int j = 0; j < 3; j++) vp[j] = L.pos[j];
			float d = sqrtf(vp[0] * vp[0] + vp[1] * vp[1] + vp[2] * vp[2]);
			if (d > 0) { vp[0] /= d; vp[1] /= d; vp[2] /= d; }
		}
		if (L.spotCut != 180)
		{
			float sd[3] = { L.spotDir[0], L.spotDir[1], L.spotDir[2] };
			float sl = sqrtf(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]);
			if (sl > 0) { sd[0] /= sl; sd[1] /= sl; sd[2] /= sl; }
			float cs = -(vp[0] * sd[0] + vp[1] * sd[1] + vp[2] * sd[2]);
			if (cs < cosf(L.spotCut * 3.14159265358979f / 180)) att = 0;
			else att *= powf(cs > 0 ? cs : 0, L.spotExp);
		}
		if (att == 0) continue;
		float ndl = n[0] * vp[0] + n[1] * vp[1] + n[2] * vp[2];
		if (ndl < 0) ndl = 0;
		float spec = 0;
		if (ndl > 0)
		{
			float hv[3];
			if (S.localViewer)
			{
				float vl = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
				for (int j = 0; j < 3; j++) hv[j] = vp[j] - (vl > 0 ? v[j] / vl : 0);
			}
			else { hv[0] = vp[0]; hv[1] = vp[1]; hv[2] = vp[2] + 1; }
			float hl = sqrtf(hv[0] * hv[0] + hv[1] * hv[1] + hv[2] * hv[2]);
			float ndh = hl > 0 ? (n[0] * hv[0] + n[1] * hv[1] + n[2] * hv[2]) / hl : 0;
			if (ndh < 0) ndh = 0;
			spec = powf(ndh, m.shin);
		}
		for (int j = 0; j < 3; j++)
			c[j] += att * (m.amb[j] * L.amb[j] + ndl * m.dif[j] * L.dif[j] + spec * m.spe[j] * L.spe[j]);
	}
	out[0] = clamp01(c[0]); out[1] = clamp01(c[1]); out[2] = clamp01(c[2]);
	out[3] = clamp01(m.dif[3]);
}

static void processVertex(const float *obj, Vtx &o)
{
	updateMatrices();
	bool needEye = S.en.lighting || S.en.fog;
	float clip[4], eye[4];
	if (needEye)
	{
		mat_xform(eye, MV(), obj);
		mat_xform(clip, PR(), eye);
	}
	else mat_xform(clip, mvp, obj);
	o.x = clip[0]; o.y = clip[1]; o.z = clip[2]; o.w = clip[3];
	float col[4];
	if (S.en.lighting) lightVertex(eye, col);
	else for (int j = 0; j < 4; j++) col[j] = clamp01(S.color[j]);
	o.r = col[0]; o.g = col[1]; o.b = col[2]; o.a = col[3];
	if (texIdentity) { o.s = S.tex[0]; o.t = S.tex[1]; if (S.tex[3] != 1 && S.tex[3] != 0) { o.s /= S.tex[3]; o.t /= S.tex[3]; } }
	else
	{
		float tc[4];
		mat_xform(tc, TX(), S.tex);
		float q = tc[3] != 0 ? tc[3] : 1;
		o.s = tc[0] / q; o.t = tc[1] / q;
	}
	if (S.en.fog)
	{
		float cz = fabsf(eye[2]);
		float f;
		if (S.fogMode == GL_LINEAR) f = S.fogEnd != S.fogStart ? (S.fogEnd - cz) / (S.fogEnd - S.fogStart) : 1;
		else if (S.fogMode == GL_EXP2) { float x = S.fogDensity * cz; f = expf(-x * x); }
		else f = expf(-S.fogDensity * cz);
		o.fog = clamp01(f);
	}
	else o.fog = 1;
}

/* ---- primitive assembly ---------------------------------------------------------------------- */

static void pushTri(const Vtx &a, const Vtx &b, const Vtx &c)
{
	batch.push_back(a); batch.push_back(b); batch.push_back(c);
}

static int lineWidthPx()
{
	int w = (int)floorf(S.lineWidth + .5f);
	return w < 1 ? 1 : w;
}

/* GL's aliased wide line: the segment widened along the minor axis by width pixels */
static void emitLine(const Vtx &a, const Vtx &b)
{
	int wpx = lineWidthPx();
	if (wpx <= 1 || a.w <= 0 || b.w <= 0)
	{
		useBatch(CLS_LINE);
		batch.push_back(a); batch.push_back(b);
		return;
	}
	useBatch(CLS_WIDE);
	float vw = (float)S.viewport[2], vh = (float)S.viewport[3];
	if (vw <= 0 || vh <= 0) return;
	float ax = a.x / a.w * vw / 2, ay = a.y / a.w * vh / 2, bx = b.x / b.w * vw / 2, by = b.y / b.w * vh / 2;
	float dx = 0, dy = 0, half = wpx * .5f;
	if (fabsf(bx - ax) >= fabsf(by - ay)) dy = half; else dx = half;
	Vtx q[4] = { a, a, b, b };
	/* offset in window pixels, back to clip space at each end's w */
	q[0].x = (ax - dx) / (vw / 2) * a.w; q[0].y = (ay - dy) / (vh / 2) * a.w;
	q[1].x = (ax + dx) / (vw / 2) * a.w; q[1].y = (ay + dy) / (vh / 2) * a.w;
	q[2].x = (bx + dx) / (vw / 2) * b.w; q[2].y = (by + dy) / (vh / 2) * b.w;
	q[3].x = (bx - dx) / (vw / 2) * b.w; q[3].y = (by - dy) / (vh / 2) * b.w;
	pushTri(q[0], q[1], q[2]);
	pushTri(q[0], q[2], q[3]);
}

/* One polygon given by vertex indices into v (3 or 4 of them, or a fan's whole list) */
static void emitPolygon(const Vtx *v, const int *idx, int n)
{
	if (n < 3) return;
	bool fill = S.polyMode[0] == GL_FILL && S.polyMode[1] == GL_FILL;
	if (fill)
	{
		useBatch(CLS_TRI);
		for (int i = 1; i + 1 < n; i++) pushTri(v[idx[0]], v[idx[i]], v[idx[i + 1]]);
		return;
	}
	/* facing from the window-space area (NDC has the same orientation) */
	bool front = true;
	bool allW = true;
	for (int i = 0; i < n; i++) if (v[idx[i]].w <= 0) allW = false;
	if (allW)
	{
		float area = 0;
		for (int i = 0; i < n; i++)
		{
			const Vtx &p = v[idx[i]], &q = v[idx[(i + 1) % n]];
			area += (p.x / p.w) * (q.y / q.w) - (q.x / q.w) * (p.y / p.w);
		}
		front = S.frontFace == GL_CCW ? area > 0 : area < 0;
	}
	if (S.en.cull)
	{
		if (S.cullMode == GL_FRONT_AND_BACK) return;
		if (S.cullMode == GL_FRONT && front) return;
		if (S.cullMode == GL_BACK && !front) return;
	}
	GLenum mode = S.polyMode[front ? 0 : 1];
	if (mode == GL_FILL)
	{
		useBatch(CLS_TRI);
		for (int i = 1; i + 1 < n; i++) pushTri(v[idx[0]], v[idx[i]], v[idx[i + 1]]);
	}
	else if (mode == GL_LINE)
	{
		for (int i = 0; i < n; i++) emitLine(v[idx[i]], v[idx[(i + 1) % n]]);
	}
	/* GL_POINT: points aren't drawn (the game never asks for them) */
}

static void assemble(GLenum mode, const Vtx *v, int n)
{
	int idx[4];
	switch (mode)
	{
	case GL_TRIANGLES:
		for (int i = 0; i + 2 < n; i += 3) { idx[0] = i; idx[1] = i + 1; idx[2] = i + 2; emitPolygon(v, idx, 3); }
		break;
	case GL_QUADS:
		for (int i = 0; i + 3 < n; i += 4) { idx[0] = i; idx[1] = i + 1; idx[2] = i + 2; idx[3] = i + 3; emitPolygon(v, idx, 4); }
		break;
	case GL_QUAD_STRIP:
		for (int i = 0; i + 3 < n; i += 2) { idx[0] = i; idx[1] = i + 1; idx[2] = i + 3; idx[3] = i + 2; emitPolygon(v, idx, 4); }
		break;
	case GL_TRIANGLE_STRIP:
		for (int i = 0; i + 2 < n; i++)
		{
			if (i & 1) { idx[0] = i + 1; idx[1] = i; } else { idx[0] = i; idx[1] = i + 1; }
			idx[2] = i + 2;
			emitPolygon(v, idx, 3);
		}
		break;
	case GL_TRIANGLE_FAN:
		for (int i = 1; i + 1 < n; i++) { idx[0] = 0; idx[1] = i; idx[2] = i + 1; emitPolygon(v, idx, 3); }
		break;
	case GL_POLYGON:
		if (n >= 3)
		{
			std::vector<int> all(n);
			for (int i = 0; i < n; i++) all[i] = i;
			emitPolygon(v, &all[0], n);
		}
		break;
	case GL_LINES:
		for (int i = 0; i + 1 < n; i += 2) emitLine(v[i], v[i + 1]);
		break;
	case GL_LINE_STRIP:
		for (int i = 0; i + 1 < n; i++) emitLine(v[i], v[i + 1]);
		break;
	case GL_LINE_LOOP:
		for (int i = 0; i + 1 < n; i++) emitLine(v[i], v[i + 1]);
		if (n >= 2) emitLine(v[n - 1], v[0]);
		break;
	default:
		break;	/* GL_POINTS */
	}
}

/* ---- textures --------------------------------------------------------------------------------- */

static Tex &texObject(GLuint name)
{
	std::map<GLuint, Tex>::iterator it = texs.find(name);
	if (it != texs.end()) return it->second;
	Tex &t = texs[name];
	t.es = 0;
	memset(t.w, 0, sizeof t.w); memset(t.h, 0, sizeof t.h); memset(t.lvl, 0, sizeof t.lvl);
	t.base = GL_RGBA;
	t.p.minF = GL_NEAREST_MIPMAP_LINEAR; t.p.magF = GL_LINEAR; t.p.wrapS = GL_REPEAT; t.p.wrapT = GL_REPEAT;
	t.cpuValid = false;
	return t;
}

static GLint esWrap(GLint w) { return w == GL_CLAMP ? GL_CLAMP_TO_EDGE : w; }

static void applyParams(Tex &t)
{
	gasm_gl_bind_texture(GL_TEXTURE_2D, t.es);
	gasm_gl_tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, t.p.minF);
	gasm_gl_tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, t.p.magF);
	gasm_gl_tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, esWrap(t.p.wrapS));
	gasm_gl_tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, esWrap(t.p.wrapT));
	appliedProgramTex = t.es;
}

static void ensureEs(Tex &t)
{
	if (t.es) { gasm_gl_bind_texture(GL_TEXTURE_2D, t.es); appliedProgramTex = t.es; return; }
	t.es = gasm_gl_create_texture();
	applyParams(t);
}

GLenum baseFormat(GLint internal)
{
	switch (internal)
	{
	case 1: case GL_LUMINANCE: case GL_LUMINANCE4: case GL_LUMINANCE8: return GL_LUMINANCE;
	case 2: case GL_LUMINANCE_ALPHA: case GL_LUMINANCE8_ALPHA8: return GL_LUMINANCE_ALPHA;
	case 3: case GL_RGB: case GL_RGB4: case GL_RGB5: case GL_RGB8: case GL_R3_G3_B2: return GL_RGB;
	case GL_ALPHA: case GL_ALPHA4: case GL_ALPHA8: return GL_ALPHA;
	case GL_INTENSITY: case GL_INTENSITY8: return GL_INTENSITY;
	default: return GL_RGBA;
	}
}

int formatComponents(GLenum format)
{
	switch (format)
	{
	case GL_RGB: case GL_BGR_EXT: return 3;
	case GL_RGBA: case GL_BGRA_EXT: return 4;
	case GL_LUMINANCE_ALPHA: return 2;
	default: return 1;
	}
}

/* Unpack client pixels (UNSIGNED_BYTE) into RGBA8 as GL converts them, then keep only the
   components of the internal format (missing colour = 0, missing alpha = 1; the stored
   texel is laid out so that the shader's swizzle for the format is the identity). */
void unpackToRGBA(GLenum format, int w, int h, const void *pixels, GLenum base, std::vector<uint8_t> &out, int align)
{
	out.assign((size_t)w * h * 4, 0);
	if (!pixels) return;
	int comps = formatComponents(format);
	size_t rowBytes = (size_t)w * comps;
	size_t stride = (rowBytes + align - 1) / align * align;
	const uint8_t *src = (const uint8_t *)pixels;
	for (int y = 0; y < h; y++)
	{
		const uint8_t *s = src + stride * y;
		uint8_t *d = &out[(size_t)y * w * 4];
		for (int x = 0; x < w; x++, s += comps, d += 4)
		{
			uint8_t r = 0, g = 0, b = 0, a = 255;
			switch (format)
			{
			case GL_RGB: r = s[0]; g = s[1]; b = s[2]; break;
			case GL_RGBA: r = s[0]; g = s[1]; b = s[2]; a = s[3]; break;
			case GL_BGR_EXT: r = s[2]; g = s[1]; b = s[0]; break;
			case GL_BGRA_EXT: r = s[2]; g = s[1]; b = s[0]; a = s[3]; break;
			case GL_LUMINANCE: r = g = b = s[0]; break;
			case GL_LUMINANCE_ALPHA: r = g = b = s[0]; a = s[1]; break;
			case GL_ALPHA: a = s[0]; break;
			case GL_RED: r = s[0]; break;
			case GL_GREEN: g = s[0]; break;
			case GL_BLUE: b = s[0]; break;
			default: break;
			}
			switch (base)
			{
			case GL_RGB: d[0] = r; d[1] = g; d[2] = b; d[3] = 255; break;
			case GL_LUMINANCE: d[0] = d[1] = d[2] = r; d[3] = 255; break;
			case GL_LUMINANCE_ALPHA: d[0] = d[1] = d[2] = r; d[3] = a; break;
			case GL_ALPHA: d[0] = d[1] = d[2] = 255; d[3] = a; break;
			case GL_INTENSITY: d[0] = d[1] = d[2] = d[3] = r; break;
			default: d[0] = r; d[1] = g; d[2] = b; d[3] = a; break;
			}
		}
	}
}

int unpackAlignment() { return unpackAlign; }

/* Upload an RGBA8 image already laid out for the internal format */
void texImageRGBA(GLint level, GLenum base, int w, int h, const uint8_t *rgba)
{
	if (level < 0 || level >= 16 || w < 0 || h < 0) { setError(GL_INVALID_VALUE); return; }
	flushBatch();
	ensureFrame();
	Tex &t = texObject(S.texBinding);
	ensureEs(t);
	gasm_gl_tex_image_2d(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, w && h ? rgba : 0, (uint32_t)w * h * 4);
	t.w[level] = w; t.h[level] = h; t.lvl[level] = w > 0 && h > 0;
	if (level == 0)
	{
		t.base = base;
		t.rgba0.assign(rgba, rgba + (size_t)w * h * 4);
		t.cpuValid = true;
	}
}

static void readTexLevel0(Tex &t, std::vector<uint8_t> &rgba)
{
	if (t.cpuValid) { rgba = t.rgba0; return; }
	int w = t.w[0], h = t.h[0];
	rgba.assign((size_t)w * h * 4, 0);
	if (!t.es || !w || !h) return;
	uint32_t tmp = gasm_gl_create_framebuffer();
	gasm_gl_bind_framebuffer(ES_FRAMEBUFFER, tmp);
	gasm_gl_framebuffer_texture_2d(ES_FRAMEBUFFER, ES_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.es, 0);
	gasm_gl_read_pixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0], (uint32_t)rgba.size());
	gasm_gl_bind_framebuffer(ES_FRAMEBUFFER, fbo);
	gasm_gl_delete_framebuffer(tmp);
	/* the stored texel follows the base format; copies hold raw framebuffer colour */
	for (size_t i = 0; i < rgba.size(); i += 4)
	{
		switch (t.base)
		{
		case GL_RGB: rgba[i + 3] = 255; break;
		case GL_LUMINANCE: rgba[i + 1] = rgba[i + 2] = rgba[i]; rgba[i + 3] = 255; break;
		case GL_LUMINANCE_ALPHA: rgba[i + 1] = rgba[i + 2] = rgba[i]; break;
		case GL_ALPHA: rgba[i] = rgba[i + 1] = rgba[i + 2] = 255; break;
		case GL_INTENSITY: rgba[i + 1] = rgba[i + 2] = rgba[i + 3] = rgba[i]; break;
		default: break;
		}
	}
	t.rgba0 = rgba;
	t.cpuValid = true;
}

/* Write RGBA8 (bottom row first) as format/UNSIGNED_BYTE with a pack alignment */
static void packFromRGBA(const uint8_t *rgba, int w, int h, GLenum format, GLenum base, void *dst)
{
	int comps = formatComponents(format);
	size_t rowBytes = (size_t)w * comps;
	size_t stride = (rowBytes + packAlign - 1) / packAlign * packAlign;
	uint8_t *out = (uint8_t *)dst;
	for (int y = 0; y < h; y++)
	{
		const uint8_t *s = rgba + (size_t)y * w * 4;
		uint8_t *d = out + stride * y;
		for (int x = 0; x < w; x++, s += 4, d += comps)
		{
			/* a texel as GL reports it: luminance comes back in red, missing colour as 0 */
			uint8_t r = s[0], g = s[1], b = s[2], a = s[3];
			if (base == GL_LUMINANCE || base == GL_LUMINANCE_ALPHA || base == GL_INTENSITY) { g = 0; b = 0; }
			if (base == GL_ALPHA) { r = g = b = 0; }
			switch (format)
			{
			case GL_RGB: d[0] = r; d[1] = g; d[2] = b; break;
			case GL_RGBA: d[0] = r; d[1] = g; d[2] = b; d[3] = a; break;
			case GL_BGR_EXT: d[0] = b; d[1] = g; d[2] = r; break;
			case GL_BGRA_EXT: d[0] = b; d[1] = g; d[2] = r; d[3] = a; break;
			case GL_ALPHA: d[0] = a; break;
			case GL_LUMINANCE: { int l = r + g + b; d[0] = (uint8_t)(l > 255 ? 255 : l); break; }
			case GL_LUMINANCE_ALPHA: { int l = r + g + b; d[0] = (uint8_t)(l > 255 ? 255 : l); d[1] = a; break; }
			case GL_RED: d[0] = r; break;
			case GL_GREEN: d[0] = g; break;
			case GL_BLUE: d[0] = b; break;
			default: d[0] = r; break;
			}
		}
	}
}

/* ---- display lists ------------------------------------------------------------------------------ */

enum Op
{
	OP_BEGIN = 1, OP_END, OP_VERTEX, OP_TEXCOORD, OP_COLOR, OP_NORMAL, OP_MATRIXMODE, OP_PUSHM, OP_POPM,
	OP_LOADID, OP_LOADM, OP_MULTM, OP_TRANSLATE, OP_ROTATE, OP_SCALE, OP_ORTHO, OP_FRUSTUM, OP_ENABLE,
	OP_DISABLE, OP_PUSHATTRIB, OP_POPATTRIB, OP_BLENDFUNC, OP_ALPHAFUNC, OP_DEPTHFUNC, OP_DEPTHMASK,
	OP_COLORMASK, OP_CULLFACE, OP_FRONTFACE, OP_POLYMODE, OP_POLYOFFSET, OP_LINEWIDTH, OP_POINTSIZE,
	OP_LOGICOP, OP_SHADEMODEL, OP_SCISSOR, OP_VIEWPORT, OP_DEPTHRANGE, OP_CLEAR, OP_CLEARCOLOR,
	OP_CLEARDEPTH, OP_LIGHT, OP_LIGHTMODEL, OP_MATERIAL, OP_COLORMATERIAL, OP_FOG, OP_BINDTEX,
	OP_TEXPARAM, OP_TEXENV, OP_CALLLIST, OP_CALLOFFSET, OP_LISTBASE, OP_HINT
};

static int opArgs(int op)
{
	switch (op)
	{
	case OP_LOADM: case OP_MULTM: return 16;
	case OP_ORTHO: case OP_FRUSTUM: case OP_LIGHT: case OP_MATERIAL: case OP_TEXPARAM: case OP_TEXENV: return 6;
	case OP_LIGHTMODEL: case OP_FOG: return 5;
	case OP_VERTEX: case OP_TEXCOORD: case OP_COLOR: case OP_ROTATE: case OP_COLORMASK: case OP_SCISSOR:
	case OP_VIEWPORT: case OP_CLEARCOLOR: return 4;
	case OP_NORMAL: case OP_TRANSLATE: case OP_SCALE: return 3;
	case OP_BLENDFUNC: case OP_ALPHAFUNC: case OP_POLYMODE: case OP_POLYOFFSET: case OP_DEPTHRANGE:
	case OP_COLORMATERIAL: case OP_HINT: return 2;
	case OP_END: case OP_PUSHM: case OP_POPM: case OP_LOADID: case OP_POPATTRIB: return 0;
	default: return 1;
	}
}

/* Record a command if a list is being compiled; true if it must not also run now */
static bool rec(int op, const float *a)
{
	if (!compiling) return false;
	compileBuf.push_back((float)op);
	for (int i = 0, n = opArgs(op); i < n; i++) compileBuf.push_back(a ? a[i] : 0);
	return compileMode == GL_COMPILE;
}

static bool rec0(int op) { return rec(op, 0); }
static bool rec1(int op, float x) { return rec(op, &x); }
static bool rec2(int op, float x, float y) { float a[2] = { x, y }; return rec(op, a); }
static bool rec3(int op, float x, float y, float z) { float a[3] = { x, y, z }; return rec(op, a); }
static bool rec4(int op, float x, float y, float z, float w) { float a[4] = { x, y, z, w }; return rec(op, a); }

/* ---- the operations themselves ------------------------------------------------------------- */

static void exVertex(float x, float y, float z, float w)
{
	if (!inBegin) return;
	float obj[4] = { x, y, z, w };
	Vtx v;
	processVertex(obj, v);
	prim.push_back(v);
}

static void applyColorMaterial()
{
	if (!S.en.colorMat) return;
	for (int f = 0; f < 2; f++)
	{
		if (f == 0 && S.cmFace == GL_BACK) continue;
		if (f == 1 && S.cmFace == GL_FRONT) continue;
		Material &m = S.mat[f];
		switch (S.cmMode)
		{
		case GL_AMBIENT: memcpy(m.amb, S.color, 16); break;
		case GL_DIFFUSE: memcpy(m.dif, S.color, 16); break;
		case GL_SPECULAR: memcpy(m.spe, S.color, 16); break;
		case GL_EMISSION: memcpy(m.emi, S.color, 16); break;
		default: memcpy(m.amb, S.color, 16); memcpy(m.dif, S.color, 16); break;
		}
	}
}

static void exColor(float r, float g, float b, float a)
{
	set4(S.color, r, g, b, a);
	applyColorMaterial();
}

static bool *enableFlag(GLenum cap)
{
	switch (cap)
	{
	case GL_TEXTURE_2D: return &S.en.tex2d;
	case GL_TEXTURE_1D: return &S.en.tex1d;
	case GL_BLEND: return &S.en.blend;
	case GL_DEPTH_TEST: return &S.en.depth;
	case GL_CULL_FACE: return &S.en.cull;
	case GL_LIGHTING: return &S.en.lighting;
	case GL_FOG: return &S.en.fog;
	case GL_ALPHA_TEST: return &S.en.alpha;
	case GL_COLOR_MATERIAL: return &S.en.colorMat;
	case GL_RESCALE_NORMAL: return &S.en.rescale;
	case GL_NORMALIZE: return &S.en.normalize;
	case GL_SCISSOR_TEST: return &S.en.scissor;
	case GL_POLYGON_OFFSET_FILL: return &S.en.offFill;
	case GL_POLYGON_OFFSET_LINE: return &S.en.offLine;
	case GL_POLYGON_OFFSET_POINT: return &S.en.offPoint;
	case GL_COLOR_LOGIC_OP: return &S.en.logic;
	case GL_DITHER: return &S.en.dither;
	case GL_LINE_SMOOTH: return &S.en.lineSmooth;
	case GL_LINE_STIPPLE: return &S.en.lineStipple;
	case GL_POINT_SMOOTH: return &S.en.pointSmooth;
	case GL_POLYGON_SMOOTH: return &S.en.polySmooth;
	case GL_POLYGON_STIPPLE: return &S.en.polyStipple;
	case GL_STENCIL_TEST: return &S.en.stencil;
	case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T: case GL_TEXTURE_GEN_R: case GL_TEXTURE_GEN_Q: return &S.en.texGen[cap - GL_TEXTURE_GEN_S];
	default:
		if (cap >= GL_LIGHT0 && cap <= GL_LIGHT7) return &S.en.light[cap - GL_LIGHT0];
		if (cap >= GL_CLIP_PLANE0 && cap <= GL_CLIP_PLANE5) return &S.en.clip[cap - GL_CLIP_PLANE0];
		return 0;
	}
}

static void exEnable(GLenum cap, bool on)
{
	bool *f = enableFlag(cap);
	if (!f) { setError(GL_INVALID_ENUM); return; }
	bool was = *f;
	*f = on;
	if (cap == GL_COLOR_MATERIAL && on && !was) applyColorMaterial();
}

static void exPushAttrib(GLbitfield mask)
{
	if (attribStack.size() >= 64) { setError(GL_STACK_OVERFLOW); return; }
	AttribFrame fr;
	fr.mask = mask;
	fr.s = S;
	Tex *t = boundTex();
	if (t) fr.boundParams = t->p;
	else { fr.boundParams.minF = GL_NEAREST_MIPMAP_LINEAR; fr.boundParams.magF = GL_LINEAR; fr.boundParams.wrapS = fr.boundParams.wrapT = GL_REPEAT; }
	attribStack.push_back(fr);
}

static void setTexParams(Tex &t, const TexParams &p);

static void exPopAttrib()
{
	if (attribStack.empty()) { setError(GL_STACK_UNDERFLOW); return; }
	AttribFrame fr = attribStack.back();
	attribStack.pop_back();
	const State &o = fr.s;
	GLbitfield m = fr.mask;
	if (m & GL_ENABLE_BIT) S.en = o.en;
	if (m & GL_CURRENT_BIT) { memcpy(S.color, o.color, sizeof S.color); memcpy(S.normal, o.normal, sizeof S.normal); memcpy(S.tex, o.tex, sizeof S.tex); }
	if (m & GL_POLYGON_BIT)
	{
		S.cullMode = o.cullMode; S.frontFace = o.frontFace; S.polyMode[0] = o.polyMode[0]; S.polyMode[1] = o.polyMode[1];
		S.offFactor = o.offFactor; S.offUnits = o.offUnits;
		S.en.cull = o.en.cull; S.en.offFill = o.en.offFill; S.en.offLine = o.en.offLine; S.en.offPoint = o.en.offPoint;
		S.en.polySmooth = o.en.polySmooth; S.en.polyStipple = o.en.polyStipple;
	}
	if (m & GL_LIGHTING_BIT)
	{
		memcpy(S.mat, o.mat, sizeof S.mat); memcpy(S.light, o.light, sizeof S.light);
		memcpy(S.modelAmb, o.modelAmb, sizeof S.modelAmb);
		S.localViewer = o.localViewer; S.twoSide = o.twoSide; S.cmFace = o.cmFace; S.cmMode = o.cmMode; S.shadeModel = o.shadeModel;
		S.en.lighting = o.en.lighting; S.en.colorMat = o.en.colorMat;
		memcpy(S.en.light, o.en.light, sizeof S.en.light);
	}
	if (m & GL_LINE_BIT) { S.lineWidth = o.lineWidth; S.en.lineSmooth = o.en.lineSmooth; S.en.lineStipple = o.en.lineStipple; }
	if (m & GL_POINT_BIT) { S.pointSize = o.pointSize; S.en.pointSmooth = o.en.pointSmooth; }
	if (m & GL_FOG_BIT)
	{
		S.fogMode = o.fogMode; S.fogDensity = o.fogDensity; S.fogStart = o.fogStart; S.fogEnd = o.fogEnd;
		memcpy(S.fogColor, o.fogColor, sizeof S.fogColor); S.en.fog = o.en.fog;
	}
	if (m & GL_DEPTH_BUFFER_BIT) { S.depthFunc = o.depthFunc; S.depthMask = o.depthMask; S.clearDepth = o.clearDepth; S.en.depth = o.en.depth; }
	if (m & GL_COLOR_BUFFER_BIT)
	{
		S.alphaFunc = o.alphaFunc; S.alphaRef = o.alphaRef; S.blendSrc = o.blendSrc; S.blendDst = o.blendDst; S.logicOp = o.logicOp;
		memcpy(S.colorMask, o.colorMask, sizeof S.colorMask); memcpy(S.clearColor, o.clearColor, sizeof S.clearColor);
		S.en.alpha = o.en.alpha; S.en.blend = o.en.blend; S.en.logic = o.en.logic; S.en.dither = o.en.dither;
	}
	if (m & GL_VIEWPORT_BIT) { memcpy(S.viewport, o.viewport, sizeof S.viewport); memcpy(S.depthRange, o.depthRange, sizeof S.depthRange); }
	if (m & GL_SCISSOR_BIT) { memcpy(S.scissor, o.scissor, sizeof S.scissor); S.en.scissor = o.en.scissor; }
	if (m & GL_TRANSFORM_BIT) { S.matrixMode = o.matrixMode; S.en.normalize = o.en.normalize; S.en.rescale = o.en.rescale; memcpy(S.en.clip, o.en.clip, sizeof S.en.clip); }
	if (m & GL_TEXTURE_BIT)
	{
		S.texBinding = o.texBinding; S.texEnvMode = o.texEnvMode; memcpy(S.texEnvColor, o.texEnvColor, sizeof S.texEnvColor);
		S.en.tex2d = o.en.tex2d; S.en.tex1d = o.en.tex1d; memcpy(S.en.texGen, o.en.texGen, sizeof S.en.texGen);
		std::map<GLuint, Tex>::iterator it = texs.find(S.texBinding);
		if (it != texs.end()) setTexParams(it->second, fr.boundParams);
	}
	if (m & GL_LIST_BIT) S.listBase = o.listBase;
}

static void setTexParams(Tex &t, const TexParams &p)
{
	if (!memcmp(&t.p, &p, sizeof p)) return;
	flushBatch();
	t.p = p;
	if (t.es) applyParams(t);
}

static int matIndex() { return S.matrixMode == GL_MODELVIEW ? 0 : S.matrixMode == GL_PROJECTION ? 1 : 2; }

static void exMult(const float *m)
{
	float r[16];
	mat_mul(r, curM(), m);
	memcpy(curM(), r, sizeof r);
	matrixChanged();
}

static void exRotate(float a, float x, float y, float z)
{
	float l = sqrtf(x * x + y * y + z * z);
	if (l == 0) return;
	x /= l; y /= l; z /= l;
	float r = a * 3.14159265358979323846f / 180;
	float c = cosf(r), s = sinf(r), t = 1 - c;
	float m[16] = {
		x * x * t + c, y * x * t + z * s, x * z * t - y * s, 0,
		x * y * t - z * s, y * y * t + c, y * z * t + x * s, 0,
		x * z * t + y * s, y * z * t - x * s, z * z * t + c, 0,
		0, 0, 0, 1 };
	exMult(m);
}

static void exOrtho(double l, double r, double b, double t, double n, double f)
{
	if (l == r || b == t || n == f) { setError(GL_INVALID_VALUE); return; }
	float m[16] = { 0 };
	m[0] = (float)(2 / (r - l)); m[5] = (float)(2 / (t - b)); m[10] = (float)(-2 / (f - n));
	m[12] = (float)(-(r + l) / (r - l)); m[13] = (float)(-(t + b) / (t - b)); m[14] = (float)(-(f + n) / (f - n)); m[15] = 1;
	exMult(m);
}

static void exFrustum(double l, double r, double b, double t, double n, double f)
{
	if (n <= 0 || f <= 0 || l == r || b == t || n == f) { setError(GL_INVALID_VALUE); return; }
	float m[16] = { 0 };
	m[0] = (float)(2 * n / (r - l)); m[5] = (float)(2 * n / (t - b));
	m[8] = (float)((r + l) / (r - l)); m[9] = (float)((t + b) / (t - b)); m[10] = (float)(-(f + n) / (f - n)); m[11] = -1;
	m[14] = (float)(-2 * f * n / (f - n));
	exMult(m);
}

static void exLight(GLenum light, GLenum pname, const float *p)
{
	if (light < GL_LIGHT0 || light > GL_LIGHT7) { setError(GL_INVALID_ENUM); return; }
	Light &L = S.light[light - GL_LIGHT0];
	switch (pname)
	{
	case GL_AMBIENT: memcpy(L.amb, p, 16); break;
	case GL_DIFFUSE: memcpy(L.dif, p, 16); break;
	case GL_SPECULAR: memcpy(L.spe, p, 16); break;
	case GL_POSITION: mat_xform(L.pos, MV(), p); break;
	case GL_SPOT_DIRECTION:
	{
		const float *m = MV();
		for (int j = 0; j < 3; j++) L.spotDir[j] = m[j] * p[0] + m[4 + j] * p[1] + m[8 + j] * p[2];
		break;
	}
	case GL_SPOT_EXPONENT: L.spotExp = p[0]; break;
	case GL_SPOT_CUTOFF: L.spotCut = p[0]; break;
	case GL_CONSTANT_ATTENUATION: L.att[0] = p[0]; break;
	case GL_LINEAR_ATTENUATION: L.att[1] = p[0]; break;
	case GL_QUADRATIC_ATTENUATION: L.att[2] = p[0]; break;
	default: setError(GL_INVALID_ENUM); break;
	}
}

static void exMaterial(GLenum face, GLenum pname, const float *p)
{
	for (int f = 0; f < 2; f++)
	{
		if (f == 0 && face == GL_BACK) continue;
		if (f == 1 && face == GL_FRONT) continue;
		Material &m = S.mat[f];
		switch (pname)
		{
		case GL_AMBIENT: memcpy(m.amb, p, 16); break;
		case GL_DIFFUSE: memcpy(m.dif, p, 16); break;
		case GL_SPECULAR: memcpy(m.spe, p, 16); break;
		case GL_EMISSION: memcpy(m.emi, p, 16); break;
		case GL_SHININESS: m.shin = p[0]; break;
		case GL_AMBIENT_AND_DIFFUSE: memcpy(m.amb, p, 16); memcpy(m.dif, p, 16); break;
		default: break;
		}
	}
}

static void exFog(GLenum pname, const float *p)
{
	switch (pname)
	{
	case GL_FOG_MODE: S.fogMode = (GLenum)p[0]; break;
	case GL_FOG_DENSITY: S.fogDensity = p[0]; break;
	case GL_FOG_START: S.fogStart = p[0]; break;
	case GL_FOG_END: S.fogEnd = p[0]; break;
	case GL_FOG_COLOR: for (int i = 0; i < 4; i++) S.fogColor[i] = clamp01(p[i]); break;
	default: break;
	}
}

static void exBindTexture(GLuint name)
{
	S.texBinding = name;
	if (name) texObject(name);
}

static void exTexParam(GLenum pname, const float *p)
{
	Tex &t = texObject(S.texBinding);
	TexParams np = t.p;
	GLint v = (GLint)p[0];
	switch (pname)
	{
	case GL_TEXTURE_MIN_FILTER: np.minF = v; break;
	case GL_TEXTURE_MAG_FILTER: np.magF = v; break;
	case GL_TEXTURE_WRAP_S: np.wrapS = v; break;
	case GL_TEXTURE_WRAP_T: np.wrapT = v; break;
	default: return;	/* border colour, priorities, anisotropy: ignored */
	}
	setTexParams(t, np);
}

static void exClear(GLbitfield mask)
{
	flushBatch();
	ensureFrame();
	Key k;
	currentKey(k, CLS_TRI);
	applyKey(k);
	gasm_gl_clear_color(clamp01(S.clearColor[0]), clamp01(S.clearColor[1]), clamp01(S.clearColor[2]), clamp01(S.clearColor[3]));
	gasm_gl_clear_depthf((float)S.clearDepth);
	gasm_gl_clear(mask & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
}

static void exEnd()
{
	if (!inBegin) { setError(GL_INVALID_OPERATION); return; }
	inBegin = false;
	if (!prim.empty()) assemble(beginMode, &prim[0], (int)prim.size());
	prim.clear();
}

void callList(GLuint name);

static void exPushMatrix()
{
	int i = matIndex();
	if (depthM[i] >= 63) { setError(GL_STACK_OVERFLOW); return; }
	memcpy(stackM[i][depthM[i] + 1], stackM[i][depthM[i]], 64);
	depthM[i]++;
}

static void exPopMatrix()
{
	int i = matIndex();
	if (depthM[i] <= 0) { setError(GL_STACK_UNDERFLOW); return; }
	depthM[i]--;
	matrixChanged();
}

static void replay(const std::vector<float> &b)
{
	size_t i = 0;
	while (i < b.size())
	{
		int op = (int)b[i++];
		const float *a = &b[i];
		i += opArgs(op);
		switch (op)
		{
		case OP_BEGIN: inBegin = true; beginMode = (GLenum)a[0]; prim.clear(); break;
		case OP_END: exEnd(); break;
		case OP_VERTEX: exVertex(a[0], a[1], a[2], a[3]); break;
		case OP_TEXCOORD: set4(S.tex, a[0], a[1], a[2], a[3]); break;
		case OP_COLOR: exColor(a[0], a[1], a[2], a[3]); break;
		case OP_NORMAL: S.normal[0] = a[0]; S.normal[1] = a[1]; S.normal[2] = a[2]; break;
		case OP_MATRIXMODE: S.matrixMode = (GLenum)a[0]; break;
		case OP_PUSHM: exPushMatrix(); break;
		case OP_POPM: exPopMatrix(); break;
		case OP_LOADID: mat_identity(curM()); matrixChanged(); break;
		case OP_LOADM: memcpy(curM(), a, 64); matrixChanged(); break;
		case OP_MULTM: exMult(a); break;
		case OP_TRANSLATE: { float m[16]; mat_identity(m); m[12] = a[0]; m[13] = a[1]; m[14] = a[2]; exMult(m); break; }
		case OP_ROTATE: exRotate(a[0], a[1], a[2], a[3]); break;
		case OP_SCALE: { float m[16]; mat_identity(m); m[0] = a[0]; m[5] = a[1]; m[10] = a[2]; exMult(m); break; }
		case OP_ORTHO: exOrtho(a[0], a[1], a[2], a[3], a[4], a[5]); break;
		case OP_FRUSTUM: exFrustum(a[0], a[1], a[2], a[3], a[4], a[5]); break;
		case OP_ENABLE: exEnable((GLenum)a[0], true); break;
		case OP_DISABLE: exEnable((GLenum)a[0], false); break;
		case OP_PUSHATTRIB: exPushAttrib((GLbitfield)a[0]); break;
		case OP_POPATTRIB: exPopAttrib(); break;
		case OP_BLENDFUNC: S.blendSrc = (GLenum)a[0]; S.blendDst = (GLenum)a[1]; break;
		case OP_ALPHAFUNC: S.alphaFunc = (GLenum)a[0]; S.alphaRef = a[1]; break;
		case OP_DEPTHFUNC: S.depthFunc = (GLenum)a[0]; break;
		case OP_DEPTHMASK: S.depthMask = a[0] != 0; break;
		case OP_COLORMASK: for (int j = 0; j < 4; j++) S.colorMask[j] = a[j] != 0; break;
		case OP_CULLFACE: S.cullMode = (GLenum)a[0]; break;
		case OP_FRONTFACE: S.frontFace = (GLenum)a[0]; break;
		case OP_POLYMODE:
		{
			GLenum face = (GLenum)a[0], mode = (GLenum)a[1];
			if (face == GL_FRONT || face == GL_FRONT_AND_BACK) S.polyMode[0] = mode;
			if (face == GL_BACK || face == GL_FRONT_AND_BACK) S.polyMode[1] = mode;
			break;
		}
		case OP_POLYOFFSET: S.offFactor = a[0]; S.offUnits = a[1]; break;
		case OP_LINEWIDTH: S.lineWidth = a[0]; break;
		case OP_POINTSIZE: S.pointSize = a[0]; break;
		case OP_LOGICOP: S.logicOp = (GLenum)a[0]; break;
		case OP_SHADEMODEL: S.shadeModel = (GLenum)a[0]; break;
		case OP_SCISSOR: scissorSet = true; for (int j = 0; j < 4; j++) S.scissor[j] = (int)a[j]; break;
		case OP_VIEWPORT: viewportSet = true; for (int j = 0; j < 4; j++) S.viewport[j] = (int)a[j]; break;
		case OP_DEPTHRANGE: S.depthRange[0] = clamp01(a[0]); S.depthRange[1] = clamp01(a[1]); break;
		case OP_CLEAR: exClear((GLbitfield)a[0]); break;
		case OP_CLEARCOLOR: memcpy(S.clearColor, a, 16); break;
		case OP_CLEARDEPTH: S.clearDepth = a[0]; break;
		case OP_LIGHT: exLight((GLenum)a[0], (GLenum)a[1], a + 2); break;
		case OP_LIGHTMODEL:
			if ((GLenum)a[0] == GL_LIGHT_MODEL_AMBIENT) memcpy(S.modelAmb, a + 1, 16);
			else if ((GLenum)a[0] == GL_LIGHT_MODEL_LOCAL_VIEWER) S.localViewer = a[1] != 0;
			else if ((GLenum)a[0] == GL_LIGHT_MODEL_TWO_SIDE) S.twoSide = a[1] != 0;
			break;
		case OP_MATERIAL: exMaterial((GLenum)a[0], (GLenum)a[1], a + 2); break;
		case OP_COLORMATERIAL: S.cmFace = (GLenum)a[0]; S.cmMode = (GLenum)a[1]; applyColorMaterial(); break;
		case OP_FOG: exFog((GLenum)a[0], a + 1); break;
		case OP_BINDTEX: exBindTexture((GLuint)a[0]); break;
		case OP_TEXPARAM: exTexParam((GLenum)a[1], a + 2); break;
		case OP_TEXENV:
			if ((GLenum)a[1] == GL_TEXTURE_ENV_MODE) S.texEnvMode = (GLenum)a[2];
			else if ((GLenum)a[1] == GL_TEXTURE_ENV_COLOR) memcpy(S.texEnvColor, a + 2, 16);
			break;
		case OP_CALLLIST: callList((GLuint)a[0]); break;
		case OP_CALLOFFSET: callList(S.listBase + (GLuint)a[0]); break;
		case OP_LISTBASE: S.listBase = (GLuint)a[0]; break;
		case OP_HINT: break;
		default: return;
		}
	}
}

void callList(GLuint name)
{
	if (callDepth >= 64) return;
	std::map<GLuint, std::vector<float> >::iterator it = lists.find(name);
	if (it == lists.end()) return;
	callDepth++;
	replay(it->second);
	callDepth--;
}

/* ---- client arrays ------------------------------------------------------------------------------- */

static void fetch(const ClientArray &ca, int i, float *out, int defN)
{
	(void)defN;
	int stride = ca.stride;
	int esize = ca.type == GL_FLOAT ? 4 : ca.type == GL_DOUBLE ? 8 : ca.type == GL_INT || ca.type == GL_UNSIGNED_INT ? 4 :
				ca.type == GL_SHORT || ca.type == GL_UNSIGNED_SHORT ? 2 : 1;
	if (!stride) stride = esize * ca.size;
	const uint8_t *p = ca.ptr + (size_t)stride * i;
	for (int k = 0; k < ca.size; k++)
	{
		switch (ca.type)
		{
		case GL_FLOAT: { float f; memcpy(&f, p + 4 * k, 4); out[k] = f; break; }
		case GL_DOUBLE: { double d; memcpy(&d, p + 8 * k, 8); out[k] = (float)d; break; }
		case GL_INT: { int32_t v; memcpy(&v, p + 4 * k, 4); out[k] = (float)v; break; }
		case GL_SHORT: { int16_t v; memcpy(&v, p + 2 * k, 2); out[k] = (float)v; break; }
		case GL_UNSIGNED_BYTE: out[k] = p[k] / 255.f; break;	/* only colours come as bytes */
		case GL_BYTE: out[k] = (float)(int8_t)p[k]; break;
		default: out[k] = 0; break;
		}
	}
}

} /* namespace gl1 */

using namespace gl1;

/* ---- the GL 1.x entry points --------------------------------------------------------------------- */

extern "C" {

void gl1_begin_frame(int width, int height)
{
	setup();
	if (width < 1) width = 1;
	if (height < 1) height = 1;
	if (!fbo || width != fboW || height != fboH) makeFramebuffer(width, height);
	gasm_gl_bind_framebuffer(ES_FRAMEBUFFER, fbo);
	if (!viewportSet) { viewportSet = true; S.viewport[0] = S.viewport[1] = 0; S.viewport[2] = width; S.viewport[3] = height; }
	if (!scissorSet) { scissorSet = true; S.scissor[0] = S.scissor[1] = 0; S.scissor[2] = width; S.scissor[3] = height; }
}

void gl1_flush(void) { flushBatch(); }

void gl1_frame_rect(int winW, int winH, int *x, int *y, int *w, int *h)
{
	int fw = fboW > 0 ? fboW : winW, fh = fboH > 0 ? fboH : winH;
	if (winW * fh <= winH * fw) { *w = winW; *h = winW * fh / fw; }
	else { *h = winH; *w = winH * fw / fh; }
	*x = (winW - *w) / 2;
	*y = (winH - *h) / 2;
}

void gl1_end_frame(void)
{
	ensureFrame();
	flushBatch();
	int w = (int)gasm_gl_width(), h = (int)gasm_gl_height();
	gasm_gl_bind_framebuffer(ES_READ_FRAMEBUFFER, fbo);
	gasm_gl_bind_framebuffer(ES_DRAW_FRAMEBUFFER, 0);
	/* the blit obeys the scissor test and nothing else of ours */
	if (appliedValid && applied.scissorOn) gasm_gl_disable(GL_SCISSOR_TEST);
	/* the game's resolution scaled to the window, aspect kept, black bars around */
	int dx, dy, dw, dh;
	gl1_frame_rect(w, h, &dx, &dy, &dw, &dh);
	gasm_gl_color_mask(1, 1, 1, 1);
	gasm_gl_clear_color(0, 0, 0, 1);
	gasm_gl_clear(GL_COLOR_BUFFER_BIT);
	gasm_gl_blit_framebuffer(0, 0, fboW, fboH, dx, dy, dx + dw, dy + dh, GL_COLOR_BUFFER_BIT,
		(dw == fboW && dh == fboH) ? GL_NEAREST : GL_LINEAR);
	/* the frame shown is opaque, whatever alpha the game left in its framebuffer */
	gasm_gl_bind_framebuffer(ES_FRAMEBUFFER, 0);
	gasm_gl_color_mask(0, 0, 0, 1);
	gasm_gl_clear(GL_COLOR_BUFFER_BIT);
	if (appliedValid) gasm_gl_color_mask(applied.cmask[0], applied.cmask[1], applied.cmask[2], applied.cmask[3]);
	else gasm_gl_color_mask(1, 1, 1, 1);
	if (appliedValid && applied.scissorOn) gasm_gl_enable(GL_SCISSOR_TEST);
	gasm_gl_bind_framebuffer(ES_FRAMEBUFFER, fbo);
}

void APIENTRY glBegin(GLenum mode)
{
	if (rec1(OP_BEGIN, (float)mode)) return;
	if (inBegin) { setError(GL_INVALID_OPERATION); return; }
	inBegin = true;
	beginMode = mode;
	prim.clear();
}

void APIENTRY glEnd(void)
{
	if (rec0(OP_END)) return;
	exEnd();
}

void APIENTRY glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { if (rec4(OP_VERTEX, x, y, z, w)) return; exVertex(x, y, z, w); }
void APIENTRY glVertex2i(GLint x, GLint y) { glVertex4f((float)x, (float)y, 0, 1); }
void APIENTRY glVertex2f(GLfloat x, GLfloat y) { glVertex4f(x, y, 0, 1); }
void APIENTRY glVertex2d(GLdouble x, GLdouble y) { glVertex4f((float)x, (float)y, 0, 1); }
void APIENTRY glVertex2fv(const GLfloat *v) { glVertex4f(v[0], v[1], 0, 1); }
void APIENTRY glVertex2iv(const GLint *v) { glVertex4f((float)v[0], (float)v[1], 0, 1); }
void APIENTRY glVertex3i(GLint x, GLint y, GLint z) { glVertex4f((float)x, (float)y, (float)z, 1); }
void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z) { glVertex4f(x, y, z, 1); }
void APIENTRY glVertex3d(GLdouble x, GLdouble y, GLdouble z) { glVertex4f((float)x, (float)y, (float)z, 1); }
void APIENTRY glVertex3fv(const GLfloat *v) { glVertex4f(v[0], v[1], v[2], 1); }
void APIENTRY glVertex3iv(const GLint *v) { glVertex4f((float)v[0], (float)v[1], (float)v[2], 1); }
void APIENTRY glVertex4fv(const GLfloat *v) { glVertex4f(v[0], v[1], v[2], v[3]); }

static void texCoord4(float s, float t, float r, float q) { if (rec4(OP_TEXCOORD, s, t, r, q)) return; set4(S.tex, s, t, r, q); }
void APIENTRY glTexCoord2i(GLint s, GLint t) { texCoord4((float)s, (float)t, 0, 1); }
void APIENTRY glTexCoord2f(GLfloat s, GLfloat t) { texCoord4(s, t, 0, 1); }
void APIENTRY glTexCoord2d(GLdouble s, GLdouble t) { texCoord4((float)s, (float)t, 0, 1); }
void APIENTRY glTexCoord2fv(const GLfloat *v) { texCoord4(v[0], v[1], 0, 1); }

void APIENTRY glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { if (rec4(OP_COLOR, r, g, b, a)) return; exColor(r, g, b, a); }
void APIENTRY glColor3f(GLfloat r, GLfloat g, GLfloat b) { glColor4f(r, g, b, 1); }
void APIENTRY glColor3d(GLdouble r, GLdouble g, GLdouble b) { glColor4f((float)r, (float)g, (float)b, 1); }
void APIENTRY glColor4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a) { glColor4f((float)r, (float)g, (float)b, (float)a); }
void APIENTRY glColor3fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], 1); }
void APIENTRY glColor4fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], v[3]); }
void APIENTRY glColor3ub(GLubyte r, GLubyte g, GLubyte b) { glColor4f(r / 255.f, g / 255.f, b / 255.f, 1); }
void APIENTRY glColor3ubv(const GLubyte *v) { glColor3ub(v[0], v[1], v[2]); }
void APIENTRY glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { glColor4f(r / 255.f, g / 255.f, b / 255.f, a / 255.f); }
void APIENTRY glColor4ubv(const GLubyte *v) { glColor4ub(v[0], v[1], v[2], v[3]); }

void APIENTRY glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
	if (rec3(OP_NORMAL, x, y, z)) return;
	S.normal[0] = x; S.normal[1] = y; S.normal[2] = z;
}
void APIENTRY glNormal3fv(const GLfloat *v) { glNormal3f(v[0], v[1], v[2]); }

/* matrices */
void APIENTRY glMatrixMode(GLenum mode)
{
	if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE) { setError(GL_INVALID_ENUM); return; }
	if (rec1(OP_MATRIXMODE, (float)mode)) return;
	S.matrixMode = mode;
}

void APIENTRY glPushMatrix(void) { if (rec0(OP_PUSHM)) return; exPushMatrix(); }
void APIENTRY glPopMatrix(void) { if (rec0(OP_POPM)) return; exPopMatrix(); }

void APIENTRY glLoadIdentity(void) { if (rec0(OP_LOADID)) return; mat_identity(curM()); matrixChanged(); }
void APIENTRY glLoadMatrixf(const GLfloat *m) { if (rec(OP_LOADM, m)) return; memcpy(curM(), m, 64); matrixChanged(); }
void APIENTRY glLoadMatrixd(const GLdouble *m) { float f[16]; for (int i = 0; i < 16; i++) f[i] = (float)m[i]; glLoadMatrixf(f); }
void APIENTRY glMultMatrixf(const GLfloat *m) { if (rec(OP_MULTM, m)) return; exMult(m); }
void APIENTRY glMultMatrixd(const GLdouble *m) { float f[16]; for (int i = 0; i < 16; i++) f[i] = (float)m[i]; glMultMatrixf(f); }

void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	if (rec3(OP_TRANSLATE, x, y, z)) return;
	float m[16]; mat_identity(m); m[12] = x; m[13] = y; m[14] = z; exMult(m);
}
void APIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z) { glTranslatef((float)x, (float)y, (float)z); }

void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) { if (rec4(OP_ROTATE, angle, x, y, z)) return; exRotate(angle, x, y, z); }
void APIENTRY glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z) { glRotatef((float)angle, (float)x, (float)y, (float)z); }

void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z)
{
	if (rec3(OP_SCALE, x, y, z)) return;
	float m[16]; mat_identity(m); m[0] = x; m[5] = y; m[10] = z; exMult(m);
}
void APIENTRY glScaled(GLdouble x, GLdouble y, GLdouble z) { glScalef((float)x, (float)y, (float)z); }

void APIENTRY glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	float a[6] = { (float)l, (float)r, (float)b, (float)t, (float)n, (float)f };
	if (rec(OP_ORTHO, a)) return;
	exOrtho(l, r, b, t, n, f);
}

void APIENTRY glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	float a[6] = { (float)l, (float)r, (float)b, (float)t, (float)n, (float)f };
	if (rec(OP_FRUSTUM, a)) return;
	exFrustum(l, r, b, t, n, f);
}

void APIENTRY glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
	if (rec4(OP_VIEWPORT, (float)x, (float)y, (float)w, (float)h)) return;
	viewportSet = true;
	S.viewport[0] = x; S.viewport[1] = y; S.viewport[2] = w; S.viewport[3] = h;
}

void APIENTRY glDepthRange(GLclampd n, GLclampd f)
{
	if (rec2(OP_DEPTHRANGE, (float)n, (float)f)) return;
	S.depthRange[0] = clamp01((float)n); S.depthRange[1] = clamp01((float)f);
}

/* state */
void APIENTRY glEnable(GLenum cap) { if (rec1(OP_ENABLE, (float)cap)) return; exEnable(cap, true); }
void APIENTRY glDisable(GLenum cap) { if (rec1(OP_DISABLE, (float)cap)) return; exEnable(cap, false); }

GLboolean APIENTRY glIsEnabled(GLenum cap)
{
	switch (cap)
	{
	case GL_VERTEX_ARRAY: return caVertex.on;
	case GL_NORMAL_ARRAY: return caNormal.on;
	case GL_COLOR_ARRAY: return caColor.on;
	case GL_TEXTURE_COORD_ARRAY: return caTex.on;
	default: break;
	}
	bool *f = enableFlag(cap);
	if (!f) { setError(GL_INVALID_ENUM); return GL_FALSE; }
	return *f ? GL_TRUE : GL_FALSE;
}

void APIENTRY glPushAttrib(GLbitfield mask) { if (rec1(OP_PUSHATTRIB, (float)mask)) return; exPushAttrib(mask); }
void APIENTRY glPopAttrib(void) { if (rec0(OP_POPATTRIB)) return; exPopAttrib(); }

static std::vector<GLbitfield> clientMasks;
static std::vector<ClientArray> clientSaved;
void APIENTRY glPushClientAttrib(GLbitfield mask)
{
	clientMasks.push_back(mask);
	clientSaved.push_back(caVertex); clientSaved.push_back(caNormal); clientSaved.push_back(caColor); clientSaved.push_back(caTex);
	clientSaved.push_back(ClientArray());
	clientSaved.back().size = unpackAlign; clientSaved.back().stride = packAlign;
}
void APIENTRY glPopClientAttrib(void)
{
	if (clientMasks.empty()) { setError(GL_STACK_UNDERFLOW); return; }
	GLbitfield m = clientMasks.back(); clientMasks.pop_back();
	ClientArray px = clientSaved.back(); clientSaved.pop_back();
	ClientArray t = clientSaved.back(); clientSaved.pop_back();
	ClientArray c = clientSaved.back(); clientSaved.pop_back();
	ClientArray n = clientSaved.back(); clientSaved.pop_back();
	ClientArray v = clientSaved.back(); clientSaved.pop_back();
	if (m & GL_CLIENT_VERTEX_ARRAY_BIT) { caVertex = v; caNormal = n; caColor = c; caTex = t; }
	if (m & GL_CLIENT_PIXEL_STORE_BIT) { unpackAlign = px.size; packAlign = px.stride; }
}

void APIENTRY glBlendFunc(GLenum s, GLenum d) { if (rec2(OP_BLENDFUNC, (float)s, (float)d)) return; S.blendSrc = s; S.blendDst = d; }
void APIENTRY glAlphaFunc(GLenum func, GLclampf ref) { if (rec2(OP_ALPHAFUNC, (float)func, ref)) return; S.alphaFunc = func; S.alphaRef = ref; }
void APIENTRY glDepthFunc(GLenum func) { if (rec1(OP_DEPTHFUNC, (float)func)) return; S.depthFunc = func; }
void APIENTRY glDepthMask(GLboolean flag) { if (rec1(OP_DEPTHMASK, flag ? 1.f : 0.f)) return; S.depthMask = flag != 0; }
void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
	if (rec4(OP_COLORMASK, r ? 1.f : 0.f, g ? 1.f : 0.f, b ? 1.f : 0.f, a ? 1.f : 0.f)) return;
	S.colorMask[0] = r != 0; S.colorMask[1] = g != 0; S.colorMask[2] = b != 0; S.colorMask[3] = a != 0;
}
void APIENTRY glCullFace(GLenum mode) { if (rec1(OP_CULLFACE, (float)mode)) return; S.cullMode = mode; }
void APIENTRY glFrontFace(GLenum mode) { if (rec1(OP_FRONTFACE, (float)mode)) return; S.frontFace = mode; }
void APIENTRY glPolygonMode(GLenum face, GLenum mode)
{
	if (rec2(OP_POLYMODE, (float)face, (float)mode)) return;
	if (face == GL_FRONT || face == GL_FRONT_AND_BACK) S.polyMode[0] = mode;
	if (face == GL_BACK || face == GL_FRONT_AND_BACK) S.polyMode[1] = mode;
}
void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units) { if (rec2(OP_POLYOFFSET, factor, units)) return; S.offFactor = factor; S.offUnits = units; }
void APIENTRY glLineWidth(GLfloat width)
{
	if (width <= 0) { setError(GL_INVALID_VALUE); return; }
	if (rec1(OP_LINEWIDTH, width)) return;
	S.lineWidth = width;
}
void APIENTRY glPointSize(GLfloat size) { if (rec1(OP_POINTSIZE, size)) return; S.pointSize = size; }
void APIENTRY glLogicOp(GLenum opcode) { if (rec1(OP_LOGICOP, (float)opcode)) return; S.logicOp = opcode; }
void APIENTRY glShadeModel(GLenum mode) { if (rec1(OP_SHADEMODEL, (float)mode)) return; S.shadeModel = mode; }
void APIENTRY glHint(GLenum target, GLenum mode) { if (rec2(OP_HINT, (float)target, (float)mode)) return; }
void APIENTRY glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
	if (rec4(OP_SCISSOR, (float)x, (float)y, (float)w, (float)h)) return;
	scissorSet = true;
	S.scissor[0] = x; S.scissor[1] = y; S.scissor[2] = w; S.scissor[3] = h;
}
void APIENTRY glClear(GLbitfield mask) { if (rec1(OP_CLEAR, (float)mask)) return; exClear(mask); }
void APIENTRY glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { if (rec4(OP_CLEARCOLOR, r, g, b, a)) return; set4(S.clearColor, r, g, b, a); }
void APIENTRY glClearDepth(GLclampd depth) { if (rec1(OP_CLEARDEPTH, (float)depth)) return; S.clearDepth = depth < 0 ? 0 : depth > 1 ? 1 : depth; }
void APIENTRY glFlush(void) { flushBatch(); }
void APIENTRY glFinish(void) { flushBatch(); }

GLenum APIENTRY glGetError(void)
{
	GLenum e = glError;
	glError = GL_NO_ERROR;
	return e;
}

const GLubyte * APIENTRY glGetString(GLenum name)
{
	switch (name)
	{
	case GL_VENDOR: return (const GLubyte *)"openbv";
	case GL_RENDERER: return (const GLubyte *)"gl1 on gasm:gl";
	case GL_VERSION: return (const GLubyte *)"1.4.0 gl1";
	case GL_EXTENSIONS: return (const GLubyte *)"";
	default: setError(GL_INVALID_ENUM); return 0;
	}
}

/* every query as floats; n = count written, 0 if unknown */
static int query(GLenum pname, float *v)
{
	switch (pname)
	{
	case GL_CURRENT_COLOR: memcpy(v, S.color, 16); return 4;
	case GL_CURRENT_NORMAL: memcpy(v, S.normal, 12); return 3;
	case GL_CURRENT_TEXTURE_COORDS: memcpy(v, S.tex, 16); return 4;
	case GL_MODELVIEW_MATRIX: memcpy(v, MV(), 64); return 16;
	case GL_PROJECTION_MATRIX: memcpy(v, PR(), 64); return 16;
	case GL_TEXTURE_MATRIX: memcpy(v, TX(), 64); return 16;
	case GL_VIEWPORT: if (!viewportSet) ensureFrame(); for (int i = 0; i < 4; i++) v[i] = (float)S.viewport[i]; return 4;
	case GL_SCISSOR_BOX: if (!viewportSet) ensureFrame(); for (int i = 0; i < 4; i++) v[i] = (float)S.scissor[i]; return 4;
	case GL_DEPTH_RANGE: v[0] = S.depthRange[0]; v[1] = S.depthRange[1]; return 2;
	case GL_LINE_WIDTH: v[0] = S.lineWidth; return 1;
	case GL_POINT_SIZE: v[0] = S.pointSize; return 1;
	case GL_COLOR_CLEAR_VALUE: memcpy(v, S.clearColor, 16); return 4;
	case GL_DEPTH_CLEAR_VALUE: v[0] = (float)S.clearDepth; return 1;
	case GL_FOG_COLOR: memcpy(v, S.fogColor, 16); return 4;
	case GL_FOG_DENSITY: v[0] = S.fogDensity; return 1;
	case GL_FOG_START: v[0] = S.fogStart; return 1;
	case GL_FOG_END: v[0] = S.fogEnd; return 1;
	case GL_FOG_MODE: v[0] = (float)S.fogMode; return 1;
	case GL_LIGHT_MODEL_AMBIENT: memcpy(v, S.modelAmb, 16); return 4;
	case GL_MATRIX_MODE: v[0] = (float)S.matrixMode; return 1;
	case GL_MODELVIEW_STACK_DEPTH: v[0] = (float)(depthM[0] + 1); return 1;
	case GL_PROJECTION_STACK_DEPTH: v[0] = (float)(depthM[1] + 1); return 1;
	case GL_TEXTURE_STACK_DEPTH: v[0] = (float)(depthM[2] + 1); return 1;
	case GL_TEXTURE_BINDING_2D: v[0] = (float)S.texBinding; return 1;
	case GL_LIST_BASE: v[0] = (float)S.listBase; return 1;
	case GL_LIST_INDEX: v[0] = compiling ? (float)compileName : 0; return 1;
	case GL_LIST_MODE: v[0] = compiling ? (float)compileMode : 0; return 1;
	case GL_BLEND_SRC: v[0] = (float)S.blendSrc; return 1;
	case GL_BLEND_DST: v[0] = (float)S.blendDst; return 1;
	case GL_DEPTH_FUNC: v[0] = (float)S.depthFunc; return 1;
	case GL_DEPTH_WRITEMASK: v[0] = S.depthMask; return 1;
	case GL_ALPHA_TEST_FUNC: v[0] = (float)S.alphaFunc; return 1;
	case GL_ALPHA_TEST_REF: v[0] = S.alphaRef; return 1;
	case GL_CULL_FACE_MODE: v[0] = (float)S.cullMode; return 1;
	case GL_FRONT_FACE: v[0] = (float)S.frontFace; return 1;
	case GL_POLYGON_MODE: v[0] = (float)S.polyMode[0]; v[1] = (float)S.polyMode[1]; return 2;
	case GL_POLYGON_OFFSET_FACTOR: v[0] = S.offFactor; return 1;
	case GL_POLYGON_OFFSET_UNITS: v[0] = S.offUnits; return 1;
	case GL_SHADE_MODEL: v[0] = (float)S.shadeModel; return 1;
	case GL_COLOR_WRITEMASK: for (int i = 0; i < 4; i++) v[i] = S.colorMask[i]; return 4;
	case GL_UNPACK_ALIGNMENT: v[0] = (float)unpackAlign; return 1;
	case GL_PACK_ALIGNMENT: v[0] = (float)packAlign; return 1;
	case GL_MAX_LIGHTS: v[0] = 8; return 1;
	case GL_MAX_MODELVIEW_STACK_DEPTH: case GL_MAX_PROJECTION_STACK_DEPTH: case GL_MAX_TEXTURE_STACK_DEPTH:
	case GL_MAX_ATTRIB_STACK_DEPTH: case GL_MAX_LIST_NESTING: v[0] = 64; return 1;
	case GL_MAX_TEXTURE_SIZE:
	{
		ensureFrame();
		int32_t m = 2048;
		gasm_gl_get_integerv(ES_MAX_TEXTURE_SIZE, &m, 1);
		v[0] = (float)m; return 1;
	}
	case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: v[0] = 8; return 1;
	case GL_DEPTH_BITS: v[0] = 24; return 1;
	case GL_STENCIL_BITS: v[0] = 8; return 1;
	default:
	{
		bool *f = enableFlag(pname);
		if (f) { v[0] = *f; return 1; }
		setError(GL_INVALID_ENUM);
		return 0;
	}
	}
}

void APIENTRY glGetFloatv(GLenum pname, GLfloat *params) { float v[16]; int n = query(pname, v); for (int i = 0; i < n; i++) params[i] = v[i]; }
void APIENTRY glGetDoublev(GLenum pname, GLdouble *params) { float v[16]; int n = query(pname, v); for (int i = 0; i < n; i++) params[i] = v[i]; }
void APIENTRY glGetBooleanv(GLenum pname, GLboolean *params) { float v[16]; int n = query(pname, v); for (int i = 0; i < n; i++) params[i] = v[i] != 0; }
void APIENTRY glGetIntegerv(GLenum pname, GLint *params)
{
	float v[16];
	int n = query(pname, v);
	bool color = pname == GL_CURRENT_COLOR || pname == GL_COLOR_CLEAR_VALUE || pname == GL_FOG_COLOR || pname == GL_LIGHT_MODEL_AMBIENT;
	for (int i = 0; i < n; i++)
		params[i] = color ? (GLint)(v[i] * 2147483647.0) : (GLint)floorf(v[i] + .5f);
}

/* lighting and fog */
void APIENTRY glLightfv(GLenum light, GLenum pname, const GLfloat *p)
{
	float a[6] = { (float)light, (float)pname, p[0], 0, 0, 0 };
	int n = (pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR || pname == GL_POSITION) ? 4 : pname == GL_SPOT_DIRECTION ? 3 : 1;
	for (int i = 1; i < n; i++) a[2 + i] = p[i];
	if (rec(OP_LIGHT, a)) return;
	exLight(light, pname, a + 2);
}
void APIENTRY glLightf(GLenum light, GLenum pname, GLfloat param) { float p[4] = { param, 0, 0, 0 }; glLightfv(light, pname, p); }

void APIENTRY glLightModelfv(GLenum pname, const GLfloat *p)
{
	float a[5] = { (float)pname, p[0], 0, 0, 0 };
	if (pname == GL_LIGHT_MODEL_AMBIENT) for (int i = 1; i < 4; i++) a[1 + i] = p[i];
	if (rec(OP_LIGHTMODEL, a)) return;
	if (pname == GL_LIGHT_MODEL_AMBIENT) memcpy(S.modelAmb, a + 1, 16);
	else if (pname == GL_LIGHT_MODEL_LOCAL_VIEWER) S.localViewer = p[0] != 0;
	else if (pname == GL_LIGHT_MODEL_TWO_SIDE) S.twoSide = p[0] != 0;
}
void APIENTRY glLightModelf(GLenum pname, GLfloat param) { float p[4] = { param, 0, 0, 0 }; glLightModelfv(pname, p); }
void APIENTRY glLightModeli(GLenum pname, GLint param) { glLightModelf(pname, (float)param); }

void APIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat *p)
{
	float a[6] = { (float)face, (float)pname, p[0], 0, 0, 0 };
	if (pname != GL_SHININESS) for (int i = 1; i < 4; i++) a[2 + i] = p[i];
	if (rec(OP_MATERIAL, a)) return;
	exMaterial(face, pname, a + 2);
}
void APIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param) { float p[4] = { param, 0, 0, 0 }; glMaterialfv(face, pname, p); }
void APIENTRY glMateriali(GLenum face, GLenum pname, GLint param) { glMaterialf(face, pname, (float)param); }

void APIENTRY glColorMaterial(GLenum face, GLenum mode)
{
	if (rec2(OP_COLORMATERIAL, (float)face, (float)mode)) return;
	S.cmFace = face; S.cmMode = mode;
	applyColorMaterial();
}

void APIENTRY glFogfv(GLenum pname, const GLfloat *p)
{
	float a[5] = { (float)pname, p[0], 0, 0, 0 };
	if (pname == GL_FOG_COLOR) for (int i = 1; i < 4; i++) a[1 + i] = p[i];
	if (rec(OP_FOG, a)) return;
	exFog(pname, a + 1);
}
void APIENTRY glFogf(GLenum pname, GLfloat param) { float p[4] = { param, 0, 0, 0 }; glFogfv(pname, p); }
void APIENTRY glFogi(GLenum pname, GLint param) { glFogf(pname, (float)param); }
void APIENTRY glFogiv(GLenum pname, const GLint *params)
{
	float p[4];
	if (pname == GL_FOG_COLOR) for (int i = 0; i < 4; i++) p[i] = (float)(params[i] / 2147483647.0);
	else p[0] = (float)params[0];
	glFogfv(pname, p);
}

/* textures */
void APIENTRY glGenTextures(GLsizei n, GLuint *out)
{
	for (int i = 0; i < n; i++)
	{
		while (texs.count(nextTexName)) nextTexName++;
		out[i] = nextTexName;
		texObject(nextTexName);
		nextTexName++;
	}
}

void APIENTRY glDeleteTextures(GLsizei n, const GLuint *names)
{
	flushBatch();
	for (int i = 0; i < n; i++)
	{
		if (!names[i]) continue;
		std::map<GLuint, Tex>::iterator it = texs.find(names[i]);
		if (it == texs.end()) continue;
		if (it->second.es) gasm_gl_delete_texture(it->second.es);
		if (appliedProgramTex == it->second.es) appliedProgramTex = (uint32_t)-1;
		texs.erase(it);
		if (S.texBinding == names[i]) S.texBinding = 0;
	}
}

GLboolean APIENTRY glIsTexture(GLuint t) { return t && texs.count(t) ? GL_TRUE : GL_FALSE; }

void APIENTRY glBindTexture(GLenum target, GLuint texture)
{
	if (target != GL_TEXTURE_2D) return;
	if (rec1(OP_BINDTEX, (float)texture)) return;
	exBindTexture(texture);
}

void APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
	if (target != GL_TEXTURE_2D) return;
	float a[6] = { (float)target, (float)pname, params[0], 0, 0, 0 };
	if (rec(OP_TEXPARAM, a)) return;
	exTexParam(pname, a + 2);
}
void APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param) { float p[4] = { (float)param, 0, 0, 0 }; glTexParameterfv(target, pname, p); }
void APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param) { float p[4] = { param, 0, 0, 0 }; glTexParameterfv(target, pname, p); }

void APIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
	float a[6] = { (float)target, (float)pname, params[0], 0, 0, 0 };
	if (pname == GL_TEXTURE_ENV_COLOR) for (int i = 1; i < 4; i++) a[2 + i] = params[i];
	if (rec(OP_TEXENV, a)) return;
	if (pname == GL_TEXTURE_ENV_MODE) S.texEnvMode = (GLenum)params[0];
	else if (pname == GL_TEXTURE_ENV_COLOR) memcpy(S.texEnvColor, a + 2, 16);
}
void APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param) { float p[4] = { (float)param, 0, 0, 0 }; glTexEnvfv(target, pname, p); }
void APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param) { float p[4] = { param, 0, 0, 0 }; glTexEnvfv(target, pname, p); }

void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	(void)border;
	if (target != GL_TEXTURE_2D) return;
	if (type != GL_UNSIGNED_BYTE) { setError(GL_INVALID_ENUM); return; }
	GLenum base = baseFormat(internalformat);
	std::vector<uint8_t> rgba;
	unpackToRGBA(format, width, height, pixels, base, rgba, unpackAlign);
	texImageRGBA(level, base, width, height, rgba.empty() ? 0 : &rgba[0]);
}

void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	if (target != GL_TEXTURE_2D || type != GL_UNSIGNED_BYTE || width <= 0 || height <= 0) return;
	Tex &t = texObject(S.texBinding);
	if (level < 0 || level >= 16 || !t.lvl[level]) { setError(GL_INVALID_OPERATION); return; }
	flushBatch();
	std::vector<uint8_t> rgba;
	unpackToRGBA(format, width, height, pixels, t.base, rgba, unpackAlign);
	ensureEs(t);
	gasm_gl_tex_sub_image_2d(GL_TEXTURE_2D, level, xoffset, yoffset, width, height, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0], (uint32_t)rgba.size());
	if (level == 0 && t.cpuValid)
		for (int y = 0; y < height; y++)
			for (int x = 0; x < width; x++)
			{
				int dx = xoffset + x, dy = yoffset + y;
				if (dx < 0 || dy < 0 || dx >= t.w[0] || dy >= t.h[0]) continue;
				memcpy(&t.rgba0[((size_t)dy * t.w[0] + dx) * 4], &rgba[((size_t)y * width + x) * 4], 4);
			}
}

void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
	(void)border;
	if (target != GL_TEXTURE_2D || level < 0 || level >= 16) return;
	flushBatch();
	ensureFrame();
	Tex &t = texObject(S.texBinding);
	ensureEs(t);
	/* our framebuffer is RGBA8, so an RGBA copy always works; the shader keeps to the base format */
	gasm_gl_copy_tex_image_2d(GL_TEXTURE_2D, level, GL_RGBA8, x, y, width, height, 0);
	t.w[level] = width; t.h[level] = height; t.lvl[level] = width > 0 && height > 0;
	if (level == 0) { t.base = baseFormat((GLint)internalformat); t.cpuValid = false; }
}

void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (target != GL_TEXTURE_2D || level < 0 || level >= 16) return;
	flushBatch();
	ensureFrame();
	Tex &t = texObject(S.texBinding);
	if (!t.lvl[level]) { setError(GL_INVALID_OPERATION); return; }
	ensureEs(t);
	gasm_gl_copy_tex_sub_image_2d(GL_TEXTURE_2D, level, xoffset, yoffset, x, y, width, height);
	if (level == 0) t.cpuValid = false;
}

void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
	if (target != GL_TEXTURE_2D || type != GL_UNSIGNED_BYTE || !pixels) return;
	Tex *t = boundTex();
	if (!t || !t->lvl[0]) return;
	if (level != 0) return;	/* the game only reads level 0 */
	flushBatch();
	std::vector<uint8_t> rgba;
	readTexLevel0(*t, rgba);
	/* glGetTexImage rows go top to bottom in memory as uploaded (row 0 first) */
	packFromRGBA(rgba.empty() ? 0 : &rgba[0], t->w[0], t->h[0], format, t->base, pixels);
}

void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
	if (target != GL_TEXTURE_2D || level < 0 || level >= 16) return;
	Tex *t = boundTex();
	if (pname == GL_TEXTURE_WIDTH) *params = t ? t->w[level] : 0;
	else if (pname == GL_TEXTURE_HEIGHT) *params = t ? t->h[level] : 0;
	else if (pname == GL_TEXTURE_INTERNAL_FORMAT) *params = t ? (GLint)t->base : 1;
	else *params = 0;
}

void APIENTRY glPixelStorei(GLenum pname, GLint param)
{
	switch (pname)
	{
	case GL_UNPACK_ALIGNMENT: if (param == 1 || param == 2 || param == 4 || param == 8) unpackAlign = param; else setError(GL_INVALID_VALUE); break;
	case GL_PACK_ALIGNMENT: if (param == 1 || param == 2 || param == 4 || param == 8) packAlign = param; else setError(GL_INVALID_VALUE); break;
	default: break;
	}
}

void APIENTRY glReadBuffer(GLenum mode) { (void)mode; }	/* one buffer: our framebuffer */
void APIENTRY glDrawBuffer(GLenum mode) { (void)mode; }

void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
	if (type != GL_UNSIGNED_BYTE || width <= 0 || height <= 0 || !pixels) return;
	flushBatch();
	ensureFrame();
	std::vector<uint8_t> rgba((size_t)width * height * 4);
	gasm_gl_read_pixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0], (uint32_t)rgba.size());
	packFromRGBA(&rgba[0], width, height, format, GL_RGBA, pixels);
}

/* display lists */
GLuint APIENTRY glGenLists(GLsizei range)
{
	if (range <= 0) return 0;
	GLuint first = nextList;
	for (GLsizei i = 0; i < range; i++) lists[first + i];
	nextList += range;
	return first;
}

void APIENTRY glDeleteLists(GLuint list, GLsizei range)
{
	for (GLsizei i = 0; i < range; i++) lists.erase(list + i);
}

GLboolean APIENTRY glIsList(GLuint list) { return lists.count(list) ? GL_TRUE : GL_FALSE; }

void APIENTRY glNewList(GLuint list, GLenum mode)
{
	if (compiling || list == 0) { setError(list ? GL_INVALID_OPERATION : GL_INVALID_VALUE); return; }
	compiling = true;
	compileName = list;
	compileMode = mode;
	compileBuf.clear();
}

void APIENTRY glEndList(void)
{
	if (!compiling) { setError(GL_INVALID_OPERATION); return; }
	compiling = false;
	lists[compileName].swap(compileBuf);
	compileBuf.clear();
	if (compileName >= nextList) nextList = compileName + 1;
}

void APIENTRY glCallList(GLuint list)
{
	if (rec1(OP_CALLLIST, (float)list)) return;
	callList(list);
}

void APIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid *names)
{
	const uint8_t *p = (const uint8_t *)names;
	for (GLsizei i = 0; i < n; i++)
	{
		GLuint off;
		switch (type)
		{
		case GL_BYTE: off = (GLuint)(int)(int8_t)p[i]; break;
		case GL_UNSIGNED_SHORT: { uint16_t v; memcpy(&v, p + 2 * i, 2); off = v; break; }
		case GL_SHORT: { int16_t v; memcpy(&v, p + 2 * i, 2); off = (GLuint)(int)v; break; }
		case GL_INT: case GL_UNSIGNED_INT: { uint32_t v; memcpy(&v, p + 4 * i, 4); off = v; break; }
		default: off = p[i]; break;
		}
		if (rec1(OP_CALLOFFSET, (float)off)) continue;
		callList(S.listBase + off);
	}
}

void APIENTRY glListBase(GLuint base) { if (rec1(OP_LISTBASE, (float)base)) return; S.listBase = base; }

/* vertex arrays */
static ClientArray *clientArray(GLenum a)
{
	switch (a)
	{
	case GL_VERTEX_ARRAY: return &caVertex;
	case GL_NORMAL_ARRAY: return &caNormal;
	case GL_COLOR_ARRAY: return &caColor;
	case GL_TEXTURE_COORD_ARRAY: return &caTex;
	default: return 0;
	}
}

void APIENTRY glEnableClientState(GLenum a) { ClientArray *c = clientArray(a); if (c) c->on = true; }
void APIENTRY glDisableClientState(GLenum a) { ClientArray *c = clientArray(a); if (c) c->on = false; }

static void setPointer(ClientArray &c, GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
	c.size = size; c.type = type; c.stride = stride; c.ptr = (const uint8_t *)ptr;
}

void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { setPointer(caVertex, size, type, stride, p); }
void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const GLvoid *p) { setPointer(caNormal, 3, type, stride, p); }
void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { setPointer(caColor, size, type, stride, p); }
void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { setPointer(caTex, size, type, stride, p); }

/* An array element goes through the immediate-mode calls, so lists record it dereferenced */
void APIENTRY glArrayElement(GLint i)
{
	float v[4];
	if (caNormal.on && caNormal.ptr) { fetch(caNormal, i, v, 3); glNormal3f(v[0], v[1], v[2]); }
	if (caColor.on && caColor.ptr) { v[3] = 1; fetch(caColor, i, v, 4); glColor4f(v[0], v[1], v[2], caColor.size == 4 ? v[3] : 1); }
	if (caTex.on && caTex.ptr) { v[1] = 0; fetch(caTex, i, v, 2); texCoord4(v[0], caTex.size > 1 ? v[1] : 0, 0, 1); }
	if (caVertex.on && caVertex.ptr)
	{
		v[2] = 0; v[3] = 1;
		fetch(caVertex, i, v, 4);
		glVertex4f(v[0], v[1], caVertex.size > 2 ? v[2] : 0, caVertex.size > 3 ? v[3] : 1);
	}
}

void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	if (count <= 0 || !caVertex.on) return;
	/* the current colour, normal and texture coordinate are left as they were */
	float color[4], normal[3], tex[4];
	memcpy(color, S.color, 16); memcpy(normal, S.normal, 12); memcpy(tex, S.tex, 16);
	Material mat[2];
	memcpy(mat, S.mat, sizeof mat);
	glBegin(mode);
	for (GLsizei i = 0; i < count; i++) glArrayElement(first + i);
	glEnd();
	if (!compiling)
	{
		memcpy(S.color, color, 16); memcpy(S.normal, normal, 12); memcpy(S.tex, tex, 16);
		if (caColor.on) memcpy(S.mat, mat, sizeof mat);
	}
}

void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	if (count <= 0 || !caVertex.on) return;
	const uint8_t *p = (const uint8_t *)indices;
	glBegin(mode);
	for (GLsizei i = 0; i < count; i++)
	{
		GLint e;
		if (type == GL_UNSIGNED_SHORT) { uint16_t v; memcpy(&v, p + 2 * i, 2); e = v; }
		else if (type == GL_UNSIGNED_INT) { uint32_t v; memcpy(&v, p + 4 * i, 4); e = (GLint)v; }
		else e = p[i];
		glArrayElement(e);
	}
	glEnd();
}

} /* extern "C" */

namespace gl1 {
struct Init { Init() { initState(); } };
static Init init;
}
