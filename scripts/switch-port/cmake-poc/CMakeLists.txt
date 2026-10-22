# Standalone CMake project to real-build (configure + compile + link, not just
# -fsyntax-only) the `common` library for Nintendo Switch using devkitPro's
# Switch.cmake toolchain, without touching the shared top-level CMakeLists.txt
# (which builds curl/decompiler/goalc/tools/tests that don't belong on-device
# and that we don't want to risk breaking for the PC build).
#
# Usage (from repo root), after `pacman -S switch-tools devkitA64 libnx` etc.:
#   cmake -S scripts/switch-port/cmake-poc -B build-switch-poc -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake
#   cmake --build build-switch-poc
cmake_minimum_required(VERSION 3.13)
project(jak_switch_poc CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(JAK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

add_compile_definitions(__SWITCH__ _GNU_SOURCE)
include_directories(${JAK_ROOT})

function(build_third_party_lib dir_name target_name)
    add_subdirectory(${JAK_ROOT}/third-party/${dir_name} ${CMAKE_BINARY_DIR}/third-party/${dir_name})
endfunction()

include_directories(${JAK_ROOT}/third-party/fmt/include)
build_third_party_lib(fmt fmt)

build_third_party_lib(lzokay lzokay)
# replxx deliberately NOT built here: needs <sys/termios.h>, doesn't exist on Switch (no TTY).
# common/CMakeLists.txt only links it on non-Switch targets - see repl_wrapper.h/.cpp.
build_third_party_lib(zstd libzstd_static)

include_directories(${JAK_ROOT}/third-party/tree-sitter/tree-sitter/lib/include)
build_third_party_lib(tree-sitter tree-sitter)

add_definitions(-DHAVE_USLEEP=1)
add_definitions(-DSQLITE_THREADSAFE=1)
add_definitions(-DSQLITE_ENABLE_JSON1)
add_definitions(-DSQLITE_ENABLE_RTREE)
if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    # sqlite3.c only pulls in <sys/mman.h> (not available in devkitA64's newlib) when WAL mode
    # and/or mmap I/O are compiled in - neither is used by this project's code (grepped for
    # journal_mode/WAL pragmas, found none), so disabling both avoids the dependency entirely
    # rather than patching the (heavily amalgamated, not meant to be hand-edited) sqlite3.c.
    add_definitions(-DSQLITE_OMIT_WAL=1)
    add_definitions(-DSQLITE_MAX_MMAP_SIZE=0)
    # dlopen()/dlfcn.h (loadable extensions) doesn't exist on Switch either, and this project
    # doesn't load SQLite extensions dynamically.
    add_definitions(-DSQLITE_OMIT_LOAD_EXTENSION=1)
    # HAVE_FCHOWN/geteuid() are disabled directly in sqlite3.c (guarded on __SWITCH__) rather
    # than via a command-line -D: the amalgamation only checks `defined(HAVE_FCHOWN)`, so
    # defining it as 0 here would still count as "defined" and re-enable the geteuid() call
    # that devkitA64's newlib doesn't provide.
endif()
build_third_party_lib(sqlite3 sqlite3)

# libtinyfiledialogs deliberately NOT built here: needs <sys/termios.h> too (its text-console
# fallback wants a TTY), and there's no Switch equivalent of native OS dialogs anyway. See
# common/util/dialogs.{h,cpp}.

include_directories(${JAK_ROOT}/third-party/draco/src)
add_subdirectory(${JAK_ROOT}/third-party/draco ${CMAKE_BINARY_DIR}/third-party/draco)
add_compile_definitions(TINYGLTF_ENABLE_DRACO)
build_third_party_lib(tiny_gltf tiny_gltf)

add_subdirectory(${JAK_ROOT}/common ${CMAKE_BINARY_DIR}/common)

# --- game/ and its remaining dependencies ---

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    find_package(SDL2 REQUIRED CONFIG)
endif()

include_directories(${JAK_ROOT}/third-party/glad/include)
include_directories(${JAK_ROOT}/third-party/SDL/include)  # only used on the non-Switch path
build_third_party_lib(imgui imgui)

build_third_party_lib(libco libco)
build_third_party_lib(stb_image stb_image)

add_subdirectory(${JAK_ROOT}/game ${CMAKE_BINARY_DIR}/game)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    nx_generate_nacp(gk.nacp NAME "OpenGOAL (Switch port research)" AUTHOR "jak-project + fork" VERSION "0.0.1")
    # A non-empty RomFS is included deliberately - gk.nro previously shipped with a zero-byte
    # RomFS (no ROMFS argument at all), which is suspected of tripping up the Homebrew Menu's
    # netloader when it receives/processes the file (see the development notes). The contents don't
    # matter, only that the section isn't empty.
    nx_create_nro(gk NACP gk.nacp ROMFS ${CMAKE_CURRENT_SOURCE_DIR}/romfs)
endif()
