#!/usr/bin/env python3
"""
Generates test/goalc/test_arm64_encoding_audit.cpp: a program that calls every function in
goalc/emitter/IGenARM64.cpp with representative, distinct register/immediate arguments and prints
the raw encoded bytes of each, labeled by function name. A companion script
(check_audit_output.py) feeds this through the NDK's own llvm-objdump and flags any function whose
output doesn't disassemble to a real instruction - the same technique used by hand this session to
find every ARM64 encoding bug. See the development notes.

Not a fully general C++ parser - a pragmatic best-effort extractor tuned to this file's actual
style (which is very consistent). Skips functions it can't confidently generate a call for
(unimplemented stubs, and a short manual EXCLUDE list built up by running this once and noting
which functions aborted).

NOTE: deliberately avoids ever writing a literal backslash-n sequence in this source file's own
string literals - some layer between authoring and disk has been observed to silently collapse
"backslash backslash n" / "backslash n" sequences into a real newline character before this file
even reaches the python interpreter, which previously produced a corrupt generated .cpp (an actual
newline landed inside a C string literal). NL below builds the two characters (backslash, n) via
chr(92) instead, sidestepping that entirely.
"""
import re

import os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SRC = f"{REPO}/goalc/emitter/IGenARM64.cpp"

NL = chr(92) + "n"  # the two characters: backslash, n - for C string literals, not python escapes

# Functions known to be unimplemented stubs, or that need inputs this script can't infer safely.
# Built up by running the generated program once and noting where it aborted.
EXCLUDE = {
    "shuffle_vf",  # ASSERT_MSG(false, "not yet implemented")
    # unconditional ASSERT_MSG(false, "not yet implemented") followed by unreachable-but-present
    # dead code that still calls Base(...) - the "no Base(...) call at all" stub heuristic below
    # can't tell these apart from a real implementation, so list them explicitly.
    "cdq",
    # internal helpers, not declared in IGenARM64.h / not reachable via emitter::IGen::ARM64::
    "branch_over",
    "tbl_16b",
}

# Per-function argument overrides: name -> list of C++ expressions (in declaration order,
# skipping nothing - full param list). Use when the generic inference below picks something that
# trips an ASSERT.
OVERRIDES = {
    "vpshuflw": ["Register(V2, true)", "Register(V5, true)", "0x88"],
    "vpshufhw": ["Register(V2, true)", "Register(V5, true)", "0x88"],
}

GPR_POOL = ["X2", "X5", "X9", "X14"]
SIMD_POOL = ["V2", "V5", "V9", "V14"]


def parse_functions(src: str):
    pattern = re.compile(r'InstructionARM64\s+(\w+)\s*\(([^)]*)\)\s*\{', re.S)
    out = []
    for m in pattern.finditer(src):
        name = m.group(1)
        params_raw = re.sub(r'\s+', ' ', m.group(2)).strip()
        start = m.end()
        depth = 1
        i = start
        while depth > 0 and i < len(src):
            if src[i] == '{':
                depth += 1
            elif src[i] == '}':
                depth -= 1
            i += 1
        body = src[start:i]
        params = []
        if params_raw:
            for p in params_raw.split(','):
                p = p.strip()
                if not p:
                    continue
                toks = p.rsplit(' ', 1)
                if len(toks) == 2:
                    typ, pname = toks[0].strip(), toks[1].strip()
                    params.append((typ, pname))
        out.append((name, params, body))
    return out


def classify_register(pname: str, body: str) -> str:
    if re.search(rf'\b{re.escape(pname)}\.is_128bit_simd', body):
        return "SIMD128"
    if re.search(rf'\b{re.escape(pname)}\.is_xmm', body):
        return "XMM"
    return "GPR"


def infer_numeric(pname: str, body: str, typ: str) -> str:
    m = re.search(rf'\b{re.escape(pname)}\s*==\s*(0x[0-9a-fA-F]+|\d+)', body)
    if m:
        return m.group(1)
    bounds = {
        "INT8_MIN": -128, "INT8_MAX": 127,
        "INT32_MIN": -(2**31), "INT32_MAX": 2**31 - 1,
        "UINT8_MAX": 255,
    }
    m = re.search(rf'\b{re.escape(pname)}\s*>=\s*(-?\w+)\s*&&\s*{re.escape(pname)}\s*<=\s*(-?\w+)', body)
    if m:
        lo_s, hi_s = m.group(1), m.group(2)
        lo = bounds.get(lo_s, None)
        hi = bounds.get(hi_s, None)
        try:
            if lo is None:
                lo = int(lo_s, 0)
            if hi is None:
                hi = int(hi_s, 0)
            for candidate in (0x22, 16, 8, 1):
                if lo <= candidate <= hi:
                    return str(candidate)
            return str((lo + hi) // 2)
        except ValueError:
            pass
    if pname == "offset":
        return "0x10"
    return "0x8"


def gen_call(name, params, body):
    if name in EXCLUDE:
        return None
    # A function is a real stub only if it never actually builds an instruction (no Base(...) call
    # anywhere) - some real, implemented functions have an unrelated `default: ASSERT(false);` in an
    # internal switch (e.g. splat_vf's VF_ELEMENT switch) and must NOT be excluded for that.
    if 'Base(' not in body and ('ASSERT_MSG(false' in body or re.search(r'ASSERT\s*\(\s*false\s*\)', body)):
        return None
    if name in OVERRIDES:
        args = OVERRIDES[name]
    else:
        args = []
        gpr_i = 0
        simd_i = 0
        for typ, pname in params:
            if typ == "Register":
                kind = classify_register(pname, body)
                if kind in ("SIMD128", "XMM"):
                    args.append("Register({}, true)".format(SIMD_POOL[simd_i % len(SIMD_POOL)]))
                    simd_i += 1
                else:
                    args.append(GPR_POOL[gpr_i % len(GPR_POOL)])
                    gpr_i += 1
            elif typ == "Register::VF_ELEMENT":
                args.append("Register::VF_ELEMENT::X")
            elif typ == "bool":
                args.append("true")
            else:
                args.append(infer_numeric(pname, body, typ))
    args_str = ", ".join(args)
    return '  run("{}", {}({}));'.format(name, name, args_str)


def main():
    src = open(SRC, encoding="utf-8").read()
    funcs = parse_functions(src)
    lines = []
    skipped = []
    for name, params, body in funcs:
        call = gen_call(name, params, body)
        if call is None:
            skipped.append(name)
            continue
        lines.append(call)

    header_lines = [
        "// GENERATED by scripts/android-port/arm64_audit/generate_audit.py - do not hand-edit.",
        "// Calls every function in goalc/emitter/IGenARM64.cpp with representative arguments and",
        "// prints its raw encoded bytes, labeled by function name, to stdout. A companion script",
        "// feeds this through the NDK's own llvm-objdump to flag anything that doesn't disassemble",
        "// to a real ARM64 instruction. See the development notes (\"automated ARM64 encoding audit\").",
        "#include <cstdio>",
        "",
        "#include \"goalc/emitter/IGenARM64.h\"",
        "#include \"goalc/emitter/Register.h\"",
        "",
        "using namespace emitter;",
        "using namespace emitter::IGen::ARM64;",
        "",
        "namespace {",
        "void run(const char* name, const InstructionARM64& instr) {",
        "  fprintf(stdout, \"FUNC %s" + NL + "\", name);",
        "  fflush(stdout);",
        "  for (int i = 0; i < instr.count; i++) {",
        "    fprintf(stdout, \"%08x" + NL + "\", instr.encodings[i]);",
        "  }",
        "  fprintf(stdout, \"END" + NL + "\");",
        "}",
        "}  // namespace",
        "",
        "int main() {",
    ]
    footer_lines = [
        "  fprintf(stdout, \"ALL_DONE" + NL + "\");",
        "  return 0;",
        "}",
        "",
    ]

    out_lines = header_lines + lines + footer_lines
    out = "\n".join(out_lines)
    out_path = f"{REPO}/test/goalc/test_arm64_encoding_audit.cpp"
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    print(f"Wrote {out_path} with {len(lines)} function calls ({len(skipped)} skipped).")
    if skipped:
        print("Skipped:", ", ".join(skipped))


if __name__ == "__main__":
    main()
