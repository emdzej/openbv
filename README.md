# openbv — Babo Violent 2, running again

**Docs and screenshots: [openbv.emdzej.pl](https://openbv.emdzej.pl)** ·
[Download](https://github.com/emdzej/openbv/releases)

[![Built for gasm](https://gasm.emdzej.pl/badge/built-for-gasm-flat.svg)](https://gasm.emdzej.pl)

Remember Babo Violent 2? Little balls with guns, top-down, a lot of shooting, a lot of laughing. RndLabs
released it as freeware in the 2000s, and in 2012 its authors published the source under the GPL. The
community kept it alive for years (the Prozac mod among others), but the game is a 32-bit Windows program
from another era: DirectInput, OpenGL 1.x, FMOD 3, raw sockets. Try running that on a Mac today.

So I ported it. openbv is the original 2.11 source, compiled to WebAssembly for the
[gasm](https://gasm.emdzej.pl) game runtime, which runs it natively on macOS, Linux and Windows (and,
soon, in the browser). The game code is the original, almost untouched: what changed is everything
underneath it. The rules, the maps, the weapons and the art are Babo Violent 2's own — the point is to
play the same game, not a remake of it.

Online play goes through a server of its own, written in Go, which hosts several games at once and comes
with an admin page. That part is in progress.

## Download

From the [releases](https://github.com/emdzej/openbv/releases), each file with a `.sha256`. The bundles
include `gasm-run` and the game's content (from the GPL source release), so there is nothing else to get:

| File | What |
|---|---|
| `openbv-gasm-<version>-macos-universal.zip` | The app (Apple Silicon and Intel) |
| `openbv-gasm-<version>-linux-x86_64.tar.gz`, `-linux-arm64.tar.gz` | `./openbv.sh` |
| `openbv-gasm-<version>-windows-x86_64.zip` | `OpenBV.cmd` |
| `openbv-<version>.wasm` | The module alone, for your own `gasm-run` |

## Build and play from source

You need CMake and a [gasm](https://github.com/emdzej/gasm) checkout next to this one (`../gasm`, built
with `make`), or the released SDK (`tools/fetch-gasm-sdk.sh` puts it and wasi-sdk into `.deps/`). Then:

```sh
./play                 # builds what changed and opens the window, with the vanilla 2.11 data
./play --help          # the options: other data, a fresh profile, a master server, logging
```

By hand:

```sh
cmake -S . -B build-gasm -DOPENBV_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=../gasm/sdk/c/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX=../gasm/tools/wasi-sdk
cmake --build build-gasm -j        # -> build-gasm/openbv.wasm
gasm-run build-gasm/openbv.wasm --asset-dir <the game's folder: bv2.db, main/>
```

How it works inside — the window and loop, the OpenGL 1.x layer, the FMOD shim, the network over
WebSockets — is on the [site](https://openbv.emdzej.pl).

## Licence

GPL-3.0-or-later, like the original source ([Daivuk/BaboViolent2](https://github.com/Daivuk/BaboViolent2),
Copyright 2012 bitHeads inc.). The game's art, sounds and maps are RndLabs' and come with their own terms
(`main/License.txt` in the content). stb_vorbis is public domain; gasm is MIT.

Babo Violent 2 belongs to its authors. This is a fan port, not affiliated with RndLabs.
