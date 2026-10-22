#!/bin/bash
# Syntax-only cross-compile sweep of game/overlord/ (PS2 IOP-side disc streaming layer),
# the one part of game/ not covered by check_game.sh. Untested until now.
set -u
cd "$(dirname "$0")/../.."

DEVKITPRO="${DEVKITPRO:-/c/devkitPro}"
CXX="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++"
[ -x "$CXX" ] || CXX="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++.exe"
LIBNX="$DEVKITPRO/libnx/include"
OUT="/tmp/overlord_build_errors.txt"
> "$OUT"

INCLUDES="-I. -Ithird-party/fmt/include -Ithird-party -Ithird-party/zstd/lib -Ithird-party/replxx/include -Ithird-party/sqlite3 -Ithird-party/SDL/include -Ithird-party/glad/include -Ithird-party/cubeb/cubeb/include -Ithird-party/imgui -Ithird-party/mman -I$LIBNX"
FLAGS="-std=c++20 -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -D_GNU_SOURCE -fsyntax-only -w"

total=0
fail=0
for f in $(find game/overlord -name "*.cpp"); do
  total=$((total+1))
  if ! "$CXX" $FLAGS $INCLUDES "$f" 2>>"$OUT" ; then
    fail=$((fail+1))
    echo "FAIL: $f" >> "$OUT"
  fi
done
echo "TOTAL=$total FAIL=$fail" | tee -a "$OUT"
echo "Full log: $OUT"
