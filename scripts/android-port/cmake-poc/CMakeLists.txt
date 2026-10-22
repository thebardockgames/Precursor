# Standalone CMake project to real-build (configure + compile + link, not just -fsyntax-only) a
# headless `gk` runtime for Android using the NDK's own CMake toolchain file, without touching the
# shared top-level CMakeLists.txt (which builds goalc/decompiler/tools/tests that don't belong
# on-device and that we don't want to risk breaking for the PC build). Mirrors
# scripts/switch-port/cmake-poc/CMakeLists.txt.
#
# Unlike Switch, this doesn't need a graphics port: the runtime already has a first-class
# `--no-display` headless mode (see game/runtime.cpp's `enable_display` / game/main.cpp's
# `--no-display` CLI flag, already used by scripts/shell/gk.sh and the test suite) that skips
# Gfx::Init()/Gfx::Loop() entirely and goes straight to spawning the EE/kernel threads that load
# and JIT-execute GOAL code - exactly the code path we want to see crash with real Android
# diagnostics (logcat/tombstones/ndk-stack), since goalc's ARM64 JIT backend is still incomplete
# upstream (see the development notes). No SDL2 downgrade either - SDL3 has real, official Android
# support already vendored in third-party/SDL, so it just builds as-is even though we won't call
# into most of it at runtime.
#
# Usage (from repo root), after installing the Android NDK (this used r29/29.0.14206865, but any
# recent NDK should work):
#   cmake -S scripts/android-port/cmake-poc -B build-android-poc -G "Ninja" \
#     -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static
#   cmake --build build-android-poc
# Then: adb push build-android-poc/game/gk /data/local/tmp/gk && adb shell chmod +x /data/local/tmp/gk
#       adb shell /data/local/tmp/gk --no-display -- -fakeiso -debug -nokernel -nodisplay
cmake_minimum_required(VERSION 3.13)
project(jak_android_poc CXX C ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(JAK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

include_directories(${JAK_ROOT})

# common/util/crc32.h uses the ARMv8 CRC32 acle intrinsics (__crc32cw/__crc32cb) unconditionally
# on any __aarch64__ target, same as the x86-64 branch unconditionally uses SSE4.2's _mm_crc32_*.
# The default aarch64-linux-android baseline doesn't enable the (optional-per-spec, but present on
# effectively every real arm64-v8a device since ~2015) CRC extension, so it needs to be requested
# explicitly - same idea as the Switch build's "-march=armv8-a+crc+crypto".
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a+crc")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+crc")

function(build_third_party_lib dir_name target_name)
    add_subdirectory(${JAK_ROOT}/third-party/${dir_name} ${CMAKE_BINARY_DIR}/third-party/${dir_name})
endfunction()

include_directories(${JAK_ROOT}/third-party/fmt/include)
build_third_party_lib(fmt fmt)

build_third_party_lib(lzokay lzokay)
build_third_party_lib(zstd libzstd_static)
build_third_party_lib(replxx replxx)
build_third_party_lib(libtinyfiledialogs libtinyfiledialogs)

include_directories(${JAK_ROOT}/third-party/tree-sitter/tree-sitter/lib/include)
build_third_party_lib(tree-sitter tree-sitter)

add_definitions(-DHAVE_USLEEP=1)
add_definitions(-DSQLITE_THREADSAFE=1)
add_definitions(-DSQLITE_ENABLE_JSON1)
add_definitions(-DSQLITE_ENABLE_RTREE)
# Unlike Switch's newlib, bionic has <sys/mman.h> and <dlfcn.h>, so none of the
# SQLITE_OMIT_WAL/MAX_MMAP_SIZE/LOAD_EXTENSION flags from the Switch POC are needed here.
build_third_party_lib(sqlite3 sqlite3)

include_directories(${JAK_ROOT}/third-party/draco/src)
add_subdirectory(${JAK_ROOT}/third-party/draco ${CMAKE_BINARY_DIR}/third-party/draco)
add_compile_definitions(TINYGLTF_ENABLE_DRACO)
build_third_party_lib(tiny_gltf tiny_gltf)

add_subdirectory(${JAK_ROOT}/common ${CMAKE_BINARY_DIR}/common)

# --- game/ and its remaining dependencies ---

include_directories(${JAK_ROOT}/third-party/glad/include)
include_directories(${JAK_ROOT}/third-party/SDL/include)
build_third_party_lib(imgui imgui)

build_third_party_lib(libco libco)
build_third_party_lib(stb_image stb_image)
build_third_party_lib(cubeb cubeb)

# SDLOptions.cmake gates SDL_STATIC/SDL_SHARED on STATICALLY_LINK - force the static path so we
# get a single self-contained `gk` executable with no extra .so to adb push separately.
set(STATICALLY_LINK ON)
list(APPEND CMAKE_MODULE_PATH ${JAK_ROOT}/third-party/cmake/modules)
include(SDLOptions)
build_third_party_lib(SDL SDL3-static)

add_subdirectory(${JAK_ROOT}/game ${CMAKE_BINARY_DIR}/game)
