# gl1: OpenGL 1.x on gasm:gl

The BaboViolent 2 code draws with OpenGL 1.x fixed function: immediate mode, display lists,
`glPushAttrib`, lighting, fog, GLU spheres. gasm offers OpenGL ES 3.0 with WebGL 2's rules
(`gasm:gl`). This directory is the bridge: the vendored code includes `<GL/gl.h>`,
`<GL/glu.h>` and `<GL/glext.h>` from `include/` unchanged and calls the real GL names.

## Build

Sources: `gl1.cpp`, `glu.cpp`. Include directories: `include/` (public: `GL/*.h`, `gl1.h`)
and this directory (`gl1_internal.h`), plus gasm's `gasm.h`. They build as `gnu++98` or later,
with `-fno-exceptions`. Do **not** link the C SDK's `gasm_gl.c` (`GASM_GL_SOURCE`) into the
same module: it defines `glEnable`, `glBindTexture`, ... with the same names.

```cmake
set(GL1_DIR ${CMAKE_SOURCE_DIR}/src/port/gl1)
target_sources(openbv PRIVATE ${GL1_DIR}/gl1.cpp ${GL1_DIR}/glu.cpp)
target_include_directories(openbv PRIVATE ${GL1_DIR}/include ${GL1_DIR})
```

## Hooks for the platform layer (`gl1.h`)

- `gl1_begin_frame(width, height)`: at the start of each gasm frame, with
  `gasm_gl_width()` / `gasm_gl_height()`. The game draws into an offscreen RGBA8 + depth24/stencil8
  framebuffer of that size: the default framebuffer has no alpha, and BV2 blends with
  `GL_DST_ALPHA` (`CdkoMaterial.cpp`) and copies the screen into textures. A new size makes a
  new (cleared) framebuffer. The viewport and scissor box start as the first size, as a new
  window's do; later sizes leave them to the game (`glViewport` on resize), as GL does.
- `gl1_end_frame()`: at the end of each gasm frame (the game's `SwapBuffers`): draws what is
  batched, blits the frame to the default framebuffer and makes it opaque.
- `gl1_flush()`: draw what is batched (`glFlush`/`glFinish` do the same).

Drawing before the first `gl1_begin_frame` creates the framebuffer at the drawable size.

## How it works

The imports are called directly (`gasm_gl_*` from `gasm.h`), so no GLES symbol clashes with the
GL 1.x entry points defined here.

Transform and lighting run on the CPU, as the GL 1.x specification states them, per vertex:
modelview to eye space, per-vertex lighting, fog factor, projection, texture matrix. The GPU
gets clip-space vertices (position, colour, texture coordinate, fog factor) and one shader:
texture environment, fog colour, alpha test, logic-op invert. Matrix, lighting, material and
colour changes never end a batch; a change of rasteriser state (texture, blend, depth, cull,
scissor, viewport, colour mask, polygon offset, alpha test, fog colour) does. Primitives are
assembled into triangle and line lists here (quads, strips, fans, polygons, loops). The maths is
float, so it is the same on every runner (wasm floats, wasi-libc's maths).

Display lists record the commands (geometry and state, `glCallList` inside lists,
`glCallLists` offsets resolved against `glListBase` at execution, as GL does) and replay them.
`glDrawArrays` / `glDrawElements` / `glArrayElement` go through the immediate-mode path, so
lists compile them dereferenced, as GL does.

Textures are converted to RGBA8 on upload laid out for their base format (luminance, alpha,
RGB, ...); the shader applies the base format's rules, so copies from the framebuffer
(`glCopyTexImage2D` with `GL_RGB`) behave too. Level 0 is kept on the CPU for
`glGetTexImage` (the font reads its alpha back, the blur reads RGB). `gluBuild2DMipmaps`
follows SGI GLU: sizes to the nearest power of two (rounding at 1.5x), box-filtered scaling,
2x2 box halving with GLU's rounding, every level uploaded. Completeness is GL 1.x's: an
incomplete texture (no image, or mipmap filtering without the levels) turns texturing off
instead of WebGL's black.

## Exact and approximate

Exact (as the GL 1.x spec defines them): the matrix stacks, `glOrtho`/`glFrustum`/`glRotate`,
GLU's `gluPerspective`, `gluLookAt`, `gluProject`, `gluUnProject` and `gluSphere` tessellation
and texture coordinates (SGI GLU's loops: pole fans without texturing, quad strips with it, the
line style for `GLU_LINE`/`GLU_SILHOUETTE`); per-vertex lighting (8 lights, ambient, diffuse,
Blinn specular with the infinite or local viewer, positional and directional lights,
attenuation, spot cutoff and exponent, emission, global ambient 0.2, `GL_RESCALE_NORMAL`,
`GL_NORMALIZE`, colour material); linear, exp and exp2 fog; `GL_MODULATE`, `GL_REPLACE`,
`GL_DECAL` and `GL_ADD`; alpha test; blending; depth, cull, scissor, colour mask, polygon offset
(fill); `glPushAttrib`/`glPopAttrib` for the current, enable, polygon, lighting, line, point,
fog, depth, colour, viewport, scissor, transform, texture and list groups; `glPolygonMode`
per face (`GL_LINE` draws polygon edges, with culling and facing decided here from the window
area); `GL_COLOR_LOGIC_OP` with `GL_INVERT` (1 - destination; also `GL_CLEAR`, `GL_NOOP`,
`GL_COPY`); wide lines as GL's aliased ones (widened along the minor axis, width rounded).

Approximate or missing:
- Fog distance is |z| in eye space and the factor is interpolated per vertex (what most
  GL 1.x drivers did); the spec allows either.
- `GL_CLAMP` is `GL_CLAMP_TO_EDGE` (no border colour in GLES); texture borders are ignored.
- Other logic ops draw as `GL_COPY`. Points (`GL_POINTS`, `GLU_POINT`, polygon mode
  `GL_POINT`) aren't drawn; the game draws none. Line stipple, smoothing, `GL_FLAT` shading,
  two-sided lighting, texgen and clip planes are state only.
- gluSphere's `GLU_FLAT` normals are drawn smooth; `GLU_INSIDE` flips the normals only.
- `gluBuild2DMipmaps` doesn't check proxy textures: sizes over `GL_MAX_TEXTURE_SIZE` are
  halved until they fit.
- `glReadBuffer(GL_FRONT)` reads the offscreen frame, which holds the last frame until the game
  clears it.
- Only `GL_UNSIGNED_BYTE` pixels; the unpack and pack alignment are honoured, row lengths and
  skips aren't.
- `glGetString(GL_EXTENSIONS)` is empty, so `dkglCheckExtension` finds nothing.

## Tests

`tests/gl1/` is a gasm guest with six panels (2D textures and blending, a lit textured babo
sphere, fog, display lists through `glCallLists`, client arrays, wireframe + wide lines + alpha
test + invert) and checks logged as `PASS`/`FAIL` (GLU round trips, attribute and matrix
stacks, `glGetTexImage`, no `gasm:gl` error under the shared WebGL model):

```sh
G=../gasm
cmake -S tests/gl1 -B build-gl1test -DCMAKE_TOOLCHAIN_FILE=$G/sdk/c/cmake/gasm-toolchain.cmake \
  -DWASI_SDK_PREFIX=$G/tools/wasi-sdk -DCMAKE_BUILD_TYPE=Release && cmake --build build-gl1test
$G/runners/native/target/release/gasm-run build-gl1test/gl1test.wasm --headless 5 --screenshot gl1test.png
node $G/runners/web/headless.mjs build-gl1test/gl1test.wasm --headless 5   # same hash line, null GL
```
