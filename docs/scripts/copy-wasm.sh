#!/usr/bin/env bash
# Copy the game module into the browser player: build-gasm/openbv.wasm (or $OPENBV_WASM) ->
# docs/public/play/build/openbv.wasm (git-ignored). Build it first (./play does, or the cmake
# commands in CMakeLists.txt).
set -euo pipefail
cd "$(dirname "$0")/../.."
src=${OPENBV_WASM:-build-gasm/openbv.wasm}
[ -f "$src" ] || { echo "copy-wasm: no $src (build the game first)" >&2; exit 1; }
mkdir -p docs/public/play/build
cp "$src" docs/public/play/build/openbv.wasm
echo "copied $src ($(wc -c < "$src" | tr -d ' ') bytes)"
