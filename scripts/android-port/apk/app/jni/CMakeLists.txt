# Native side of the real installable Android APK (scripts/android-port/apk), invoked by Gradle's
# externalNativeBuild (see ../build.gradle) via the Android Gradle Plugin's own CMake/NDK
# integration - NOT run standalone like scripts/android-port/cmake-poc, and NOT part of the
# shared top-level CMakeLists.txt (which builds goalc/decompiler/tools/tests that don't belong
# on-device). Otherwise mirrors cmake-poc closely - see that file's own comments for why each
# third-party dependency is built the way it is; the real differences from cmake-poc are:
#   1. SDL3 is built SHARED here (STATICALLY_LINK OFF), not static - Android JNI loads native code
#      as .so files, and SDLActivity.java's loadLibraries() specifically loads a separate
#      "libSDL3.so" before "libmain.so" (see SDLActivity.java's getLibraries()/getMainSharedObject()).
#      cmake-poc's headless executable has no such constraint and prefers one self-contained binary.
#   2. game/CMakeLists.txt's OPENGOAL_ANDROID_APK_BUILD option is turned on, which additionally
#      defines the `main` SHARED library target (-> libmain.so) SDLActivity.java expects, exporting
#      a real `SDL_main` symbol from the same main.cpp the headless `gk` executable also uses (see
#      that option's own comment in game/CMakeLists.txt and the matching guard in game/main.cpp).
cmake_minimum_required(VERSION 3.13)
project(opengoal_android_apk CXX C ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Gradle's debug APK otherwise runs the entire renderer/synth at -O0. Keep debug
# symbols and assertions, but optimize native code for real-time play. Set OFF
# explicitly when instruction-by-instruction source debugging is needed. (AI-assisted)
option(OPENGOAL_ANDROID_OPTIMIZE "Optimize the Android runtime, including debug APKs" ON)
if(OPENGOAL_ANDROID_OPTIMIZE)
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:-O2>" "$<$<COMPILE_LANGUAGE:CXX>:-O2>")
endif()

get_filename_component(JAK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

include_directories(${JAK_ROOT})

# See cmake-poc/CMakeLists.txt's identical comment: requests the CRC ACLE extension
# common/util/crc32.h needs unconditionally on any __aarch64__ target.
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

# SDL3 as a real shared library this time - see the file-level comment above for why.
set(STATICALLY_LINK OFF)
list(APPEND CMAKE_MODULE_PATH ${JAK_ROOT}/third-party/cmake/modules)
include(SDLOptions)
build_third_party_lib(SDL SDL3)

set(OPENGOAL_ANDROID_APK_BUILD ON CACHE BOOL "" FORCE)
add_subdirectory(${JAK_ROOT}/game ${CMAKE_BINARY_DIR}/game)

# Load the compiler and extractor only in the isolated installer. (AI-assisted)
option(OPENGOAL_ANDROID_LAUNCHER "Bundle the on-device ISO installer" OFF)
if(OPENGOAL_ANDROID_LAUNCHER)
    set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ZYDIS_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
    build_third_party_lib(zydis Zydis)
    build_third_party_lib(xdelta3 xdelta3)
    add_subdirectory(${JAK_ROOT}/decompiler ${CMAKE_BINARY_DIR}/decompiler EXCLUDE_FROM_ALL)
    add_subdirectory(${JAK_ROOT}/goalc ${CMAKE_BINARY_DIR}/goalc EXCLUDE_FROM_ALL)
    add_library(opengoal_installer SHARED installer.cpp ${JAK_ROOT}/decompiler/extractor/main.cpp)
    target_compile_definitions(opengoal_installer PRIVATE OPENGOAL_EXTRACTOR_LIBRARY)
    target_link_libraries(opengoal_installer compiler decomp common)
    add_executable(iso-reader-test EXCLUDE_FROM_ALL ${JAK_ROOT}/test/common/iso_reader_smoke.cpp)
    target_link_libraries(iso-reader-test common)
    add_executable(iso-probe-test EXCLUDE_FROM_ALL ${JAK_ROOT}/test/common/iso_probe_smoke.cpp)
    target_link_libraries(iso-probe-test decomp common)
endif()
