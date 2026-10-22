#!/bin/bash
# Syntax-only cross-compile sweep of common/ against the real Android NDK toolchain, to find
# portability issues without doing a full CMake port. Mirrors scripts/switch-port/check_common.sh.
# Requires: Android NDK installed, ANDROID_NDK env var set (defaults to /d/Android/ndk/<ver> on
# this machine - override if yours lives elsewhere).
set -u
cd "$(dirname "$0")/../.."

ANDROID_NDK="${ANDROID_NDK:-/d/Android/ndk/29.0.14206865}"
HOST_TAG=windows-x86_64
API=26
CXX="$ANDROID_NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/aarch64-linux-android${API}-clang++"
[ -x "$CXX" ] || CXX="$CXX.cmd"
OUT="/tmp/common_build_errors_android.txt"
> "$OUT"

INCLUDES="-I. -Ithird-party/fmt/include -Ithird-party -Ithird-party/zstd/lib -Ithird-party/replxx/include -Ithird-party/sqlite3 -Ithird-party/tree-sitter/tree-sitter/lib/include"
FLAGS="-std=c++20 -fPIE -D_GNU_SOURCE -fsyntax-only -w"

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
