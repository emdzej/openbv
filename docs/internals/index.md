# Architecture

openbv is the Babo Violent 2 source (the 2.11 "Pro" build: `_PRO_`) compiled for WebAssembly, with
the layer below it replaced. The game and its engine are the original code, vendored from
[Daivuk/BaboViolent2](https://github.com/Daivuk/BaboViolent2) (commit `7171c84`) and converted to
UTF-8 with LF line ends; the changes in them are small, marked with `OPENBV_GASM`, and mostly
`#ifdef`s around Windows-only headers.

| Path | What |
|---|---|
| `src/game/` | The game: menus, client, server, game rules, maps, editor, console (the original `BaboViolent2/Code`) |
| `src/engine/dk/` | The "DukZeven" engine, originally a set of DLLs: `dkc` timer, `dkf` fonts, `dkgl` OpenGL helpers, `dki` input, `dkp` particles, `dks` sound, `dksvar` console variables, `dkt` textures |
| `src/engine/dko/` | `dko`: the DKO model format and renderer |
| `src/engine/babonet/` | The original network library (kept for reference, not built) |
| `src/inc/` | The engine's public headers as the game sees them |
| `src/port/` | What replaces the platform: below |

## The platform layer

**Entry and loop** (`src/port/dkw_gasm.cpp`, `src/game/main.cpp`). The original `WinMain` made a
window, initialised the engine modules and ran `while (dkwMainLoop());`, whose `WM_PAINT` called the
game's `paint()`: update at a fixed 30 Hz (`dkcInit(30)`), then render. On gasm the runner owns the
loop, so `WinMain` is cut in two: the start-up runs from `gasm_init`, one `paint()` from each
`gasm_frame` (60 Hz), the shutdown from `gasm_exit`. The `dkw` module is rewritten for gasm; it
also turns gasm's raw keyboard into DirectInput scan codes and the pointer into DirectInput-style
mouse deltas, which the unchanged `dki.cpp` reads. The game renders at the largest 16:9 size
that fits the window, in its pixels; its UI is laid out in 1066x600 units (`src/game/ui.h`), where the
original used 800x600. The menus are openbv's redesign (`src/game/UITheme.*`, the Rubik font built into
the module), the gameplay the original's.

**OpenGL 1.x** (`src/port/gl1/`). The game draws with fixed-function OpenGL: immediate mode,
display lists, `glPushAttrib`, lighting, fog, GLU spheres (every babo is one). gasm offers OpenGL ES
3.0 with WebGL 2's rules. `gl1` implements the GL 1.x API the game uses on top of it: transform,
lighting and fog on the CPU as the specification states them, one shader for texturing, fog colour,
alpha test and the invert logic op, batches that only break on rasteriser-state changes, display
lists recorded and replayed, `gluBuild2DMipmaps` and `gluSphere` as SGI's GLU does them. The game
draws into an RGBA framebuffer of its own, because it blends with destination alpha and gasm's
default framebuffer has none.

**Sound** (`src/port/fmod/`). FMOD 3's `FSOUND_*` API on a software mixer: 16 channels shared by
samples and streams, priority-based stealing, the 3D rolloff and pan computed from the listener the
game sets once a frame, PCM WAV samples and Ogg Vorbis music (stb_vorbis). `dks.cpp` is unchanged.
The mix is pushed to gasm at the game's mix rate (`s_mixRate`, 22050 Hz).

**Files** (`src/port/fs_gasm.c`). The game opens files relative to its folder (`main/bv2.cfg`,
`main\maps\x.bvm`). `fopen`, `remove`, `opendir`/`readdir` and `stat` are wrapped at link time: reads
come from gasm assets (the game's data, read-only), writes go to gasm storage (the key is the path
with `/` as `--`), and a stored file shadows the shipped one, so `bv2.cfg` and edited maps read
back. `src/port/win_find.cpp` provides `FindFirstFile`/`FindNextFile` over the same listing, in the
order NTFS returned names (case-insensitive), so the map lists and the rotation come out as on
Windows.

**bv2.db** (`src/port/sqlite_shim.cpp`). The game keeps a few settings in a 3 KB SQLite database and
runs eight fixed statements against it. The shim reads the tables straight from the shipped
`bv2.db` (the SQLite file format's table b-trees), answers those statements with SQLite's result
layout, and saves changes to storage. The master server row is replaced (see
[Networking](./networking)).

**Network** (`src/port/babonet_gasm.cpp`). baboNet, the game's network DLL, over WebSockets:
[Networking](./networking).

**The rest.** `curl_shim.cpp`: the account server the game posted to is gone, so requests fail as an
unreachable host did. Threads (`CThread`, used for those requests) run to completion inside
`start()`.

## Two copies of the same classes

The engine DLLs and the game each compiled their own `CString`, `CVector2i`..`CVector4f` and
`CMatrix3x3f`, and the copies differ: the engine's `EPSILON` (vector equality) is 0.01, the game's
0.0001; the engine parses numbers with `sscanf("%i")`, the game with `atoi`. Separate DLLs kept them
apart; one wasm module would merge them. So the engine is compiled with those names renamed
(`src/port/include/engine_names.h`: `CString` is `dkString` there, and so on), and the engine
functions that take them across the old DLL boundary (`dksvarRegister`, `dkglProject`,
`dkpCreateParticleEx`, ...) get one fixed link name on both sides with `OPENBV_LINK`
(`src/port/include/openbv_link.h`). The layouts are identical, as they had to be between the DLLs.

The game also leaned on MSVC accepting temporaries for non-const references
(`dkpCreateParticleEx(CVector3f(...), ...)`); those parameters are `const &` now, which changes
nothing at run time.

## Determinism

The game's timer reads the WASI clock, which gasm makes virtual in headless runs, and nothing else
in the port depends on wall time or threads. A headless run with the same data and scripted input
gives the same video and audio hashes on gasm's native runner and its Node runner.
