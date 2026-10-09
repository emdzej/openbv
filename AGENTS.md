# AGENTS.md

Guidance for coding agents working in this repository. Humans: see [README.md](README.md) and
[openbv.emdzej.pl](https://openbv.emdzej.pl).

## What this is

openbv is **Babo Violent 2** (RndLabs, version 2.11) on [gasm](https://github.com/emdzej/gasm): the
original GPL-3.0 C++ source ([Daivuk/BaboViolent2](https://github.com/Daivuk/BaboViolent2), commit
`7171c84`), compiled to `openbv.wasm`, with everything below the game replaced by gasm backends. Sibling
projects with the same conventions: `../openrf`, `../opengta`, `../openballance`.

**Fidelity is the product — for gameplay.** Same rules, same feel, same assets. Presentation is the
exception, by the user's decision (2026-10-09): openbv is 16:9 throughout (`src/game/ui.h`: the UI canvas is
1066x600 units, the render resolution follows the window, default 1920x1080), and the menus are redesigned
(`src/game/UITheme.*`: flat panels, one accent colour, the Rubik font baked into the game's font format by
`tools/fonts/`, built into the module by `src/port/embedded_assets.c`); the HUD is next. The game code stays the original's; change
it only where the platform forces it, and keep the change small, marked and explained. Reproduce the
original's behaviour, quirks included. The reference is the Windows build of 2.11 (the source's `ProRelease`
configuration: `_PRO_`, `_MD5CODESEG_`); where the source has Windows and Linux branches that differ, the
Windows one is the behaviour to keep.

**State:** the client runs: menus, hosting a game (in-process server), local play, sound. Not yet: online
play (the Go server in `server/` is planned: WebSocket transport, several sessions per process, an admin
UI; its game rules are a Go port of `Server*.cpp` and the shared simulation), the master server list, the
browser player, gamepad buttons, the Prozac 2.1.3 data (renders without text: its font differs).

## Layout

| Path | What |
|---|---|
| `src/game/` | The game (BaboViolent2/Code), only the files the 2.11 project builds. UTF-8/LF, otherwise as upstream except edits marked `OPENBV_GASM` |
| `src/engine/dk/`, `src/engine/dko/` | The engine DLLs (DukZeven: dkc, dkf, dkgl, dki, dkp, dks, dksvar, dkt; dko models). `dkw.cpp` is replaced; `babonet/` is kept for reference only (not built) |
| `src/inc/` | The engine's public headers as the game sees them (copies of the engine's, as the original `BaboViolent2/inc` was) |
| `src/port/` | What replaces the platform: `dkw_gasm.cpp` (gasm entry points, window, loop, keyboard, mouse, joystick), `babonet_gasm.cpp` (baboNet over gasm:net WebSockets + in-process server), `fs_gasm.c` (fopen/opendir/stat over assets and storage, by `--wrap`), `sqlite_shim.cpp` (bv2.db read from the file format), `curl_shim.cpp`, `win_find.cpp` (FindFirstFile in NTFS order) |
| `src/port/gl1/` | OpenGL 1.x fixed function on gasm:gl (GLES 3); see its README |
| `src/port/fmod/` | FMOD 3 (FSOUND) on a software mixer, stb_vorbis; see its README |
| `src/game/UITheme.*`, `src/game/ui.h` | The redesigned menus' look and the 16:9 canvas (openbv's, not upstream's) |
| `assets/fonts/`, `tools/fonts/` | Our UI font (Rubik, OFL: `babo.tga` + licence), its baker (`bake_font.py`, Pillow in `.deps/py`) and `embed_assets.py`, which writes `src/port/embedded_assets.c` (the file layer serves it ahead of the game's data, so every runner gets it) |
| `src/port/include/` | Stand-ins for system headers (`LinuxHeader.h`, `dik.h`, `sqlite3.h`, `curl/curl.h`, `openssl/md5.h`, `win_find.h`), `openbv_link.h`, `engine_names.h` |
| `play` | Build and run locally (`./play --help`) |
| `docs/` | VitePress site, openbv.emdzej.pl |
| `tools/` | Packaging (`fetch-gasm-sdk.sh`, `fetch-gasm-runner.sh`, `fetch-content.sh`, `package-gasm.sh`, launchers), the site background guest (`tools/site-bg/`) |
| `tests/` | `gl1/` (a GL test guest), `audio/` (the FMOD shim against the real sounds, native) |

## Hard rules

1. **Never commit game data.** `game/` (Prozac 2.1.3, unzipped), `ref/` (upstream checkouts), `.deps/`,
   builds: git-ignored. Releases bundle the vanilla content, fetched at build time from the pinned upstream
   commit (`tools/fetch-content.sh`); git never has it.
2. **The engine/game boundary.** The original was separate DLLs; the engine and the game each have their own
   `CString`, `CVector*`, `CMatrix3x3f`, which differ (`EPSILON`, `toInt`, ...). The engine is compiled with
   `engine_names.h` force-included (its classes renamed), the game with its own; functions that pass those
   types between them carry `OPENBV_LINK(name)` on every declaration (src/inc, engine headers, internal
   headers). A new such function needs the same. Game code must not include engine-private headers.
3. **No blocking loops, no threads.** gasm calls `gasm_frame`; `bv2_gasm_frame` is one pass of the original
   `dkwMainLoop`. `CThread` runs its work inside `start()`.
4. **Determinism.** The same input gives the same frames and sound on every runner. Native gasm-run and the
   Node runner must print the same hash line.
5. Cite the original when replacing it (file and function), write in your own words.

## Build and run

```sh
./play                                     # build + window (vanilla data, fetched into ref/ if missing)
./play --fresh --headless 600 --screenshot /tmp/bv.png
```

Headless reference (vanilla data, no input): frame 600 is the main menu, hash line
`video_fnv32=1be66dd1 audio_fnv32=b56bc7ab`, the same on `node ../gasm/runners/web/headless.mjs`. A change
that alters it must be explained by the change.

Scripted play (1280x720 headless drawable; the game fills it at 16:9, its UI in 1066x600 units, src/game/ui.h): Host tab
`610:PTR(494,44),620:PTR(494,44,L),625:PTR(494,44)`, Start `700:PTR(116,95),720:PTR(116,95,L),740:PTR(116,95)`
(wait for the page to settle), weapon `1000:PTR(579,174),1010:PTR(579,174,L),1020:PTR(579,174)`, Auto assign
team `1060:PTR(309,174),...`, spawn by shooting `1300:PTR(800,300,L)`. The console is the backquote key.

Unattended runs: always `--headless`, never a window.

Network runs (a client against the Go server in `server/`) need `--realtime` (headless virtual time runs
far ahead of the network) and `--allow-net=127.0.0.1`. `--param netlog=1` prints every packet the client
sends and receives (`src/port/babonet_gasm.cpp`; off by default, hashes unchanged): host a game in the
client for the original server's sequence, compare with the Go server's.

## Data

Vanilla: `ref/BaboViolent2/BaboViolent2/Content` (bv2.db, main/). Prozac 2.1.3: `game/` (from
`~/Downloads/Babo Violent 2 Prozac 2.1.3.zip`; bv2p.exe there is the reference for Prozac's changes, via
Ghidra). The game's paths are relative to that folder (`main/maps/x.bvm`); files it writes go to gasm
storage (keys: `/` as `--`, lower case) and shadow the assets.

## Docs site

VitePress in `docs/`, published to openbv.emdzej.pl by `.github/workflows/pages.yml`. No emojis. The
"Built for gasm" badge is gasm's official one, hotlinked (don't recolor, stretch or crop). Don't overstate
the state. The home background is a gasm guest (`tools/site-bg/`), drawn procedurally: no game assets on the
site except screenshots of the port.

## Releases

Plain semver tags, no `v` prefix: bump `project(OpenBV VERSION ...)` in `CMakeLists.txt`, then tag. The
release workflow builds the wasm and the four gasm bundles (with the content), smoke-tests each on its OS
and attaches them.

## Commits

Imperative subject, body explaining why when non-obvious. Never commit `game/`, `ref/`, `.deps/`, `build*/`,
`out/`.
