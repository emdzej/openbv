cmake_minimum_required(VERSION 3.24)
project(OpenBV VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# Babo Violent 2 (2.11) on gasm. src/game, src/engine are the GPL-3.0 sources from
# github.com/Daivuk/BaboViolent2 (commit 7171c84), converted to UTF-8/LF; src/port holds what replaces
# the platform below them (window and loop, OpenGL 1.x, FMOD, input, sockets).
#
#   cmake -S . -B build-gasm -DOPENBV_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=$GASM/sdk/c/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX=$GASM/tools/wasi-sdk
set(OPENBV_PLATFORM native CACHE STRING "Platform: native (tests, tools) or gasm (the game)")
set_property(CACHE OPENBV_PLATFORM PROPERTY STRINGS native gasm)

file(GLOB GAME_SRC src/game/*.cpp src/game/*.c)
file(GLOB DK_SRC src/engine/dk/*.cpp)
file(GLOB DKO_SRC src/engine/dko/*.cpp)
file(GLOB GL1_SRC src/port/gl1/*.c src/port/gl1/*.cpp)
file(GLOB FMOD_SRC src/port/fmod/*.c src/port/fmod/*.cpp)
list(REMOVE_ITEM FMOD_SRC "${CMAKE_SOURCE_DIR}/src/port/fmod/fmodshim_stdio.c")
# dkw.cpp: replaced by src/port/dkw_gasm.cpp. CDkoObject.cpp: in no DLL's project. dko's CVector.cpp:
# the same code as the dk DLLs' copy, which the engine shares.
list(REMOVE_ITEM DK_SRC "${CMAKE_SOURCE_DIR}/src/engine/dk/dkw.cpp" "${CMAKE_SOURCE_DIR}/src/engine/dk/CDkoObject.cpp")
list(REMOVE_ITEM DKO_SRC "${CMAKE_SOURCE_DIR}/src/engine/dko/CVector.cpp")

# The two sides of the old DLL boundary see different headers, as the original projects did: the game
# its own CString/CVector and the public engine headers in src/inc, the engine its own copies (renamed
# by src/port/include/engine_names.h, see openbv_link.h).
set(PORT_INCLUDES src/port/include src/port/gl1/include src/port/gl1 src/port/fmod/include)
set(GAME_INCLUDES ${PORT_INCLUDES} src/inc src/game)
set(ENGINE_INCLUDES ${PORT_INCLUDES} src/engine/dk src/engine/dko)
# LINUX64: the original's non-Windows branches, with 32-bit INT4. _PRO_: the 2.11 ("Pro") build.
set(BV_DEFINES LINUX64 _PRO_ _MD5CODESEG_ OPENBV_GASM _GNU_SOURCE)

if(OPENBV_PLATFORM STREQUAL "gasm")
  include(Gasm)
  # The vendored code is 2000s C++: keep its warnings quiet.
  add_library(bv_engine OBJECT ${DK_SRC} ${DKO_SRC} src/port/dkw_gasm.cpp)
  target_include_directories(bv_engine PRIVATE ${ENGINE_INCLUDES} ${GASM_INCLUDE_DIR} ${GASM_SDK_INCLUDE_DIR})
  target_compile_definitions(bv_engine PRIVATE ${BV_DEFINES})
  target_compile_options(bv_engine PRIVATE -O2 -w $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions> -include ${CMAKE_SOURCE_DIR}/src/port/include/engine_names.h)

  add_library(bv_game OBJECT ${GAME_SRC} src/port/sqlite_shim.cpp src/port/curl_shim.cpp src/port/win_find.cpp src/port/babonet_gasm.cpp)
  target_include_directories(bv_game PRIVATE ${GAME_INCLUDES} ${GASM_INCLUDE_DIR} ${GASM_SDK_INCLUDE_DIR})
  target_compile_definitions(bv_game PRIVATE ${BV_DEFINES})
  target_compile_options(bv_game PRIVATE -O2 -w $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)

  gasm_add_game(openbv src/port/fs_gasm.c src/port/embedded_assets.c ${GL1_SRC} ${FMOD_SRC} ${GASM_VFILE_SOURCE}
    $<TARGET_OBJECTS:bv_engine> $<TARGET_OBJECTS:bv_game>)
  target_include_directories(openbv PRIVATE ${PORT_INCLUDES})
  target_compile_definitions(openbv PRIVATE ${BV_DEFINES})
  target_compile_options(openbv PRIVATE -O2)
  target_link_options(openbv PRIVATE -Wl,-z,stack-size=1048576 -Wl,--strip-debug
    # src/port/fs_gasm.c: the game's files are gasm assets and storage.
    -Wl,--wrap=fopen -Wl,--wrap=remove -Wl,--wrap=opendir -Wl,--wrap=readdir -Wl,--wrap=closedir -Wl,--wrap=stat)
  return()
endif()
