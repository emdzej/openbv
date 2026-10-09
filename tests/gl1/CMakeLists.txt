# The GL 1.x emulator's test scene, a gasm guest:
#   cmake -S tests/gl1 -B build-gl1test -DCMAKE_TOOLCHAIN_FILE=<gasm>/sdk/c/cmake/gasm-toolchain.cmake \
#     -DWASI_SDK_PREFIX=<gasm>/tools/wasi-sdk -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-gl1test
#   gasm-run build-gl1test/gl1test.wasm --headless 3 --screenshot gl1test.png
cmake_minimum_required(VERSION 3.20)
project(gl1test C CXX)
include(Gasm)

set(GL1_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src/port/gl1")
gasm_add_game(gl1test main.cpp "${GL1_DIR}/gl1.cpp" "${GL1_DIR}/glu.cpp")
target_include_directories(gl1test PRIVATE "${GL1_DIR}/include" "${GL1_DIR}")
set_target_properties(gl1test PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
target_compile_options(gl1test PRIVATE -Wall)
