#!/bin/bash
# Syntax-only cross-compile sweep of common/ against the real devkitA64/libnx
# toolchain, to find portability issues without doing a full CMake port.
# Requires: devkitPro with devkitA64 + libnx installed, DEVKITPRO env var set
# (defaults to /c/devkitPro on Windows / /opt/devkitpro on Linux/macOS).
set -u
cd "$(dirname "$0")/../.."

DEVKITPRO="${DEVKITPRO:-/c/devkitPro}"
CXX="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++"
[ -x "$CXX" ] || CXX="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++.exe"
LIBNX="$DEVKITPRO/libnx/include"
OUT="/tmp/common_build_errors.txt"
> "$OUT"

INCLUDES="-I. -Ithird-party/fmt/include -Ithird-party -Ithird-party/zstd/lib -Ithird-party/replxx/include -Ithird-party/sqlite3 -Ithird-party/tree-sitter/tree-sitter/lib/include -I$LIBNX"
FLAGS="-std=c++20 -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -D_GNU_SOURCE -fsyntax-only -w"

total=0
fail=0
for f in $(find common -name "*.cpp"); do
  total=$((total+1))
  if ! "$CXX" $FLAGS $INCLUDES "$f" 2>>"$OUT" ; then
    fail=$((fail+1))
    echo "FAIL: $f" >> "$OUT"
  fi
done
echo "TOTAL=$total FAIL=$fail" | tee -a "$OUT"
echo "Full log: $OUT"
