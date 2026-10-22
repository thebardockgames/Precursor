# Android ARM64 runtime regression tests (AI-assisted)

These tests execute the actual `game/kernel/asm_funcs_arm64.s` routines and the
same generated trampolines used by Jak 1, Jak 2 and Jak 3. They do not require game
assets or start the APK. Run them on an Android ARM64 device; a passing x86 emitter
test does not validate ARM64 execution.

The fourteen built-in cases cover direct and argument-array C calls, the process-pointer
argument, mips2c context layout and stack restoration, and all three C-to-GOAL
entry points. Distinct 128-bit values verify saved vectors on GOAL-to-C calls;
`goal-vectors`, `goal8-vectors`, and `stack-vectors` separately poison V14/V15
to verify preservation at each C-to-GOAL entry. Arguments and return values
use nonzero upper 32 bits. The mips2c case
reserves 0, 16, 128 and 1024 bytes of fake GOAL stack. The `nothing` and `zero`
cases execute the same return-stub emitter used by all three kernels, starting
with a nonzero 64-bit value in X8. They check that `nothing` preserves it and
`zero` clears it, while preserving the saved vector registers. (AI-assisted)

From the repository root, configure and build with the Android NDK (adjust the
SDK/NDK paths to your installation):

```powershell
$sdkPath = 'D:/Android'
$ndkPath = "$sdkPath/ndk/29.0.14206865"
& "$sdkPath/cmake/3.22.1/bin/cmake.exe" -S test/arm64_runtime -B out/arm64-runtime-tests-build -G Ninja "-DCMAKE_MAKE_PROGRAM=$sdkPath/cmake/3.22.1/bin/ninja.exe" "-DCMAKE_TOOLCHAIN_FILE=$ndkPath/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
& "$sdkPath/cmake/3.22.1/bin/cmake.exe" --build out/arm64-runtime-tests-build
```

Run each case in a separate device process so that a signal in one case does not
prevent the others from running. With multiple devices, add `-s SERIAL` to adb.

```powershell
& "$sdkPath/platform-tools/adb.exe" push out/arm64-runtime-tests-build/arm64-runtime-tests /data/local/tmp/opengoal-arm64-runtime-tests
& "$sdkPath/platform-tools/adb.exe" shell chmod 755 /data/local/tmp/opengoal-arm64-runtime-tests
$failedCases = @()
foreach ($testCase in @('direct', 'array', 'pp', 'mips2c', 'goal', 'goal8', 'stack', 'nothing', 'zero', 'format', 'cache', 'goal-vectors', 'goal8-vectors', 'stack-vectors')) {
    & "$sdkPath/platform-tools/adb.exe" shell /data/local/tmp/opengoal-arm64-runtime-tests $testCase
    if ($LASTEXITCODE -ne 0) { $failedCases += $testCase }
}
if ($failedCases.Count) { throw "ARM64 runtime tests failed: $failedCases" }
```

On the connected device during the 2026-09-06 review, the original routines failed
all seven cases (mips2c terminated with SIGILL, exit 132). The corrected routines
pass all seven. This tests the native call boundary, not the entire GOAL compiler,
game startup, or graphical correctness.

On 2026-09-07 all nine cases pass on the connected ARM64 phone. The previous
`nothing` stub contained the x86 byte `C3`; the actual APK hit SIGILL at this
stub (symbolicated as its alias `lb-del`) after 234 mips2c calls. Both stubs now
emit ARM64 instructions and synchronize the instruction cache. (AI-assisted)

## Generated compiler/emitter regressions (AI-assisted)

### Jak 2 instanced collision regression (AI-assisted)

`collision-matrix-inverse` exports the actual Jak 2 matrix inversion body,
with minimal equivalent type layouts and vf0 supplied beside the input so
the raw code needs no relocations. Twenty-four device cases cover scaled
rotations, large translations, ignored W lanes, and in-place output. The
original emitter produced NaN/infinity even for identity: private V14/V15
scratch overwrote spill operands. Private scratch now uses V30/V31.

`vector-layout-spill.bin` repeats all blend masks and shuffle controls using
V14/V15 inputs, with separate and aliased destinations. It runs through the
existing `vector-layout` mode. `collision-sphere-box` tests 63 integer broad-phase
queries; it is not an exact sphere/triangle collision test.

```powershell
& out/build/Release/bin/goalc-test.exe --gtest_filter=ARM64Collision.*:ARM64EmitterEncoding.vector_layout
foreach ($fixture in @('collision-matrix-inverse', 'collision-sphere-box', 'vector-layout-spill')) {
    & "$sdkPath/platform-tools/adb.exe" push "out/arm64-runtime-generated/$fixture.bin" "/data/local/tmp/$fixture.bin"
    $mode = if ($fixture -eq 'vector-layout-spill') { 'vector-layout' } else { $fixture }
    & "$sdkPath/platform-tools/adb.exe" shell /data/local/tmp/opengoal-arm64-runtime-tests $mode "/data/local/tmp/$fixture.bin"
    if ($LASTEXITCODE -ne 0) { throw "Collision regression failed: $fixture" }
}
```

### Jak 2 intro spill regression (AI-assisted)

`spilled-store` executes a compiled GOAL byte store after an early-return branch,
with every temporary forced onto the stack. Four values on both paths check
that only destination byte 76 changes and all other bytes retain their canary.
The paired host allocator test reproduces the old input alias across a
control-flow gap on both x86 and ARM64. The device case passed on 2026-09-07.

Rebuild/push/chmod the native runner with the commands above, then run:

```powershell
& out/build/Release/bin/goalc-test.exe --gtest_filter=ARM64Allocator.*
& "$sdkPath/platform-tools/adb.exe" push out/arm64-runtime-generated/spilled-store-after-branch.bin /data/local/tmp/spilled-store-after-branch.bin
& "$sdkPath/platform-tools/adb.exe" shell /data/local/tmp/opengoal-arm64-runtime-tests spilled-store /data/local/tmp/spilled-store-after-branch.bin
if ($LASTEXITCODE -ne 0) { throw 'Spilled store regression failed' }
```

### Earlier generated cases (AI-assisted)

Six additional cases consume machine code exported by the host tests. Build
`goalc-test`, then run these commands from the repository root:

```powershell
& out/build/Release/bin/goalc-test.exe --gtest_filter=ARM64EmitterReturn.*:ARM64EmitterEncoding.*
foreach ($fixture in @('tail-jump-plain', 'tail-jump-stacked', 'wide-value-across-call', 'vector-math', 'vector-layout', 'scalar-math', 'packed-interleave')) {
    & "$sdkPath/platform-tools/adb.exe" push "out/arm64-runtime-generated/$fixture.bin" "/data/local/tmp/$fixture.bin"
}
& "$sdkPath/platform-tools/adb.exe" shell /data/local/tmp/opengoal-arm64-runtime-tests stack-return /data/local/tmp/tail-jump-stacked.bin
if ($LASTEXITCODE -ne 0) { throw 'Stacked continuation failed' }
& "$sdkPath/platform-tools/adb.exe" shell /data/local/tmp/opengoal-arm64-runtime-tests vector-math /data/local/tmp/vector-math.bin
if ($LASTEXITCODE -ne 0) { throw 'Vector arithmetic failed' }
& "$sdkPath/platform-tools/adb.exe" shell /data/local/tmp/opengoal-arm64-runtime-tests wide-return /data/local/tmp/wide-value-across-call.bin
if ($LASTEXITCODE -ne 0) { throw 'Wide value across call failed' }
foreach ($fixture in @('vector-layout', 'scalar-math', 'packed-interleave')) {
    & "$sdkPath/platform-tools/adb.exe" shell /data/local/tmp/opengoal-arm64-runtime-tests $fixture "/data/local/tmp/$fixture.bin"
    if ($LASTEXITCODE -ne 0) { throw "Generated regression failed: $fixture" }
}
```

`stack-return` runs actual compiled GOAL `.jr :stack-return #t` code. It must
return through the continuation pushed by the caller, yielding 99. For the
negative control, run the same command with `tail-jump-plain.bin`: it must
report 13 and exit 1. Both outcomes were observed on the phone on 2026-09-07.
The plain jump remains correct for `thread-resume`, which resumes a suspended
body instead of entering a new function.

`vector-math` executes the emitter's ADD/SUB/MUL/MIN/MAX/DIV on four finite
lanes, including negative and fractional inputs. It catches integer ADD
accidentally used for floats and unsupported FAMIN/FAMAX instructions; all 24
arithmetic results pass on the phone. It also copies V1 with unrelated data
in V0. The old MOV encoding omitted its second source field and ORed V0 in:
the four copied lanes became `-4, NaN, 16, 29` instead of `2, -3, 4, 5` on
the phone. After encoding both sources correctly, all 28 results pass.
NaNs, signed zero, PS2 saturation and graphical
correctness are not covered by this test. (AI-assisted)

The `format` case checks that unset format options promote to -1 even on
Android, where plain `char` is unsigned. The shared kernel format structure
now uses explicit signed bytes, preserving its 64-byte layout and string
access through `c_str()`. The old unsigned sentinel generated 255 padding
bytes in numeric output and malformed filenames. All twelve positive native
regressions have passed on the phone. (AI-assisted)

The thirteenth case, `cache`, rewrites and executes the same RWX allocation
256 times using the exact `CacheFlush` helper called by the linker. It does
not toggle memory protection, which could hide a missing instruction-cache
flush. This case passes on the phone. OpenGOAL objects require flushing each
relocated segment, not the original input buffer (`m_code_size` is zero for
v3 objects). (AI-assisted)

The fourteenth case, `wide-return`, executes a compiled GOAL function taking and
returning a uint128 across a callback. The callback poisons V0-V7. Before fixing
the call's register-clobber metadata, the result was `aaaaaaaaaaaaaaaa` in both
halves (FAIL); afterwards the exact `fedcba9876543210/123456789abcdef0` input
survives (PASS), both observed on the phone. ARM64 vector metadata indices
32-39 must decode to tagged V0-V7, not nonexistent general registers. (AI-assisted)

`vector-layout` checks all 16 blend masks and 256 shuffle controls, with separate
and aliased destinations (2240 exact 32-bit lane results). `scalar-math` checks
six float operations and square root. The old scalar square-root emitter put
the destination in Rm instead of Rd, changing the opcode: sqrt(16) into S2 left
16 instead of producing 4 on the phone. After fixing Rd, all sixteen positive
native cases have passed. These checks do not establish graphical correctness;
the title screen still has severely distorted geometry. (AI-assisted)

`packed-interleave` checks low/high interleaving of 8-, 16-, 32- and 64-bit
elements with distinct byte patterns and aliased destinations (384 bytes).
The six PEXT emitters incorrectly used UZP instead of ZIP: the old fixture
failed all six on the phone; after the fix all eight operations pass. The
title logo, island and font colors become recognizable after deploying this
fix. Seventeen reproducible positive native regressions now pass. (AI-assisted)

Two optional diagnostic modes, `vector-transform` and `matrix-multiply`, accept
raw ARM64 code for the real `vector-matrix*!` and `matrix*!` functions from
`goal_src/jak1/engine/math/matrix.gc`. Their fixtures are not generated by the
host tests above: export the functions with a temporary `(declare (print-asm))`
in an ARM64 game build, then extract each function's `RAW_BYTES` payload.
Both functions passed a nontrivial finite matrix/vector input on the phone.
These diagnostics are separate from the seventeen reproducible regressions. (AI-assisted)
