#!/usr/bin/env python3
"""
Companion to generate_audit.py. Reads the FUNC/hex/END-delimited output of
arm64-encoding-audit.exe (built from the generated test/goalc/test_arm64_encoding_audit.cpp),
assembles each raw 32-bit word as a ".inst 0x..." directive, disassembles it with the NDK's own
llvm-objdump, and flags any function whose output contains "<unknown>" or "undefined" - the
disassembler's way of saying the bytes aren't a real, allocated ARM64 instruction. This is the
same manual technique used by hand this session (print-asm + clang + llvm-objdump) to find every
ARM64 encoding bug so far, generalized to every function IGenARM64.cpp can emit in one pass.

It only catches STATIC encoding-table bugs (wrong bit fields, wrong option/size values, etc) -
NOT register-allocator-dependent bugs, since every argument here is a fixed, representative value
chosen by generate_audit.py rather than whatever a real compiled program would pick.

Usage:
    python3 check_audit_output.py <path-to-audit-run-output.txt>
"""
import re
import os
import tempfile
import subprocess
import sys

# Functions that intentionally embed raw literal data words (patched-at-link-time placeholders,
# or compile-time-constant mask/index tables for a "branch over literal, LDR <label>" pattern) in
# the middle of their instruction stream. A disassembler correctly can't decode literal data as a
# real instruction, so these always get flagged here even when 100% correct - confirmed by manual
# source review each time this list was extended. Not something the tool can safely auto-detect
# (some words of the same literal pool coincidentally decode as *something*, so "flagged" and
# "not flagged" words are mixed within a single function's output) - so it's a manually-reviewed
# allowlist instead. Extend it only after reading the source and confirming: real bug -> fix the
# code, not this list.
KNOWN_LITERAL_POOL_FALSE_POSITIVES = {
    "load_symbol_ptr_offset",
    "blend_vf",
    "swizzle_vf",
    "vpshuflw",
    "vpshufhw",
}

NDK_BIN = os.environ.get("ANDROID_NDK", "") + "/toolchains/llvm/prebuilt/windows-x86_64/bin"
CLANG = f"{NDK_BIN}/clang.exe"
OBJDUMP = f"{NDK_BIN}/llvm-objdump.exe"
SCRATCH = os.environ.get("AUDIT_SCRATCH", tempfile.gettempdir())


def parse_runs(text: str):
    runs = []
    name = None
    words = []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("FUNC "):
            name = line[len("FUNC "):]
            words = []
        elif line == "END":
            runs.append((name, words))
            name = None
        elif line == "ALL_DONE":
            break
        elif re.fullmatch(r"[0-9a-fA-F]{8}", line):
            words.append(line)
    return runs


def disassemble(words):
    asm_lines = [".text"] + [f".inst 0x{w}" for w in words]
    asm_path = f"{SCRATCH}/audit_check.s"
    obj_path = f"{SCRATCH}/audit_check.o"
    with open(asm_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(asm_lines) + "\n")
    subprocess.run(
        [CLANG, "--target=aarch64-linux-android21", "-c", asm_path, "-o", obj_path],
        check=True, capture_output=True,
    )
    result = subprocess.run(
        [OBJDUMP, "-d", "--no-show-raw-insn", obj_path],
        check=True, capture_output=True, text=True,
    )
    return result.stdout


def main():
    if len(sys.argv) != 2:
        print("usage: check_audit_output.py <audit-run-output.txt>")
        sys.exit(1)
    text = open(sys.argv[1], encoding="utf-8").read()
    runs = parse_runs(text)
    print(f"Checking {len(runs)} functions...")
    flagged = []
    for name, words in runs:
        if not words:
            print(f"  SKIP {name}: no encoding words captured")
            continue
        try:
            disasm = disassemble(words)
        except subprocess.CalledProcessError as e:
            flagged.append((name, "clang/objdump failed: " + e.stderr.decode(errors="replace")[:300]))
            continue
        bad_lines = [
            line for line in disasm.splitlines()
            if "<unknown>" in line or "undefined" in line.lower()
        ]
        if bad_lines:
            flagged.append((name, "\n".join(bad_lines)))

    real_flags = [(n, d) for n, d in flagged if n not in KNOWN_LITERAL_POOL_FALSE_POSITIVES]
    known_flags = [(n, d) for n, d in flagged if n in KNOWN_LITERAL_POOL_FALSE_POSITIVES]

    print()
    if known_flags:
        print(f"{len(known_flags)} function(s) flagged but explained (embedded literal pool, see "
              f"KNOWN_LITERAL_POOL_FALSE_POSITIVES): {', '.join(n for n, _ in known_flags)}")
    if not real_flags:
        print(f"\nAll {len(runs)} functions clean - no NEW unallocated encodings found.")
    else:
        print(f"\n{len(real_flags)} function(s) flagged with NO known explanation - investigate:")
        for name, detail in real_flags:
            print(f"\n=== {name} ===")
            print(detail)


if __name__ == "__main__":
    main()
