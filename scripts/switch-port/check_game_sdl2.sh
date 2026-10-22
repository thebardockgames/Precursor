#!/bin/bash
# Same idea as check_game.sh, but pointed at devkitPro's real SDL2 headers instead of the
# project's vendored SDL3 (there's no open SDL3 backend for Switch - see the development notes).
# Validates the SDL2 port of game/graphics/pipelines/opengl.{cpp,h}, game/settings/settings.h,
# and game/system/hid/** against the actual SDL2 API surface, not just syntax in isolation.
set -u
cd "$(dirname "$0")/../.."

DEVKITPRO="${DEVKITPRO:-/c/devkitPro}"
CXX="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++"
[ -x "$CXX" ] || CXX="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++.exe"
LIBNX="$DEVKITPRO/libnx/include"
SDL2INC="$DEVKITPRO/portlibs/switch/include/SDL2"
OUT="/tmp/game_sdl2_build_errors.txt"
> "$OUT"

INCLUDES="-I. -Ithird-party/fmt/include -Ithird-party -Ithird-party/glad/include -Ithird-party/cubeb/cubeb/include -Ithird-party/imgui -Ithird-party/stb_image -Ithird-party/mman -I$SDL2INC -I$LIBNX"
FLAGS="-std=c++20 -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -D_GNU_SOURCE -fsyntax-only -w"

total=0
fail=0
for f in $(find game -name "*.cpp" | grep -v "overlord\|discord\|mips2c/asm"); do
  total=$((total+1))
  if ! "$CXX" $FLAGS $INCLUDES "$f" 2>>"$OUT" ; then
    fail=$((fail+1))
    echo "FAIL: $f" >> "$OUT"
  fi
done
echo "TOTAL=$total FAIL=$fail" | tee -a "$OUT"
echo "Full log: $OUT"
