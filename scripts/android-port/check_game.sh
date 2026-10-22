#!/bin/bash
# Syntax-only cross-compile sweep of game/ (excluding overlord/, checked separately by
# check_overlord.sh) against the real Android NDK toolchain and the project's real vendored SDL3
# headers (SDL3 has official Android support - no SDL2 downgrade needed here, unlike Switch).
set -u
cd "$(dirname "$0")/../.."

ANDROID_NDK="${ANDROID_NDK:-/d/Android/ndk/29.0.14206865}"
HOST_TAG=windows-x86_64
API=26
CXX="$ANDROID_NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/aarch64-linux-android${API}-clang++"
[ -x "$CXX" ] || CXX="$CXX.cmd"
OUT="/tmp/game_build_errors_android.txt"
> "$OUT"

INCLUDES="-I. -Ithird-party/fmt/include -Ithird-party -Ithird-party/glad/include -Ithird-party/cubeb/cubeb/include -Ithird-party/imgui -Ithird-party/stb_image -Ithird-party/SDL/include"
FLAGS="-std=c++20 -fPIE -D_GNU_SOURCE -fsyntax-only -w"

total=0
fail=0
for f in $(find game -name "*.cpp" | grep -v "overlord"); do
  total=$((total+1))
  if ! "$CXX" $FLAGS $INCLUDES "$f" 2>>"$OUT" ; then
    fail=$((fail+1))
    echo "FAIL: $f" >> "$OUT"
  fi
done
echo "TOTAL=$total FAIL=$fail" | tee -a "$OUT"
echo "Full log: $OUT"
