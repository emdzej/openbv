**OpenBV** is Babo Violent 2 (RndLabs, 2.11) on [gasm](https://gasm.emdzej.pl): the game's own GPL-3.0 source
built as a WebAssembly module, with the game's content included. Each file has a `.sha256` next to it.

- `openbv-gasm-…-macos-universal.zip`: `OpenBV.app` for macOS 11+ (Apple Silicon and Intel).
- `openbv-gasm-…-linux-x86_64.tar.gz` / `openbv-gasm-…-linux-arm64.tar.gz`: Linux, start `./openbv.sh`.
- `openbv-gasm-…-windows-x86_64.zip`: Windows 10/11, start `OpenBV.cmd`.
- `openbv-….wasm`: the module alone, for your own `gasm-run` (gasm 0.13.0 or newer) with the game's content
  (`--asset-dir <folder with bv2.db and main/>`).

Each bundle is `openbv.wasm`, the game's content (`data/`: bv2.db and main/, from RndLabs' source release) and the
released gasm runner with ANGLE (OpenGL ES).

First run:

- macOS: the app is not notarized. The first time, right-click it → **Open** (or
  `xattr -dr com.apple.quarantine OpenBV.app`).
- Windows: extract the zip; if SmartScreen warns, **More info → Run anyway**.
- Linux: `tar xzf` keeps the executable bits; `./openbv.sh --install-desktop` adds a menu entry. Needs ALSA
  (`libasound2t64`) and a Vulkan or OpenGL capable driver.

Licenses: OpenBV is GPL-3.0; gasm-run is MIT; the game's content is RndLabs' (see `THIRD-PARTY` in each bundle).

Details: [openbv.emdzej.pl](https://openbv.emdzej.pl).
