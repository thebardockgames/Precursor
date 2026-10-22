/*!
 * @file test_CodeTester.cpp
 * Tests for the CodeTester, a tool for testing the emitter by emitting code and running it
 * from within the test application.
 *
 * These tests should just make sure the basic functionality of CodeTester works, and that it
 * can generate prologues/epilogues, and execute them without crashing.
 */

#include "goalc/emitter/CodeTester.h"
#include "goalc/emitter/IGen.h"
#include "goalc/emitter/InstructionSet.h"
#include "goalc/emitter/Register.h"
#include "gtest/gtest.h"

using namespace emitter;

namespace {
// ARM64's V (vector) registers numerically overlap X0..SP (see Register::m_is_arm64_vector's own
// comment in Register.h) - a bare `V3` used where a `Register` is expected is indistinguishable
// from `X3` without this explicit tag, which real compiler code always sets via exactly this
// pattern (see Register.cpp's own `vreg` lambda). Every ARM64 SIMD test below needs this, not the
// bare enumerator, or `is_128bit_simd()`/`is_xmm()` see a GPR and assert.
Register vreg(int id) {
  return Register(id, true);
}
}  // namespace

TEST(CodeTester, prologue_x86) {
  CodeTester tester;
  tester.init_code_buffer(256);
  tester.emit_push_all_gprs();
  // check we generate the right code for pushing all gpr's
  EXPECT_EQ(tester.dump_to_hex_string(),
            "50 51 52 53 54 55 56 57 41 50 41 51 41 52 41 53 41 54 41 55 41 56 41 57");
}

TEST(CodeTester, prologue_arm64) {
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(256);
  // tester.emit(IGen::push_gpr64(tester.generator(), ARM64_REG::X0));
  // EXPECT_EQ(tester.dump_to_hex_string(), "e0 8f 1f f8");
  tester.emit_push_all_gprs();
  // check we generate the right code for pushing all gpr's
  EXPECT_EQ(tester.dump_to_hex_string(),
            "e0 0f 1f f8 e1 0f 1f f8 e2 0f 1f f8 e3 0f 1f f8 e4 0f 1f f8 e5 0f 1f f8 e6 0f 1f f8 "
            "e7 0f 1f f8 e8 0f 1f f8 e9 0f 1f f8 ea 0f 1f f8 eb 0f 1f f8 ec 0f 1f f8 ed 0f 1f f8 "
            "ee 0f 1f f8 ef 0f 1f f8 f0 0f 1f f8 f1 0f 1f f8 f2 0f 1f f8 f3 0f 1f f8 f4 0f 1f f8 "
            "f5 0f 1f f8 f6 0f 1f f8 f7 0f 1f f8 f8 0f 1f f8 f9 0f 1f f8 fa 0f 1f f8 fb 0f 1f f8 "
            "fc 0f 1f f8 fd 0f 1f f8 fe 0f 1f f8");
}

TEST(CodeTester, epilogue_x86) {
  CodeTester tester;
  tester.init_code_buffer(256);
  tester.emit_pop_all_gprs();
  // check we generate the right code for popping all gpr's
  EXPECT_EQ(tester.dump_to_hex_string(),
            "41 5f 41 5e 41 5d 41 5c 41 5b 41 5a 41 59 41 58 5f 5e 5d 5c 5b 5a 59 58");
}

TEST(CodeTester, epilogue_arm64) {
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(256);
  tester.emit_pop_all_gprs();
  // check we generate the right code for popping all gpr's
  EXPECT_EQ(tester.dump_to_hex_string(),
            "fe 07 41 f8 fd 07 41 f8 fc 07 41 f8 fb 07 41 f8 fa 07 41 f8 f9 07 41 f8 f8 07 41 f8 "
            "f7 07 41 f8 f6 07 41 f8 f5 07 41 f8 f4 07 41 f8 f3 07 41 f8 f2 07 41 f8 f1 07 41 f8 "
            "f0 07 41 f8 ef 07 41 f8 ee 07 41 f8 ed 07 41 f8 ec 07 41 f8 eb 07 41 f8 ea 07 41 f8 "
            "e9 07 41 f8 e8 07 41 f8 e7 07 41 f8 e6 07 41 f8 e5 07 41 f8 e4 07 41 f8 e3 07 41 f8 "
            "e2 07 41 f8 e1 07 41 f8 e0 07 41 f8");
}

TEST(CodeTester, sub_gpr64_imm8_x86) {
  CodeTester tester;
  tester.init_code_buffer(256);
  for (int i = 0; i < 16; i++) {
    tester.emit(IGen::sub_gpr64_imm8s(tester.generator(), i, -1));
  }
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "4883E8FF4883E9FF4883EAFF4883EBFF4883ECFF4883EDFF4883EEFF4883EFFF4983E8FF4983E9FF4983EA"
            "FF4983EBFF4983ECFF4983EDFF4983EEFF4983EFFF");
}

TEST(CodeTester, sub_gpr64_imm8_arm64) {
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(256);
  for (int i = 0; i < 31; i++) {
    tester.emit(IGen::sub_gpr64_imm8s(tester.generator(), i, -1));
  }
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "0004009121040091420400916304009184040091A5040091C6040091E704009108050091290500914A0500"
            "916B0500918C050091AD050091CE050091EF0500911006009131060091520600917306009194060091B506"
            "0091D6060091F706009118070091390700915A0700917B0700919C070091BD070091DE070091");
}

TEST(CodeTester, add_gpr64_imm8_x86) {
  CodeTester tester;
  tester.init_code_buffer(256);
  for (int i = 0; i < 16; i++) {
    tester.emit(IGen::add_gpr64_imm8s(tester.generator(), i, -1));
  }
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "4883C0FF4883C1FF4883C2FF4883C3FF4883C4FF4883C5FF4883C6FF4883C7FF4983C0FF4983C1FF4983C2"
            "FF4983C3FF4983C4FF4983C5FF4983C6FF4983C7FF");
}

TEST(CodeTester, add_gpr64_imm8_arm64) {
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(256);
  for (int i = 0; i < 31; i++) {
    tester.emit(IGen::add_gpr64_imm8s(tester.generator(), i, -1));
  }
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "000400D1210400D1420400D1630400D1840400D1A50400D1C60400D1E70400D1080500D1290500D14A0500"
            "D16B0500D18C0500D1AD0500D1CE0500D1EF0500D1100600D1310600D1520600D1730600D1940600D1B506"
            "00D1D60600D1F70600D1180700D1390700D15A0700D17B0700D19C0700D1BD0700D1DE0700D1");
}

TEST(CodeTester, cmp_gpr64_gpr64_arm64) {
  // Regression test for two real bugs found this session in cmp_gpr64_gpr64's ARM64 encoding:
  // (1) the second operand was packed into the Rn field (bits [9:5]) instead of Rm (bits
  // [20:16]) - a copy-paste mistake (`Rn(b.id())` instead of `Rm(b.id())`); (2) the `option`
  // field (bits [15:13]) was hardcoded to UXTB (byte-extend) instead of UXTX (the correct option
  // for comparing two full 64-bit registers), silently truncating the second operand to 8 bits
  // before every comparison. Together these silently miscompiled ordinary GOAL `(if ...)`/`cond`
  // truthiness and equality checks - no assertion failure anywhere, only a real device crash deep
  // in ARM64 JIT'd GOAL code (`(defmethod new cpu-thread ...)`) gave it away. Uses varied,
  // non-matching (a, b) pairs specifically so a reintroduction of "both registers land in the
  // same field" would show up as a wrong Rm nibble here instead of accidentally matching (as it
  // can when b happens to be 0).
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(256);
  for (int i = 0; i < 31; i++) {
    tester.emit(IGen::cmp_gpr64_gpr64(tester.generator(), i, (i * 7 + 3) % 31));
  }
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "1F6023EB3F602AEB5F6031EB7F6038EB9F6020EBBF6027EBDF602EEBFF6035EB1F613CEB3F6124EB5F612BEB"
            "7F6132EB9F6139EBBF6121EBDF6128EBFF612FEB1F6236EB3F623DEB5F6225EB7F622CEB9F6233EBBF623A"
            "EBDF6222EBFF6229EB1F6330EB3F6337EB5F633EEB7F6326EB9F632DEBBF6334EBDF633BEB");
}

TEST(CodeTester, register_offset_loads_stores_arm64) {
  // Regression test for a real bug found this session: six ARM64 "load/store register (register
  // offset)" encodings hardcoded their `option` field (which extend/shift to apply to the index
  // register) to 0b000 = UXTB (extend from a byte) instead of 0b011 = UXTX (a plain 64-bit
  // register offset, the correct option whenever the index register isn't being extended/shifted
  // from a narrower value - every other register-offset function in this file already uses either
  // UXTX or the equally-valid SXTX). UXTB isn't just "wrong but valid" here - for these particular
  // (size, V, opc) combinations it produces a genuinely unallocated/illegal ARM64 instruction, not
  // a differently-behaving valid one. Confirmed on real hardware: this crashed with SIGILL inside
  // `matrix-identity!` (`goal_src/jak1/engine/math/matrix.gc`) - the first GOAL code in this
  // project's history to store a 128-bit value at a zero field offset, the first call shape that
  // actually reaches `storevf_gpr64_plus_gpr64`. `loadvf_gpr64_plus_gpr64` additionally passed
  // `addr1` for both `Rn` and `Rm` (never referencing the real second address register at all) -
  // caught only by comparing raw bytes against a real instruction assembled by the NDK's own
  // `clang` (`str/ldr <reg>, [x2, x5]` etc.), the same technique that found the original bug.
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(256);
  tester.emit(IGen::store16_gpr64_gpr64_plus_gpr64(tester.generator(), X2, X5, X9));
  tester.emit(IGen::load32u_gpr64_gpr64_plus_gpr64(tester.generator(), X9, X2, X5));
  tester.emit(IGen::load64_gpr64_gpr64_plus_gpr64(tester.generator(), X9, X2, X5));
  tester.emit(IGen::store32_xmm32_gpr64_plus_gpr64(tester.generator(), X2, X5, vreg(V9)));
  tester.emit(IGen::storevf_gpr64_plus_gpr64(tester.generator(), vreg(V9), X2, X5));
  tester.emit(IGen::loadvf_gpr64_plus_gpr64(tester.generator(), vreg(V9), X2, X5));
  EXPECT_EQ(tester.dump_to_hex_string(true),
            "49682578496865B8496865F8A96822BC4968A53C4968E53C");
}

TEST(CodeTester, store128_gpr64_simd128_offset_arm64) {
  // Regression test for a real bug found this session: `store128_gpr64_simd128_s8` and its
  // `_s32` sibling (used to back up a second-or-later callee-saved Q register at a non-zero
  // prologue stack offset - `CodeGenerator::do_goal_function_arm64`, via
  // `store128_xmm128_reg_offset`) declared their final STR's opcode literal
  // (`0b0011110110`, a 10-bit value - matches the *zero*-offset sibling
  // `store128_gpr64_simd128` just above, which correctly uses `Base(..., 10)`) with
  // `Base(..., 22)` instead - shifting it 12 bits too far and zeroing the instruction's entire
  // top byte, producing a word with nothing resembling a real STR encoding at all. Confirmed on
  // real hardware: SIGILL inside `update-math-camera`
  // (`goal_src/jak1/engine/gfx/math-camera.gc`), the first GOAL function needing to back up two
  // or more callee-saved XMM/Q registers.
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(256);
  tester.emit(IGen::store128_gpr64_simd128_s8(tester.generator(), X2, vreg(V9), 0x10));
  EXPECT_EQ(tester.dump_to_hex_string(true), "50000091104200910902803D");
}

TEST(CodeTester, simd_store_128_x86) {
  CodeTester tester;
  tester.init_code_buffer(256);
  //  movdqa [rbx], xmm3
  //  movdqa [r14], xmm3
  //  movdqa [rbx], xmm14
  //  movdqa [r14], xmm13
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), RBX, XMM3));
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), R14, XMM3));
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), RBX, XMM14));
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), R14, XMM13));
  EXPECT_EQ(tester.dump_to_hex_string(),
            "66 0f 7f 1b 66 41 0f 7f 1e 66 44 0f 7f 33 66 45 0f 7f 2e");

  tester.clear();
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), RSP, XMM1));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 0f 7f 0c 24");  // requires SIB byte.

  tester.clear();
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), R12, XMM13));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 45 0f 7f 2c 24");  // requires SIB byte and REX byte

  tester.clear();
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), RBP, XMM1));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 0f 7f 4d 00");

  tester.clear();
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), RBP, XMM11));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 44 0f 7f 5d 00");

  tester.clear();
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), R13, XMM2));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 41 0f 7f 55 00");

  tester.clear();
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), R13, XMM12));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 45 0f 7f 65 00");
}

TEST(CodeTester, simd_store_128_arm64) {
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(256);

  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), X2, vreg(V3)));
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), X14, vreg(V3)));
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), X2, vreg(V14)));
  tester.emit(IGen::store128_gpr64_simd128(tester.generator(), X14, vreg(V13)));
  EXPECT_EQ(tester.dump_to_hex_string(), "43 00 80 3d c3 01 80 3d 4e 00 80 3d cd 01 80 3d");
}

TEST(CodeTester, xmm_load_128_x86) {
  CodeTester tester;
  tester.init_code_buffer(256);

  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), XMM3, RBX));
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), XMM3, R14));
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), XMM14, RBX));
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), XMM13, R14));
  EXPECT_EQ(tester.dump_to_hex_string(),
            "66 0f 6f 1b 66 41 0f 6f 1e 66 44 0f 6f 33 66 45 0f 6f 2e");

  tester.clear();
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), XMM1, RSP));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 0f 6f 0c 24");  // requires SIB byte.

  tester.clear();
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), XMM13, R12));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 45 0f 6f 2c 24");  // requires SIB byte and REX byte

  tester.clear();
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), XMM1, RBP));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 0f 6f 4d 00");

  tester.clear();
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), XMM11, RBP));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 44 0f 6f 5d 00");

  tester.clear();
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), XMM2, R13));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 41 0f 6f 55 00");

  tester.clear();
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), XMM12, R13));
  EXPECT_EQ(tester.dump_to_hex_string(), "66 45 0f 6f 65 00");
}

TEST(CodeTester, xmm_load_128_arm64) {
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(256);

  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), vreg(V3), X1));
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), vreg(V3), X14));
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), vreg(V14), X1));
  tester.emit(IGen::load128_simd128_gpr64(tester.generator(), vreg(V13), X14));
  EXPECT_EQ(tester.dump_to_hex_string(), "23 00 c0 3d c3 01 c0 3d 2e 00 c0 3d cd 01 c0 3d");
}

void execute_tester(CodeTester& tester) {
  if (tester.generator().instr_set() == InstructionSet::ARM64) {
#ifdef __aarch64__
    tester.execute();
#endif
  } else if (tester.generator().instr_set() == InstructionSet::X86) {
#ifndef __aarch64__
    tester.execute();
#endif
  }
}

// These tests actually execute the code, you cannot execute arm64 code on x86 and vise versa
// so these tests have to be conditional based on the platform unfortunately.
TEST(CodeTester, execute_push_pop_simd_x86) {
  CodeTester tester;
  tester.init_code_buffer(512);
  tester.emit_push_all_simd();
  tester.emit_pop_all_simd();
  tester.emit_return();
  EXPECT_EQ(
      tester.dump_to_hex_string(),
      "48 83 ec 08 48 83 ec 10 66 0f 7f 04 24 48 83 ec 10 66 0f 7f 0c 24 48 83 ec 10 66 0f 7f 14 "
      "24 48 83 ec 10 66 0f 7f 1c 24 48 83 ec 10 66 0f 7f 24 24 48 83 ec 10 66 0f 7f 2c 24 48 83 "
      "ec 10 66 0f 7f 34 24 48 83 ec 10 66 0f 7f 3c 24 48 83 ec 10 66 44 0f 7f 04 24 48 83 ec 10 "
      "66 44 0f 7f 0c 24 48 83 ec 10 66 44 0f 7f 14 24 48 83 ec 10 66 44 0f 7f 1c 24 48 83 ec 10 "
      "66 44 0f 7f 24 24 48 83 ec 10 66 44 0f 7f 2c 24 48 83 ec 10 66 44 0f 7f 34 24 48 83 ec 10 "
      "66 44 0f 7f 3c 24 66 0f 6f 04 24 48 83 c4 10 66 0f 6f 0c 24 48 83 c4 10 66 0f 6f 14 24 48 "
      "83 c4 10 66 0f 6f 1c 24 48 83 c4 10 66 0f 6f 24 24 48 83 c4 10 66 0f 6f 2c 24 48 83 c4 10 "
      "66 0f 6f 34 24 48 83 c4 10 66 0f 6f 3c 24 48 83 c4 10 66 44 0f 6f 04 24 48 83 c4 10 66 44 "
      "0f 6f 0c 24 48 83 c4 10 66 44 0f 6f 14 24 48 83 c4 10 66 44 0f 6f 1c 24 48 83 c4 10 66 44 "
      "0f 6f 24 24 48 83 c4 10 66 44 0f 6f 2c 24 48 83 c4 10 66 44 0f 6f 34 24 48 83 c4 10 66 44 "
      "0f 6f 3c 24 48 83 c4 10 48 83 c4 08 c3");
  execute_tester(tester);
}

TEST(CodeTester, execute_push_pop_simd_arm64) {
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(512);
  tester.emit_push_all_simd();
  tester.emit_pop_all_simd();
  tester.emit_return();
  EXPECT_EQ(
      tester.dump_to_hex_string(),
      "ff 43 00 d1 e0 03 80 3d ff 43 00 d1 e1 03 80 3d ff 43 00 d1 e2 03 80 3d ff 43 00 d1 e3 03 "
      "80 3d ff 43 00 d1 e4 03 80 3d ff 43 00 d1 e5 03 80 3d ff 43 00 d1 e6 03 80 3d ff 43 00 d1 "
      "e7 03 80 3d ff 43 00 d1 e8 03 80 3d ff 43 00 d1 e9 03 80 3d ff 43 00 d1 ea 03 80 3d ff 43 "
      "00 d1 eb 03 80 3d ff 43 00 d1 ec 03 80 3d ff 43 00 d1 ed 03 80 3d ff 43 00 d1 ee 03 80 3d "
      "ff 43 00 d1 ef 03 80 3d e0 03 c0 3d ff 43 00 91 e1 03 c0 3d ff 43 00 91 e2 03 c0 3d ff 43 "
      "00 91 e3 03 c0 3d ff 43 00 91 e4 03 c0 3d ff 43 00 91 e5 03 c0 3d ff 43 00 91 e6 03 c0 3d "
      "ff 43 00 91 e7 03 c0 3d ff 43 00 91 e8 03 c0 3d ff 43 00 91 e9 03 c0 3d ff 43 00 91 ea 03 "
      "c0 3d ff 43 00 91 eb 03 c0 3d ff 43 00 91 ec 03 c0 3d ff 43 00 91 ed 03 c0 3d ff 43 00 91 "
      "ee 03 c0 3d ff 43 00 91 ef 03 c0 3d ff 43 00 91 c0 03 5f d6");
  execute_tester(tester);
}

TEST(CodeTester, execute_push_pop_all_the_things_x86) {
  CodeTester tester;
  tester.init_code_buffer(512);
  tester.emit_push_all_simd();
  tester.emit_push_all_gprs();

  // ...
  tester.emit_pop_all_gprs();
  tester.emit_pop_all_simd();
  tester.emit_return();
  EXPECT_EQ(tester.dump_to_hex_string(),
            "48 83 ec 08 48 83 ec 10 66 0f 7f 04 24 48 83 ec 10 66 0f 7f 0c 24 48 83 ec 10 66 0f "
            "7f 14 24 48 83 ec 10 66 0f 7f 1c 24 48 83 ec 10 66 0f 7f 24 24 48 83 ec 10 66 0f 7f "
            "2c 24 48 83 ec 10 66 0f 7f 34 24 48 83 ec 10 66 0f 7f 3c 24 48 83 ec 10 66 44 0f 7f "
            "04 24 48 83 ec 10 66 44 0f 7f 0c 24 48 83 ec 10 66 44 0f 7f 14 24 48 83 ec 10 66 44 "
            "0f 7f 1c 24 48 83 ec 10 66 44 0f 7f 24 24 48 83 ec 10 66 44 0f 7f 2c 24 48 83 ec 10 "
            "66 44 0f 7f 34 24 48 83 ec 10 66 44 0f 7f 3c 24 50 51 52 53 54 55 56 57 41 50 41 51 "
            "41 52 41 53 41 54 41 55 41 56 41 57 41 5f 41 5e 41 5d 41 5c 41 5b 41 5a 41 59 41 58 "
            "5f 5e 5d 5c 5b 5a 59 58 66 0f 6f 04 24 48 83 c4 10 66 0f 6f 0c 24 48 83 c4 10 66 0f "
            "6f 14 24 48 83 c4 10 66 0f 6f 1c 24 48 83 c4 10 66 0f 6f 24 24 48 83 c4 10 66 0f 6f "
            "2c 24 48 83 c4 10 66 0f 6f 34 24 48 83 c4 10 66 0f 6f 3c 24 48 83 c4 10 66 44 0f 6f "
            "04 24 48 83 c4 10 66 44 0f 6f 0c 24 48 83 c4 10 66 44 0f 6f 14 24 48 83 c4 10 66 44 "
            "0f 6f 1c 24 48 83 c4 10 66 44 0f 6f 24 24 48 83 c4 10 66 44 0f 6f 2c 24 48 83 c4 10 "
            "66 44 0f 6f 34 24 48 83 c4 10 66 44 0f 6f 3c 24 48 83 c4 10 48 83 c4 08 c3");
  execute_tester(tester);
}

TEST(CodeTester, execute_push_pop_all_the_things_arm64) {
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(512);
  tester.emit_push_all_simd();
  tester.emit_push_all_gprs();

  // ...
  tester.emit_pop_all_gprs();
  tester.emit_pop_all_simd();
  tester.emit_return();
  EXPECT_EQ(
      tester.dump_to_hex_string(),
      "ff 43 00 d1 e0 03 80 3d ff 43 00 d1 e1 03 80 3d ff 43 00 d1 e2 03 80 3d ff 43 00 d1 e3 03 "
      "80 3d ff 43 00 d1 e4 03 80 3d ff 43 00 d1 e5 03 80 3d ff 43 00 d1 e6 03 80 3d ff 43 00 d1 "
      "e7 03 80 3d ff 43 00 d1 e8 03 80 3d ff 43 00 d1 e9 03 80 3d ff 43 00 d1 ea 03 80 3d ff 43 "
      "00 d1 eb 03 80 3d ff 43 00 d1 ec 03 80 3d ff 43 00 d1 ed 03 80 3d ff 43 00 d1 ee 03 80 3d "
      "ff 43 00 d1 ef 03 80 3d e0 0f 1f f8 e1 0f 1f f8 e2 0f 1f f8 e3 0f 1f f8 e4 0f 1f f8 e5 0f "
      "1f f8 e6 0f 1f f8 e7 0f 1f f8 e8 0f 1f f8 e9 0f 1f f8 ea 0f 1f f8 eb 0f 1f f8 ec 0f 1f f8 "
      "ed 0f 1f f8 ee 0f 1f f8 ef 0f 1f f8 f0 0f 1f f8 f1 0f 1f f8 f2 0f 1f f8 f3 0f 1f f8 f4 0f "
      "1f f8 f5 0f 1f f8 f6 0f 1f f8 f7 0f 1f f8 f8 0f 1f f8 f9 0f 1f f8 fa 0f 1f f8 fb 0f 1f f8 "
      "fc 0f 1f f8 fd 0f 1f f8 fe 0f 1f f8 fe 07 41 f8 fd 07 41 f8 fc 07 41 f8 fb 07 41 f8 fa 07 "
      "41 f8 f9 07 41 f8 f8 07 41 f8 f7 07 41 f8 f6 07 41 f8 f5 07 41 f8 f4 07 41 f8 f3 07 41 f8 "
      "f2 07 41 f8 f1 07 41 f8 f0 07 41 f8 ef 07 41 f8 ee 07 41 f8 ed 07 41 f8 ec 07 41 f8 eb 07 "
      "41 f8 ea 07 41 f8 e9 07 41 f8 e8 07 41 f8 e7 07 41 f8 e6 07 41 f8 e5 07 41 f8 e4 07 41 f8 "
      "e3 07 41 f8 e2 07 41 f8 e1 07 41 f8 e0 07 41 f8 e0 03 c0 3d ff 43 00 91 e1 03 c0 3d ff 43 "
      "00 91 e2 03 c0 3d ff 43 00 91 e3 03 c0 3d ff 43 00 91 e4 03 c0 3d ff 43 00 91 e5 03 c0 3d "
      "ff 43 00 91 e6 03 c0 3d ff 43 00 91 e7 03 c0 3d ff 43 00 91 e8 03 c0 3d ff 43 00 91 e9 03 "
      "c0 3d ff 43 00 91 ea 03 c0 3d ff 43 00 91 eb 03 c0 3d ff 43 00 91 ec 03 c0 3d ff 43 00 91 "
      "ed 03 c0 3d ff 43 00 91 ee 03 c0 3d ff 43 00 91 ef 03 c0 3d ff 43 00 91 c0 03 5f d6");
  execute_tester(tester);
}

TEST(CodeTester, execute_return_x86) {
  CodeTester tester;
  tester.init_code_buffer(256);
  // test creating a function which simply returns
  tester.emit_return();
  EXPECT_EQ(tester.dump_to_hex_string(), "c3");
  // and execute it!
  execute_tester(tester);
}

TEST(CodeTester, execute_return_arm64) {
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(256);
  // test creating a function which simply returns
  tester.emit(IGen::add_gpr64_imm8s(tester.generator(), ARM64_REG::X0, 1));
  tester.emit(IGen::ret(tester.generator()));
  EXPECT_EQ(tester.dump_to_hex_string(), "00 04 00 91 c0 03 5f d6");
  // and execute it!
  execute_tester(tester);
}

TEST(CodeTester, execute_push_pop_gprs_x86) {
  CodeTester tester;
  tester.init_code_buffer(256);
  // test we can push/pop gprs without crashing.
  tester.emit_push_all_gprs();
  tester.emit_pop_all_gprs();
  tester.emit_return();
  EXPECT_EQ(tester.dump_to_hex_string(),
            "50 51 52 53 54 55 56 57 41 50 41 51 41 52 41 53 41 54 41 55 41 56 41 57 41 5f 41 5e "
            "41 5d 41 5c 41 5b 41 5a 41 59 41 58 5f 5e 5d 5c 5b 5a 59 58 c3");
  execute_tester(tester);
}

TEST(CodeTester, execute_push_pop_gprs_arm64) {
  CodeTester tester(emitter::InstructionSet::ARM64);
  tester.init_code_buffer(256);
  // test we can push/pop gprs without crashing.
  tester.emit_push_all_gprs();
  tester.emit_pop_all_gprs();
  tester.emit_return();
  EXPECT_EQ(tester.dump_to_hex_string(),
            "e0 0f 1f f8 e1 0f 1f f8 e2 0f 1f f8 e3 0f 1f f8 e4 0f 1f f8 e5 0f 1f f8 e6 0f 1f f8 "
            "e7 0f 1f f8 e8 0f 1f f8 e9 0f 1f f8 ea 0f 1f f8 eb 0f 1f f8 ec 0f 1f f8 ed 0f 1f f8 "
            "ee 0f 1f f8 ef 0f 1f f8 f0 0f 1f f8 f1 0f 1f f8 f2 0f 1f f8 f3 0f 1f f8 f4 0f 1f f8 "
            "f5 0f 1f f8 f6 0f 1f f8 f7 0f 1f f8 f8 0f 1f f8 f9 0f 1f f8 fa 0f 1f f8 fb 0f 1f f8 "
            "fc 0f 1f f8 fd 0f 1f f8 fe 0f 1f f8 fe 07 41 f8 fd 07 41 f8 fc 07 41 f8 fb 07 41 f8 "
            "fa 07 41 f8 f9 07 41 f8 f8 07 41 f8 f7 07 41 f8 f6 07 41 f8 f5 07 41 f8 f4 07 41 f8 "
            "f3 07 41 f8 f2 07 41 f8 f1 07 41 f8 f0 07 41 f8 ef 07 41 f8 ee 07 41 f8 ed 07 41 f8 "
            "ec 07 41 f8 eb 07 41 f8 ea 07 41 f8 e9 07 41 f8 e8 07 41 f8 e7 07 41 f8 e6 07 41 f8 "
            "e5 07 41 f8 e4 07 41 f8 e3 07 41 f8 e2 07 41 f8 e1 07 41 f8 e0 07 41 f8 c0 03 5f d6");
  execute_tester(tester);
}