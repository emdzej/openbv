# Building from source

## Requirements

- A [gasm](https://github.com/emdzej/gasm) checkout next to this one (`../gasm`), built with `make`:
  it provides the native runner (`runners/native/target/release/gasm-run`), the C/C++ SDK
  (`sdk/c`) and wasi-sdk (`tools/wasi-sdk`). `GASM_DIR=<path>` points `./play` elsewhere.
- CMake 3.24 or newer, git, a POSIX shell.

## ./play

```sh
./play                          # vanilla 2.11 data, fetched into ref/ the first time
./play --data=<dir>             # any folder laid out like the game's (bv2.db, main/...)
./play --master=host:port       # the master server (the Game Browser's list); none by default
./play --fresh                  # no saved settings: a throwaway storage folder
./play --profile=ana            # a second player on this machine, with saves of its own
./play --debug                  # the output also in build/logs/play-<time>.log
./play --headless 600 --screenshot /tmp/bv.png      # anything else goes to gasm-run
```

`WINDOW=1920x1080` sets the window's size (default 1600x900; it can be resized), `NET=host,...` the
hosts the game may reach without asking (default `127.0.0.1,localhost`).

## CMake

`./play` runs these:

```sh
GASM=../gasm
cmake -S . -B build-gasm -DOPENBV_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$GASM/sdk/c/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX=$GASM/tools/wasi-sdk
cmake --build build-gasm -j
$GASM/runners/native/target/release/gasm-run build-gasm/openbv.wasm \
  --asset-dir ref/BaboViolent2/BaboViolent2/Content --window 1600x900
```

The game's files are gasm assets: `--asset-dir` exposes a folder (names relative to it, looked up
case-insensitively), so the folder must hold `bv2.db` and `main/`.

## Headless runs

`gasm-run --headless N` runs N frames without a window and prints hashes of the video and audio;
`--input` scripts the mouse and keyboard by frame, `--screenshot` writes the last frame. The runs are
deterministic: the same build, data and input give the same hashes on gasm's native runner and on
its Node runner (`node ../gasm/runners/web/headless.mjs`). For example, hosting a game and
spawning (frames at 60 Hz; positions in the 1280x720 drawable):

```sh
gasm-run build-gasm/openbv.wasm --asset-dir ref/BaboViolent2/BaboViolent2/Content --headless 1640 \
  --input "610:PTR(630,54),620:PTR(630,54,L),625:PTR(630,54),700:PTR(266,101),720:PTR(266,101,L),740:PTR(266,101),1000:PTR(579,174),1010:PTR(579,174,L),1020:PTR(579,174),1060:PTR(309,174),1070:PTR(309,174,L),1080:PTR(309,174),1300:PTR(800,300),1310:PTR(800,300,L),1320:PTR(800,300),1600-1640:PTR(900,250,L)" \
  --screenshot /tmp/in-game.png
```

The site's screenshots come from `docs/scripts/screenshots.sh`, which runs the same scripts.
