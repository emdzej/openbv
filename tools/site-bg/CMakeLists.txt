# The website's background (babos.wasm): a gasm guest, built with gasm's C SDK.
#   cmake -S tools/site-bg -B build-bg -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=<gasm C SDK>/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX=<wasi-sdk>
# (docs/scripts/build-bg.sh does this and copies it to docs/public/play/build/)
cmake_minimum_required(VERSION 3.20)
project(babos C)
include(Gasm)
gasm_add_game(babos babos.c)
target_compile_options(babos PRIVATE -O2 -Wall -Wextra)
target_link_options(babos PRIVATE -Wl,--strip-debug)
