#!/usr/bin/env bash
# Build the home page's background (tools/site-bg/babos.c, a gasm guest) and put it where the page
# loads it: docs/public/play/build/babos.wasm (git-ignored).
# The C SDK and wasi-sdk: GASM_C_SDK and WASI_SDK, else .deps/ (tools/fetch-gasm-sdk.sh), else a
# gasm checkout next to this repository (../gasm).
set -euo pipefail
cd "$(dirname "$0")/../.."
if [[ -z "${GASM_C_SDK:-}" ]]; then
  if [[ -f .deps/gasm-c-sdk/cmake/gasm-toolchain.cmake ]]; then GASM_C_SDK=.deps/gasm-c-sdk
  else GASM_C_SDK=../gasm/sdk/c; fi
fi
if [[ -z "${WASI_SDK:-}" ]]; then
  if [[ -x .deps/wasi-sdk/bin/clang ]]; then WASI_SDK=.deps/wasi-sdk
  else WASI_SDK=../gasm/tools/wasi-sdk; fi
fi
if [[ ! -f build-bg/CMakeCache.txt ]]; then
  cmake -S tools/site-bg -B build-bg -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$(cd "$GASM_C_SDK" && pwd)/cmake/gasm-toolchain.cmake" \
    -DWASI_SDK_PREFIX="$(cd "$WASI_SDK" && pwd)" >/dev/null
fi
cmake --build build-bg >/dev/null
mkdir -p docs/public/play/build
cp build-bg/babos.wasm docs/public/play/build/babos.wasm
echo "docs/public/play/build/babos.wasm: $(wc -c < build-bg/babos.wasm | tr -d ' ') bytes"
