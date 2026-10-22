#!/bin/bash
# Syntax-only cross-compile sweep of game/overlord/ (PS2 IOP-side disc streaming layer), the one
# part of game/ not covered by check_game.sh. Mirrors scripts/switch-port/check_overlord.sh.
set -u
cd "$(dirname "$0")/../.."

ANDROID_NDK="${ANDROID_NDK:-/d/Android/ndk/29.0.14206865}"
HOST_TAG=windows-x86_64
API=26
CXX="$ANDROID_NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/aarch64-linux-android${API}-clang++"
[ -x "$CXX" ] || CXX="$CXX.cmd"
OUT="/tmp/overlord_build_errors_android.txt"
> "$OUT"

INCLUDES="-I. -Ithird-party/fmt/include -Ithird-party -Ithird-party/zstd/lib -Ithird-party/replxx/include -Ithird-party/sqlite3 -Ithird-party/SDL/include -Ithird-party/glad/include -Ithird-party/cubeb/cubeb/include -Ithird-party/imgui"
FLAGS="-std=c++20 -fPIE -D_GNU_SOURCE -fsyntax-only -w"

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
