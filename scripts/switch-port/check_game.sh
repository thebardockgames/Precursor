#!/bin/bash
# Syntax-only cross-compile sweep of game/ against the real devkitA64/libnx
# toolchain. See check_common.sh for the same idea applied to common/.
# Excludes overlord/ (untested PS2 IOP streaming layer), discord-rpc (not
# needed on Switch), and mips2c/asm (x86 assembly, unrelated to this check).
set -u
cd "$(dirname "$0")/../.."

DEVKITPRO="${DEVKITPRO:-/c/devkitPro}"
CXX="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++"
[ -x "$CXX" ] || CXX="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++.exe"
LIBNX="$DEVKITPRO/libnx/include"
OUT="/tmp/game_build_errors.txt"
> "$OUT"

INCLUDES="-I. -Ithird-party/fmt/include -Ithird-party -Ithird-party/zstd/lib -Ithird-party/replxx/include -Ithird-party/sqlite3 -Ithird-party/SDL/include -Ithird-party/glad/include -Ithird-party/cubeb/cubeb/include -Ithird-party/imgui -Ithird-party/mman -I$LIBNX"
FLAGS="-std=c++20 -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -DOS_POSIX -D_GNU_SOURCE -D__BYTE_ORDER=1234 -fsyntax-only -w"

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
