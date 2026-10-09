# Getting started

openbv is one WebAssembly module, `openbv.wasm`, that runs on gasm's native runner (`gasm-run`). It
needs the game's data (maps, models, textures, sounds): the files of the original's `main/` folder
and its `bv2.db`.

## From a release

The [releases](https://github.com/emdzej/openbv/releases) have bundles for macOS (universal), Linux
(x86_64, arm64) and Windows (x86_64): `gasm-run`, `openbv.wasm`, the game data and a launcher. Unpack
one and start the launcher: `OpenBV.app` on macOS (signed ad hoc, not notarized: right-click it and
choose Open the first time), `./openbv.sh` on Linux, `OpenBV.cmd` on Windows. Each bundle's README
says the same.

::: info
The first release is still being put together; until it's out, build from source (below).
:::

## From source

You need the [gasm](https://github.com/emdzej/gasm) repository next to this one (`../gasm`), built
with `make` (it brings the native runner and wasi-sdk), CMake 3.24 or newer, and git.

```sh
git clone https://github.com/emdzej/openbv.git && cd openbv
./play
```

`./play` builds what changed (`build-gasm/openbv.wasm`), fetches the vanilla 2.11 data from the
[original source repository](https://github.com/Daivuk/BaboViolent2) into `ref/` the first time, and
opens the game in a window. See [Building from source](./build) for its options and the CMake
commands behind it.

## Your settings

The game keeps `bv2.cfg` (every setting, including your key bindings and player name) and its
`bv2.db` changes in gasm's storage, under the id `openbv`: on macOS in
`~/Library/Application Support/gasm/openbv/`. `./play --fresh` starts without them.

## Next

- [Controls](./controls): the default keys.
- [Hosting a game](./hosting): play on your own server.
