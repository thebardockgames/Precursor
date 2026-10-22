#!/usr/bin/env bash
# Fast ARM64 test loop: compile -> sync only what changed -> run the game headless -> report.
# (AI-assisted)
#
# Runs the headless `gk` from scripts/android-port/cmake-poc (no APK, no window) on an Android
# device over adb. The device can be the x86_64 emulator, which runs ARM64 code through
# libndk_translation, or a real phone. See `devloop.sh -h`.
#
# Usage: scripts/android-port/devloop.sh [options]
#   -g GAME        jak1 (default), jak2, jak3
#   -s SERIAL      adb device serial (default: the only connected device, emulator first)
#   -c             compile GOAL for ARM64 first: goalc '(mi)'
#   -C FORM        compile with a custom goalc form instead, e.g. '(begin (make-cgo "GAME") (mi))'
#   -b             rebuild the headless gk with the NDK (incremental) and push it if it changed
#   -t SECONDS     how long to let the game run (default 120)
#   -H SECONDS     no-progress threshold before calling it a hang (default 40)
#   -a "ARGS"      game args after `--` (default: -boot -fakeiso -debug)
#   -n             skip syncing data (just run)
#   -S FILE        drive the running game with a REPL script (see devloop-scripts/). goalc runs
#                  (mi) and (lt) first, then each form of FILE; (script-wait SECONDS) pauses.
#                  The run ends a few seconds after the script finishes (or at -t).

set -uo pipefail
export MSYS_NO_PATHCONV=1

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && (pwd -W 2>/dev/null || pwd))"
GAME=jak1
SERIAL=""
COMPILE_FORM=""
BUILD_GK=0
RUN_SECONDS=120
HANG_SECONDS=40
GAME_ARGS="-boot -fakeiso -debug"
SYNC=1
SCRIPT=""

while getopts "g:s:cC:bt:H:a:nS:" opt; do
  case $opt in
    g) GAME=$OPTARG ;;
    s) SERIAL=$OPTARG ;;
    c) COMPILE_FORM='(mi)' ;;
    C) COMPILE_FORM=$OPTARG ;;
    b) BUILD_GK=1 ;;
    t) RUN_SECONDS=$OPTARG ;;
    H) HANG_SECONDS=$OPTARG ;;
    a) GAME_ARGS=$OPTARG ;;
    n) SYNC=0 ;;
    S) SCRIPT=$OPTARG ;;
    *) sed -n '2,24p' "$0"; exit 2 ;;
  esac
done

# ---- tools and paths (override with env vars) ----
SDK="${ANDROID_SDK:-C:/Program Files (x86)/Android/android-sdk}"
NDK="${ANDROID_NDK:-$SDK/ndk/29.0.14206865}"
CMAKE_BIN="${ANDROID_CMAKE_BIN:-$SDK/cmake/3.22.1/bin}"
ADB_BIN="${ADB:-$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe}"
[ -x "$ADB_BIN" ] || ADB_BIN="$SDK/platform-tools/adb.exe"
GOALC="$REPO/out/build/Release/bin/goalc.exe"
POC_BUILD="$REPO/out/android-poc"
# ARM64 data projects live outside the repo: $OPENGOAL_DATA, or ../OpenGoal next to it.
DATA_ROOT="${OPENGOAL_DATA:-$(dirname "$REPO")/OpenGoal}"
case $GAME in
  jak1) PROJ="${PROJ_PATH:-$DATA_ROOT/android-arm64/jak1/data}" ;;
  jak2) PROJ="${PROJ_PATH:-$DATA_ROOT/android-arm64/jak2/data}" ;;
  jak3) PROJ="${PROJ_PATH:-$DATA_ROOT/android-arm64/jak3/worktree-data}" ;;
  *) echo "unknown game $GAME"; exit 2 ;;
esac
REMOTE=/data/local/tmp/opengoal
STATE="$REPO/out/devloop"
mkdir -p "$STATE"

if [ -z "$SERIAL" ]; then
  SERIAL=$("$ADB_BIN" devices | tr -d '\r' | awk 'NR>1 && $2=="device" {print $1}' | sort -r | head -1)
  # `sort -r` puts emulator-* ahead of hardware serials
  [ -n "$SERIAL" ] || { echo "no adb device connected"; exit 2; }
fi
adb() { "$ADB_BIN" -s "$SERIAL" "$@"; }
# Under the emulator's ARM64 translation the process is named
# ndk_translation_program_runner_binfmt_misc_arm64, so find it by its command line.
remote_gk_pid() {
  adb shell 'for p in $(pgrep -f "gk -v --game"); do [ "$(cat /proc/$p/comm 2>/dev/null)" = gk ] && echo $p; done' |
    tr -d '\r' | head -1
}
log() { echo "[devloop $(date +%H:%M:%S)] $*"; }
STAMP=$(date +%Y%m%d-%H%M%S)
RUN_LOG="$STATE/$STAMP-$GAME-$SERIAL.log"

# ---- 1. compile GOAL ----
if [ -n "$COMPILE_FORM" ]; then
  log "goalc $GAME arm64: $COMPILE_FORM"
  t0=$(date +%s)
  "$GOALC" -g "$GAME" -a arm64 --proj-path "$PROJ" -c "$COMPILE_FORM" > "$STATE/last-goalc.log" 2>&1
  rc=$?
  if [ $rc -ne 0 ] || grep -qE "^-- Compilation Error|Compilation Error" "$STATE/last-goalc.log"; then
    grep -nE "Error|error" "$STATE/last-goalc.log" | head -20
    log "goalc FAILED (exit $rc), full log: $STATE/last-goalc.log"
    exit 1
  fi
  log "goalc ok in $(( $(date +%s) - t0 ))s ($(grep -oE 'built all [0-9]+ targets' "$STATE/last-goalc.log" | tail -1))"
fi

# ---- 2. build and push gk ----
if [ $BUILD_GK -eq 1 ]; then
  if [ ! -f "$POC_BUILD/build.ninja" ]; then
    log "configuring NDK build in $POC_BUILD"
    "$CMAKE_BIN/cmake.exe" -S "$REPO/scripts/android-port/cmake-poc" -B "$POC_BUILD" -G Ninja \
      -DCMAKE_MAKE_PROGRAM="$CMAKE_BIN/ninja.exe" \
      -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$STATE/last-gk-configure.log" 2>&1 ||
      { log "configure FAILED: $STATE/last-gk-configure.log"; exit 1; }
  fi
  log "building gk (NDK, incremental)"
  "$CMAKE_BIN/cmake.exe" --build "$POC_BUILD" --target gk -j 20 > "$STATE/last-gk-build.log" 2>&1 ||
    { grep -E "error" "$STATE/last-gk-build.log" | head -20; log "gk build FAILED: $STATE/last-gk-build.log"; exit 1; }
fi
GK_LOCAL="$POC_BUILD/game/gk"
[ -f "$GK_LOCAL" ] || { log "no gk at $GK_LOCAL, run with -b"; exit 1; }
gk_md5=$(md5sum "$GK_LOCAL" | cut -c1-32)
GK_CACHE="$STATE/$SERIAL-gk.md5"
[ -f "$GK_CACHE" ] || adb shell "md5sum $REMOTE/gk 2>/dev/null" | tr -d '\r' | cut -c1-32 > "$GK_CACHE"
if [ "$gk_md5" != "$(cat "$GK_CACHE")" ]; then
  log "pushing gk"
  adb shell "mkdir -p $REMOTE/config $REMOTE/data/out/$GAME/iso" >/dev/null
  adb push "$GK_LOCAL" "$REMOTE/gk" >/dev/null || { log "push gk FAILED"; exit 1; }
  adb shell "chmod 755 $REMOTE/gk"
  echo "$gk_md5" > "$GK_CACHE"
fi

# ---- 3. sync only the CGO/DGO files whose hash changed ----
if [ $SYNC -eq 1 ]; then
  LOCAL_ISO="$PROJ/out/$GAME/iso"
  CACHE="$STATE/$SERIAL-$GAME.md5"
  t0=$(date +%s)
  (cd "$LOCAL_ISO" && md5sum -- * | sed 's/ \*/  /' | sort -k2) > "$STATE/local-$GAME.md5"
  if [ ! -f "$CACHE" ]; then
    log "no sync cache for $SERIAL/$GAME yet, hashing files on the device once"
    adb shell "mkdir -p $REMOTE/data/out/$GAME/iso; cd $REMOTE/data/out/$GAME/iso && md5sum * 2>/dev/null" |
      tr -d '\r' | sort -k2 > "$CACHE"
  fi
  changed=$(comm -23 "$STATE/local-$GAME.md5" "$CACHE" | awk '{print $2}')
  n=$(echo -n "$changed" | grep -c . || true)
  if [ "$n" -gt 0 ]; then
    log "pushing $n changed file(s)"
    for f in $changed; do
      adb push "$LOCAL_ISO/$f" "$REMOTE/data/out/$GAME/iso/$f" >/dev/null || { log "push $f FAILED"; exit 1; }
      echo "    $f"
    done
  fi
  cp "$STATE/local-$GAME.md5" "$CACHE"
  log "sync done in $(( $(date +%s) - t0 ))s ($n changed)"
fi

# ---- 4. run headless and watch ----
log "running $GAME on $SERIAL for up to ${RUN_SECONDS}s (hang after ${HANG_SECONDS}s without progress)"
old=$(remote_gk_pid); [ -n "$old" ] && adb shell "kill -9 $old" >/dev/null 2>&1
adb shell "cd $REMOTE && timeout -s KILL $RUN_SECONDS ./gk -v --game $GAME --no-display \
  --proj-path $REMOTE/data --config-path $REMOTE/config -- $GAME_ARGS 2>&1; echo DEVLOOP_EXIT=\$?" \
  < /dev/null 2>&1 | tr -d '\r' > "$RUN_LOG" &
RUN_PID=$!

# Remember where the gk binary is mapped, to symbolicate native (C++) crash PCs afterwards.
GK_MAPS="$STATE/$STAMP-maps.txt"
for _ in 1 2 3 4 5 6 7 8 9 10; do
  sleep 1
  gpid=$(remote_gk_pid)
  [ -n "$gpid" ] && adb shell "grep -E '/gk\$' /proc/$gpid/maps 2>/dev/null" | tr -d '\r' > "$GK_MAPS" && [ -s "$GK_MAPS" ] && break
done

# REPL script: goalc compiles each form to ARM64 and sends it to the game over adb forward.
SCRIPT_PID=""
REPL_LOG="$STATE/$STAMP-repl.log"
if [ -n "$SCRIPT" ]; then
  [ -f "$SCRIPT" ] || { log "no script $SCRIPT"; exit 2; }
  adb forward tcp:8112 tcp:8112 < /dev/null >/dev/null
  full_script="$STATE/$STAMP-script.gc"
  { printf '(mi)\n(lt)\n(script-wait 1)\n'; cat "$SCRIPT"; printf '\n(script-wait 3)\n'; } > "$full_script"
  log "REPL script $SCRIPT (goalc output: $REPL_LOG)"
  "$GOALC" -g "$GAME" -a arm64 --proj-path "$PROJ" --script "$full_script" 2>&1 |
    sed -u 's/\x1b\[[0-9;]*m//g' > "$REPL_LOG" &
  SCRIPT_PID=$!
fi

# Progress = any new kernel dispatcher / link line. A hang = none of those for HANG_SECONDS.
progress_pat='kernel_dispatcher call #|link finish|Kernel dispatch time'
last_progress_lines=0
last_progress_time=$(date +%s)
result=ALIVE
script_finished=0
while kill -0 $RUN_PID 2>/dev/null; do
  sleep 2
  if [ -n "$SCRIPT_PID" ] && [ $script_finished -eq 0 ] && ! kill -0 $SCRIPT_PID 2>/dev/null; then
    script_finished=1
    log "REPL script finished, stopping the game"
    sleep 3
    gpid=$(remote_gk_pid); [ -n "$gpid" ] && adb shell "kill -9 $gpid" >/dev/null 2>&1
  fi
  if grep -q "=== TEMP DEBUG CRASH HANDLER ===" "$RUN_LOG"; then result=CRASH; fi
  cur=$(grep -cE "$progress_pat" "$RUN_LOG")
  now=$(date +%s)
  if [ "$cur" -ne "$last_progress_lines" ]; then
    last_progress_lines=$cur; last_progress_time=$now
  elif [ $result = ALIVE ] && [ $(( now - last_progress_time )) -ge "$HANG_SECONDS" ] && [ "$cur" -gt 0 ]; then
    result=HANG
    pid=$(remote_gk_pid)
    log "no progress for ${HANG_SECONDS}s, sampling the EE thread of pid $pid"
    for tid in $(adb shell "for t in /proc/$pid/task/*; do [ \"\$(cat \$t/comm)\" = EE ] && basename \$t; done" | tr -d '\r'); do
      adb shell "cat /proc/$pid/task/$tid/stat" > "$STATE/$STAMP-ee-stat-1.txt"
      adb shell "kill -USR2 $tid"; sleep 1; adb shell "kill -USR2 $tid"; sleep 1
      adb shell "cat /proc/$pid/task/$tid/stat" > "$STATE/$STAMP-ee-stat-2.txt"
    done
    sleep 2
    adb shell "kill -9 $pid" >/dev/null 2>&1
  fi
done
wait $RUN_PID 2>/dev/null
[ -n "$SCRIPT_PID" ] && kill $SCRIPT_PID 2>/dev/null
# Free host port 8112 again: goalc-test's Listener tests need it.
[ -n "$SCRIPT" ] && adb forward --remove tcp:8112 < /dev/null >/dev/null 2>&1
exit_line=$(grep -oE "DEVLOOP_EXIT=[0-9]+" "$RUN_LOG" | tail -1)
exit_code=${exit_line#DEVLOOP_EXIT=}
if [ $result = ALIVE ]; then
  case "$exit_code" in
    137) [ $script_finished -eq 1 ] && result="ALIVE (script finished)" ||
           result="ALIVE (stopped after ${RUN_SECONDS}s)" ;;
    0) result="EXITED (0)" ;;
    *) grep -q "CRASH HANDLER" "$RUN_LOG" && result=CRASH || result="EXITED ($exit_code)" ;;
  esac
fi

# ---- 5. report ----
echo
echo "================ devloop: $GAME on $SERIAL ================"
echo "result:      $result"
echo "frames:      $(grep -oE 'kernel_dispatcher call #[0-9]+ returned' "$RUN_LOG" | tail -1 | grep -oE '[0-9]+' || echo '?')"
echo "last links:  $(grep -oE 'link finish: [^ ]+' "$RUN_LOG" | tail -3 | sed 's/link finish: //' | tr '\n' ' ')"
errs=$(grep -E "\] (error|warn)\]|ERROR|kmalloc: !alloc|not enough stack" "$RUN_LOG" | grep -v "cpuid not implemented" | sort | uniq -c | sort -rn | head -8)
[ -n "$errs" ] && { echo "errors/warnings (count):"; echo "$errs" | sed 's/^/  /'; }
if grep -q "CRASH HANDLER ===" "$RUN_LOG"; then
  echo "crash:"
  sed -n '/=== TEMP DEBUG CRASH HANDLER ===/,/=== END TEMP DEBUG CRASH HANDLER ===/p' "$RUN_LOG" |
    grep -E "^signal|^fault addr|^pc:|^mips2c|best match|outside the GOAL heap" | head -15 | sed 's/^/  /'
  sed -n '/=== TEMP DEBUG CRASH HANDLER ===/,/=== END TEMP DEBUG CRASH HANDLER ===/p' "$RUN_LOG" > "$STATE/$STAMP-crash.txt"
  # Native PC/LR inside gk -> source line (GOAL code lives in the EE heap and is symbolicated above).
  if [ -s "$GK_MAPS" ]; then
    base=$(head -1 "$GK_MAPS" | cut -d- -f1)
    for reg in pc x30; do
      v=$(grep -oE "^$reg: 0x[0-9a-f]+" "$STATE/$STAMP-crash.txt" | head -1 | grep -oE '0x[0-9a-f]+')
      [ -n "$v" ] || continue
      while read -r range perms off _rest; do
        lo=$((16#${range%-*})); hi=$((16#${range#*-}))
        if [ $((v)) -ge $lo ] && [ $((v)) -lt $hi ]; then
          rel=$(printf '0x%x' $(( v - 16#$base )))
          sym=$("$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-addr2line.exe" -f -C -i -e "$GK_LOCAL" "$rel" | paste -sd' ')
          echo "  native $reg $v = gk+$rel: $sym"
          break
        fi
      done < "$GK_MAPS"
    done
  fi
  echo "  full dump: $STATE/$STAMP-crash.txt"
fi
if grep -q "SIGUSR2 SAMPLE" "$RUN_LOG"; then
  echo "hang samples (EE thread pc, symbolicated):"
  sed -n '/SIGUSR2 SAMPLE/,/bytes around pc/p' "$RUN_LOG" | grep -E '^pc:|best match|outside the GOAL heap' | sed 's/^/  /' | head -12
fi
if [ -n "$SCRIPT" ]; then
  echo "repl script: $(grep -c '\[script\] >' "$REPL_LOG") forms sent"
  grep -E "^GAMEPLAY:|Compilation Error|REPL Error|not responding|Failed to get version|^\[script-print\]" "$REPL_LOG" |
    uniq -c | sed 's/^/  /' | head -25
  echo "repl log:    $REPL_LOG"
fi
echo "log:         $RUN_LOG"
echo "=============================================================="
case "$result" in CRASH|HANG) exit 1 ;; esac
exit 0
