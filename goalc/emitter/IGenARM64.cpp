
#include "IGenARM64.h"

#include <tuple>

#include "goalc/emitter/Instruction.h"
#include "goalc/emitter/InstructionSet.h"
#include "goalc/emitter/Register.h"

// https://armconverter.com/?code=ret
// https://developer.arm.com/documentation/ddi0487/latest

// TODO ARM64 - just silencing errors while things are not implemented obviously
#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace emitter {
namespace IGen {
namespace ARM64 {

const auto instr_set = emitter::InstructionSet::ARM64;
using namespace emitter::ARM64;

// Utility functions (not public facing instructions)
// used to encode instructions and match the same API

// Checks whether or not an immediate can be represented in 12 unsigned bits, either:
// - plain [0-4095] immediate
// - imm << 12 (some multiple of 4096)
std::tuple<bool, u16, bool> can_encode_single_imm12(u64 imm) {
  if (imm < 4096) {
    return {true, static_cast<u16>(imm), false};
  }
  if ((imm & 0xFFF) == 0) {  // divisible by 4096
    u64 upper = imm >> 12;
    if (upper < 4096) {
      return {true, static_cast<uint16_t>(upper), true};
    }
  }
  return {false, 0, false};
}

// Unconditional branch to a compile-time-*known* byte offset from this instruction's own address -
// NOT the deferred/linker-patched jmp_imm() (see IR_GotoLabel::do_codegen_arm64, IR.cpp) used for a
// real GOAL `goto`. This exists purely so a function that embeds a literal data word inline in its
// own instruction stream (see load_cross_seg_addr()/load_symbol_ptr_offset()/load_symbol_value()/
// store_symbol_value() below) can jump over it: on real hardware, every 4-byte-aligned word in the
// instruction stream is fetched and executed as an instruction unless something branches around it
// - a literal placeholder sitting in the fall-through path would be decoded and run as whatever
// garbage instruction its bits happen to form the moment real ARM64 code actually executes this
// path, not just when goalc encodes it (goalc's own compile-only pipeline can never catch this,
// since it never executes the ARM64 it emits).
// Forward declaration - defined near vpshuflw()/vpshufhw() below, used by swizzle_vf() above them.
InstructionARM64 tbl_16b(Register dst, Register src, Register index);

InstructionARM64 branch_over(s32 byte_offset) {
  ASSERT(byte_offset % 4 == 0);
  return InstructionARM64(Base(0b000101, 6), Imm26(static_cast<u32>(byte_offset / 4) & 0x3ffffff));
}

// Given a larger than u12 immediate, decompose it into multiple (shifted or not)
// immediates that can be used to emit multiple instructions to produce the desired outcome
std::vector<std::tuple<u16, bool>> decompose_into_imm12_chunks(u64 imm) {
  std ::vector<std::tuple<u16, bool>> result;
  u64 upper = imm >> 12;
  while (upper > 0) {
    u16 chunk = (upper > 4095) ? 4095 : static_cast<u16>(upper);
    result.emplace_back(chunk, true);
    upper -= chunk;
  }

  u16 lower = imm & 0xFFF;
  if (lower > 0) {
    result.emplace_back(lower, false);
  }
  return result;
}

std::vector<InstructionARM64> construct_multiple_imm12_adds(int64_t imm, u32 register_id) {
  const auto chunks = decompose_into_imm12_chunks(imm);
  std::vector<InstructionARM64> instrs;
  for (const auto& [_imm12, _needs_shift] : chunks) {
    // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
    // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
    instrs.emplace_back(InstructionARM64(Base(0b100100010, 9), Sh(_needs_shift ? 1 : 0),
                                         Imm12(_imm12), Rd(register_id), Rn(register_id)));
  }
  return instrs;
}

std::vector<InstructionARM64> construct_multiple_imm12_subs(int64_t imm, u32 register_id) {
  const auto chunks = decompose_into_imm12_chunks(imm);
  std::vector<InstructionARM64> instrs;
  for (const auto& [_imm12, _needs_shift] : chunks) {
    // https://www.scs.stanford.edu/~zyedidia/arm64/sub_addsub_imm.html
    // SUB <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
    instrs.emplace_back(InstructionARM64(Base(0b110100010, 9), Sh(_needs_shift ? 1 : 0),
                                         Imm12(_imm12), Rd(register_id), Rn(register_id)));
  }
  return instrs;
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   MOVES
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

InstructionARM64 mov_gpr64_gpr64(Register dst, Register src) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(src.is_gpr(instr_set));
  // BUG (found this session): register 31 means two completely different things depending on
  // which instruction *form* it appears in - XZR (the zero register) in this MOV-via-ORR encoding
  // (Rd/Rn/Rm=31 here are hardwired to mean XZR, no exceptions), but SP in the ADD/SUB-immediate
  // and ADD/SUB-extended-register forms below. `emitter::SP`'s raw id is 31 (same numeric value as
  // XZR - there's no separate bit anywhere that distinguishes them, only the instruction encoding
  // chosen does). Since this function picked the ORR-alias MOV unconditionally, `(set! sp <src>)`/
  // `(set! <dst> sp)` (used by every hand-written asm-func "reset the real stack pointer" routine,
  // e.g. `reset-and-call` in gkernel.gc) silently compiled to a MOV into/out of XZR - a complete
  // no-op that discarded the intended stack-pointer update entirely while producing a
  // perfectly valid, silently-wrong instruction. Confirmed via a real `mov sp, x0`/`mov x0, sp`
  // assembled by the NDK's own clang (`0x9100001f`/`0x910003e0` - both the "ADD (immediate), #0"
  // pseudo-op, not ORR) vs this function's pre-fix output for the same inputs (`0xaa0003ff`/
  // `0xaa1f03e0` - real ORR-with-XZR instructions, entirely different opcodes). See
  // the development notes.
  if (dst.id() == emitter::SP || src.id() == emitter::SP) {
    // MOV <Xd|SP>, <Xn|SP> == ADD <Xd|SP>, <Xn|SP>, #0
    // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
    return InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(dst.id()), Rn(src.id()));
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/mov_orr_log_shift.html
  // MOV <Xd>, <Xm>
  return InstructionARM64(Base(0b10101010000, 11), Rm(src.id()), Rn(0b11111), Rd(dst.id()),
                          Imm6(0));
}

InstructionARM64 mov_gpr64_u64(Register dst, uint64_t val) {
  // Cannot be done in a single instruction, must combine multiple MOVZ/MOVKs
  std::vector<InstructionARM64> instrs;
  bool emitted_movz = false;
  for (int i = 0; i < 4; i++) {
    u16 chunk = (val >> (i * 16)) & 0xFFFF;
    if (!emitted_movz && chunk != 0) {
      // https://www.scs.stanford.edu/~zyedidia/arm64/movz.html
      // MOVZ <Xd>, #<imm>{, LSL #<shift>/16}
      instrs.emplace_back(
          InstructionARM64(Base(0b110100101, 9), Hw(i), Imm16(chunk), Rd(dst.id())));
      emitted_movz = true;
    } else if (emitted_movz && chunk != 0) {
      // https://www.scs.stanford.edu/~zyedidia/arm64/movk.html
      // MOVK <Xd>, #<imm>{, LSL #<shift>/16}
      instrs.emplace_back(
          InstructionARM64(Base(0b111100101, 9), Hw(i), Imm16(chunk), Rd(dst.id())));
    }
  }
  if (!emitted_movz) {
    // https://www.scs.stanford.edu/~zyedidia/arm64/movz.html
    // MOVZ <Xd>, #<imm>{, LSL #0}
    instrs.emplace_back(InstructionARM64(Base(0b110100101, 9), Hw(0), Imm16(0), Rd(dst.id())));
  }
  return InstructionARM64(instrs);
}

InstructionARM64 mov_gpr64_u32(Register dst, uint64_t val) {
  return mov_gpr64_u64(dst, val);
}

InstructionARM64 mov_gpr64_s32(Register dst, int64_t val) {
  // preserve sign -- but we are are simply moving the bits
  u64 raw_val = static_cast<u64>(val);  // via int64_t → sign already there
  return mov_gpr64_u64(dst, raw_val);
}

InstructionARM64 movd_gpr32_f32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fmov_float_gen.html
  // Single-precision to 32-bit (sf == 0 && ftype == 00 && rmode == 00 && opcode == 110)
  // FMOV <Wd>, <Sn>
  ASSERT(dst.is_gpr(instr_set));
  return InstructionARM64(Base(0b0001111000100110000000, 22), Rn(src.id()), Rd(dst.id()));
}

InstructionARM64 movd_f32_gpr32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fmov_float_gen.html
  // 32-bit to single-precision (sf == 0 && ftype == 00 && rmode == 00 && opcode == 111)
  // FMOV <Sd>, <Wn>
  ASSERT(src.is_gpr(instr_set));
  return InstructionARM64(Base(0b0001111000100111000000, 22), Rn(src.id()), Rd(dst.id()));
}

InstructionARM64 movq_gpr64_f64(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fmov_float_gen.html
  // Double-precision to 64-bit (sf == 1 && ftype == 01 && rmode == 00 && opcode == 110)
  // FMOV <Xd>, <Dn>
  ASSERT(dst.is_gpr(instr_set));
  return InstructionARM64(Base(0b1001111001100110000000, 22), Rn(src.id()), Rd(dst.id()));
}

InstructionARM64 movq_f64_gpr64(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fmov_float_gen.html
  // 64-bit to double-precision (sf == 1 && ftype == 01 && rmode == 00 && opcode == 111)
  // FMOV <Xd>, <Dn>
  ASSERT(src.is_gpr(instr_set));
  return InstructionARM64(Base(0b1001111001100111000000, 22), Rn(src.id()), Rd(dst.id()));
}

InstructionARM64 mov_f32_f32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fmov_float.html
  // Single-precision (ftype == 00)
  // FMOV <Sd>, <Sn>
  return InstructionARM64(Base(0b0001111000100000010000, 22), Rn(src.id()), Rd(dst.id()));
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   GOAL Loads and Stores
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

InstructionARM64 load8s_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldrsb_reg.html
  // 64-bit with extended register offset (opc == 10 && option != 011)
  // LDRSB <Xt>, [<Xn|SP>, (<Wm>|<Xm>), <extend> {<amount>}]
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  return InstructionARM64(Base(0b0011100010100000111010, 22), Rt(dst.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 store8_gpr64_gpr64_plus_gpr64(Register addr1, Register addr2, Register value) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/strb_reg.html
  // 64 bit - SXTX
  // strb Wt, [Xn, Xm]
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  return InstructionARM64(Base(0b0011100000100000111010, 22), Rt(value.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

// TODO ARM64 - x16 needs to be reserved, started leveraging it here
// yes it would be possible to only reserve it _sometimes_, but keep things simple
// we have SO many more registers already over x86, 1 less isn't going to be that big of a deal

InstructionARM64 load8s_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                       Register addr1,
                                                       Register addr2,
                                                       s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
       // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
       InstructionARM64(Base(0b1000101100100000111000, 22), Rd(X16), Rn(addr1.id()),
                        Rm(addr2.id())),
       // https://www.scs.stanford.edu/~zyedidia/arm64/ldursb.html
       // LDURSB <Xt>, [<Xn|SP>{, #<simm>}]
       InstructionARM64(Base(0b0011100010000000000000, 22), Imm9s(offset), Rt(dst.id()), Rn(X16))});
}

InstructionARM64 store8_gpr64_gpr64_plus_gpr64_plus_s8(Register addr1,
                                                       Register addr2,
                                                       Register value,
                                                       s64 offset) {
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  return InstructionARM64({// https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
                           // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
                           InstructionARM64(Base(0b1000101100100000111000, 22), Rd(X16),
                                            Rn(addr1.id()), Rm(addr2.id())),
                           // https://www.scs.stanford.edu/~zyedidia/arm64/sturb.html
                           // STURB <Wt>, [<Xn|SP>{, #<simm>}]
                           InstructionARM64(Base(0b0011100000000000000000, 22), Imm9s(offset),
                                            Rt(value.id()), Rn(X16))});
}

InstructionARM64 load8s_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // finally do the load
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldrsb_imm.html
  // LDRSB <Xt>, [<Xn|SP>], #<simm>
  instrs.emplace_back(InstructionARM64(Base(0b0011100110, 10), Imm12(0), Rt(dst.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 store8_gpr64_gpr64_plus_gpr64_plus_s32(Register addr1,
                                                        Register addr2,
                                                        Register value,
                                                        s64 offset) {
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/strb_imm.html
  // unsigned offset
  // STRB <Wt>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(InstructionARM64(Base(0b0011100100, 10), Imm12(0), Rt(value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load8u_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldrb_reg.html
  // SXTX extend option
  // LDRB <Wt>, [<Xn|SP>, <Xm>{, LSL <amount>}]
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  return InstructionARM64(Base(0b0011100001100000111010, 22), Rt(dst.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 load8u_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                       Register addr1,
                                                       Register addr2,
                                                       s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  std::vector<InstructionARM64> instrs;
  if (offset > 0) {
    instrs = {// https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
              // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
              InstructionARM64(Base(0b1000101100100000111000, 22), Rd(X16), Rn(addr1.id()),
                               Rm(addr2.id())),
              // https://www.scs.stanford.edu/~zyedidia/arm64/ldrb_imm.html
              // Unsigned offset mode
              // LDRB <Xt>, [<Xn|SP>], #<simm>
              InstructionARM64(Base(0b0011100101, 10), Imm12(offset), Rt(dst.id()), Rn(X16))};
  } else {
    instrs = {
        // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
        // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
        InstructionARM64(Base(0b1000101100100000111000, 22), Rd(X16), Rn(addr1.id()),
                         Rm(addr2.id())),
        // https://www.scs.stanford.edu/~zyedidia/arm64/ldurb.html
        // LDURB <Xt>, [<Xn|SP>{, #<simm>}]
        InstructionARM64(Base(0b0011100001000000000000, 22), Imm9s(offset), Rt(dst.id()), Rn(X16))};
  }
  return InstructionARM64(instrs);
}

InstructionARM64 load8u_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // finally do the load - X16 already holds the exact final address (base + the full `offset`,
  // added/subtracted above), so the load itself needs a displacement of 0, not `offset` again.
  // BUG (found this session, same class as `load16u_gpr64_gpr64_plus_gpr64_plus_s32`'s bug above,
  // and the only other instance of it in this file - every sibling `_plus_s32` load/store variant
  // already correctly uses `Imm12(0)` here): this used to pass `Imm12(offset)`, silently adding
  // `offset` a second time on top of X16 already containing it.
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldrb_imm.html
  // LDRB <Xt>, [<Xn|SP>], #<simm>
  instrs.emplace_back(InstructionARM64(Base(0b0011100101, 10), Imm12(0), Rt(dst.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load16s_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldrsh_reg.html
  // LDRSH <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  return InstructionARM64(Base(0b0111100010100000111010, 22), Rt(dst.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 store16_gpr64_gpr64_plus_gpr64(Register addr1, Register addr2, Register value) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/strh_reg.html
  // STRH <Wt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  // BUG (found this session, same class as the SIMD register-offset bugs above): `option` was
  // hardcoded to UXTB (0b000) instead of UXTX (0b011) - an unallocated/illegal instruction
  // whenever this path actually ran. Found by systematically auditing every `Base(..., 22)` used
  // alongside an `Rm(...)` call in this file after the SIMD instances turned out wrong.
  return InstructionARM64(Base(0b0111100000100000011010, 22), Rt(value.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 store16_gpr64_gpr64_plus_gpr64_plus_s8(Register addr1,
                                                        Register addr2,
                                                        Register value,
                                                        s64 offset) {
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  return InstructionARM64({// https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
                           // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
                           InstructionARM64(Base(0b1000101100100000111000, 22), Rd(X16),
                                            Rn(addr1.id()), Rm(addr2.id())),
                           // https://www.scs.stanford.edu/~zyedidia/arm64/sturh.html
                           // STURH <Wt>, [<Xn|SP>{, #<simm>}]
                           InstructionARM64(Base(0b0111100000000000000000, 22), Imm9s(offset),
                                            Rt(value.id()), Rn(X16))});
}

InstructionARM64 store16_gpr64_gpr64_plus_gpr64_plus_s32(Register addr1,
                                                         Register addr2,
                                                         Register value,
                                                         s64 offset) {
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // finally do the load
  // https://www.scs.stanford.edu/~zyedidia/arm64/strh_imm.html
  // STRH <Wt>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(InstructionARM64(Base(0b0111100100, 10), Imm12(0), Rt(value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load16s_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
       // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
       InstructionARM64(Base(0b1000101100100000111000, 22), Rd(X16), Rn(addr1.id()),
                        Rm(addr2.id())),
       // https://www.scs.stanford.edu/~zyedidia/arm64/ldursh.html
       // LDURSH <Xt>, [<Xn|SP>{, #<simm>}]
       InstructionARM64(Base(0b0111100010000000000000, 22), Imm9s(offset), Rt(dst.id()), Rn(X16))});
}

InstructionARM64 load16s_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                         Register addr1,
                                                         Register addr2,
                                                         s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // finally do the load
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldrsh_imm.html
  // LDRSH <Xt>, [<Xn|SP>], #<simm>
  instrs.emplace_back(InstructionARM64(Base(0b0111100110, 10), Imm12(0), Rt(dst.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load16u_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldrh_reg.html
  // SXTX extend option
  // LDRH <Wt>, [<Xn|SP>, <Xm>{, LSL <amount>}]
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  return InstructionARM64(Base(0b0111100001100000111010, 22), Rt(dst.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 load16u_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  std::vector<InstructionARM64> instrs;
  if (offset > 0) {
    // BUG (found this session): this scaled-immediate LDRH encodes a byte offset divided by 2 in
    // its imm12 field (the ARM reference calls this `pimm`, a "scaled" immediate - the actual
    // byte offset is `imm12 * 2` for halfword accesses) - passing the raw byte `offset` straight
    // into `Imm12()` silently doubled every such field access (e.g. offset 0x8 was encoded as
    // imm12=8, decoding back to a real byte offset of 0x10). Confirmed on real hardware: this
    // exact bug turned `(-> process size)` (a `uint16` field at byte offset 8) into a read of
    // method-table[0] (offset 0x10) instead, corrupting the size argument to the allocator call
    // in `(defmethod new process ...)` (`goal_src/jak1/kernel/gkernel.gc`) into a garbage value
    // large enough to make the allocation fail (returning a null `this`) - this session's fourth
    // real ARM64 codegen bug. `offset` must be even here (2-byte aligned, as any real
    // `uint16`-or-wider field naturally is) - an odd byte offset can't be expressed by this
    // scaled form at all and must use the unscaled `LDURH` path below instead.
    ASSERT(offset % 2 == 0);
    instrs = {// https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
              // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
              InstructionARM64(Base(0b1000101100100000111000, 22), Rd(X16), Rn(addr1.id()),
                               Rm(addr2.id())),
              // https://www.scs.stanford.edu/~zyedidia/arm64/ldrh_imm.html
              // Unsigned offset mode
              // LDRH <Wt>, [<Xn|SP>{, #<pimm>}]
              InstructionARM64(Base(0b0111100101, 10), Imm12(offset / 2), Rt(dst.id()), Rn(X16))};
  } else {
    instrs = {
        // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
        // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
        InstructionARM64(Base(0b1000101100100000111000, 22), Rd(X16), Rn(addr1.id()),
                         Rm(addr2.id())),
        // https://www.scs.stanford.edu/~zyedidia/arm64/ldurh.html
        // LDURH <Wt>, [<Xn|SP>{, #<simm>}]
        InstructionARM64(Base(0b0111100001000000000000, 22), Imm9s(offset), Rt(dst.id()), Rn(X16))};
  }
  return InstructionARM64(instrs);
}

InstructionARM64 load16u_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                         Register addr1,
                                                         Register addr2,
                                                         s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // finally do the load - X16 already holds the exact final address (base + the full `offset`,
  // added/subtracted above via construct_multiple_imm12_adds/subs), so the load itself needs a
  // displacement of 0, not `offset` again. BUG (found this session, same investigation as the
  // sibling `_plus_s8` overload above): this used to pass `Imm12(offset)` here, which - on top of
  // already being unscaled for a halfword access (see the `_plus_s8` overload's comment) - also
  // silently added `offset` a *second* time on top of X16 already containing it. The signed
  // sibling below (`load16s_gpr64_gpr64_plus_gpr64_plus_s32`) already does this correctly with
  // `Imm12(0)` - this one just never matched it.
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldrh_imm.html
  // LDRH <Wt>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(InstructionARM64(Base(0b0111100101, 10), Imm12(0), Rt(dst.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load32s_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldrsw_reg.html
  // LDRSW <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  return InstructionARM64(Base(0b1011100010100000111010, 22), Rt(dst.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 store32_gpr64_gpr64_plus_gpr64(Register addr1, Register addr2, Register value) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_reg_gen.html
  // STR <Wt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  return InstructionARM64(Base(0b1011100000100000111010, 22), Rt(value.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 load32s_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
       // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
       InstructionARM64(Base(0b1000101100100000111000, 22), Rd(X16), Rn(addr1.id()),
                        Rm(addr2.id())),
       // https://www.scs.stanford.edu/~zyedidia/arm64/ldursw.html
       // LDURSW <Xt>, [<Xn|SP>{, #<simm>}]
       InstructionARM64(Base(0b1011100010000000000000, 22), Imm9s(offset), Rt(dst.id()), Rn(X16))});
}

InstructionARM64 store32_gpr64_gpr64_plus_gpr64_plus_s8(Register addr1,
                                                        Register addr2,
                                                        Register value,
                                                        s64 offset) {
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_gen.html
  // STR <Wt>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(InstructionARM64(Base(0b1011100100, 10), Imm12(0), Rt(value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load32s_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                         Register addr1,
                                                         Register addr2,
                                                         s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // finally do the load
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldrsw_imm.html
  // LDRSW <Xt>, [<Xn|SP>], #<simm>
  instrs.emplace_back(InstructionARM64(Base(0b1011100110, 10), Imm12(0), Rt(dst.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 store32_gpr64_gpr64_plus_gpr64_plus_s32(Register addr1,
                                                         Register addr2,
                                                         Register value,
                                                         s64 offset) {
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_gen.html
  // STR <Wt>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(InstructionARM64(Base(0b1011100100, 10), Imm12(0), Rt(value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load32u_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_reg_gen.html
  // 32-bit variant
  // LDR <Wt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  // BUG (found this session, same class as the SIMD register-offset bugs above): `option` was
  // hardcoded to UXTB (0b000) instead of UXTX (0b011) - an unallocated/illegal instruction
  // whenever this path actually ran.
  return InstructionARM64(Base(0b1011100001100000011010, 22), Rt(dst.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 load32u_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
       // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
       InstructionARM64(Base(0b1000101100100000111000, 22), Rd(X16), Rn(addr1.id()),
                        Rm(addr2.id())),
       // https://www.scs.stanford.edu/~zyedidia/arm64/ldur_gen.html
       // 32 bit
       // LDUR <Wt>, [<Xn|SP>{, #<simm>}]
       InstructionARM64(Base(0b1011100001000000000000, 22), Imm9s(offset), Rt(dst.id()), Rn(X16))});
}

InstructionARM64 load32u_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                         Register addr1,
                                                         Register addr2,
                                                         s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // finally do the load
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_gen.html
  // 32-bit variant
  // LDR <Wt>, [<Xn|SP>], #<simm>
  instrs.emplace_back(
      InstructionARM64(Base(0b1011100001000000000001, 22), Imm9s(0), Rt(dst.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load64_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_reg_gen.html
  // 64 bit mode
  // LDR <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  // BUG (found this session, same class as the SIMD register-offset bugs above): `option` was
  // hardcoded to UXTB (0b000) instead of UXTX (0b011) - an unallocated/illegal instruction
  // whenever this path actually ran. This is a plain 64-bit register-offset load, used very
  // widely - almost certainly hit constantly throughout boot already; the fact boot got this far
  // at all suggests most real GOAL field accesses go through the immediate-offset variants
  // instead, only reaching this exact register+register form in less common cases (as with
  // `matrix-identity!`'s zero-offset SIMD store above).
  return InstructionARM64(Base(0b1111100001100000011010, 22), Rt(dst.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 store64_gpr64_gpr64_plus_gpr64(Register addr1, Register addr2, Register value) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_reg_gen.html
  // STR <Xt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  return InstructionARM64(Base(0b1111100000100000111010, 22), Rt(value.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 load64_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                       Register addr1,
                                                       Register addr2,
                                                       s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
       // ADD <Xd|SP>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}
       InstructionARM64(Base(0b1000101100100000111000, 22), Rd(X16), Rn(addr1.id()),
                        Rm(addr2.id())),
       // https://www.scs.stanford.edu/~zyedidia/arm64/ldur_gen.html
       // 64 bit
       // LDUR <Xt>, [<Xn|SP>{, #<simm>}]
       InstructionARM64(Base(0b1111100001000000000000, 22), Imm9s(offset), Rt(dst.id()), Rn(X16))});
}

InstructionARM64 store64_gpr64_gpr64_plus_gpr64_plus_s8(Register addr1,
                                                        Register addr2,
                                                        Register value,
                                                        s64 offset) {
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_gen.html
  // STR <Xt>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(InstructionARM64(Base(0b1111100100, 10), Imm12(0), Rt(value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load64_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // finally do the load
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_gen.html
  // 64-bit variant
  // LDR <Xt>, [<Xn|SP>], #<simm>
  instrs.emplace_back(
      InstructionARM64(Base(0b1111100001000000000001, 22), Imm9s(0), Rt(dst.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 store64_gpr64_gpr64_plus_gpr64_plus_s32(Register addr1,
                                                         Register addr2,
                                                         Register value,
                                                         s64 offset) {
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_gen.html
  // STR <Xt>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(InstructionARM64(Base(0b1111100100, 10), Imm12(0), Rt(value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load64_gpr64_plus_s32(Register dst_reg, int32_t offset, Register src_reg) {
  ASSERT(dst_reg.is_gpr(instr_set));
  ASSERT(src_reg.is_gpr(instr_set));
  // SP is fine here: the address is computed with the same ADD <Xd|SP>, <Xn|SP>, #imm encoding
  // that lea_reg_plus_off32/off8 already use with no such restriction (that encoding's Rn field
  // explicitly permits SP, per the ISA reference both cite) - needed for function-local spill
  // slots addressed relative to SP, see CodeGenerator::do_goal_function_arm64.
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(X16), Rn(src_reg.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // finally do the load
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_gen.html
  // 64-bit variant
  // LDR <Xt>, [<Xn|SP>], #<simm>
  instrs.emplace_back(
      InstructionARM64(Base(0b1111100001000000000001, 22), Imm9s(0), Rt(dst_reg.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 store64_gpr64_plus_s32(Register addr, int32_t offset, Register value) {
  ASSERT(value.is_gpr(instr_set));
  ASSERT(addr.is_gpr(instr_set));
  // SP is fine here - see the matching comment in load64_gpr64_plus_s32 above.
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(X16), Rn(addr.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_gen.html
  // STR <Xt>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(InstructionARM64(Base(0b1111100100, 10), Imm12(0), Rt(value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 store_goal_vf(Register addr, Register value, Register off, s64 offset) {
  if (offset == 0) {
    return storevf_gpr64_plus_gpr64(value, addr, off);
  } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
    return storevf_gpr64_plus_gpr64_plus_s8(value, addr, off, offset);
  } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
    return storevf_gpr64_plus_gpr64_plus_s32(value, addr, off, offset);
  }
  ASSERT(false);
  return {0};
}

InstructionARM64 store_goal_gpr(Register addr, Register value, Register off, int offset, int size) {
  switch (size) {
    case 1:
      if (offset == 0) {
        return store8_gpr64_gpr64_plus_gpr64(addr, off, value);
      } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
        return store8_gpr64_gpr64_plus_gpr64_plus_s8(addr, off, value, offset);
      } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
        return store8_gpr64_gpr64_plus_gpr64_plus_s32(addr, off, value, offset);
      } else {
        ASSERT(false);
      }
    case 2:
      if (offset == 0) {
        return store16_gpr64_gpr64_plus_gpr64(addr, off, value);
      } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
        return store16_gpr64_gpr64_plus_gpr64_plus_s8(addr, off, value, offset);
      } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
        return store16_gpr64_gpr64_plus_gpr64_plus_s32(addr, off, value, offset);
      } else {
        ASSERT(false);
      }
    case 4:
      if (offset == 0) {
        return store32_gpr64_gpr64_plus_gpr64(addr, off, value);
      } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
        return store32_gpr64_gpr64_plus_gpr64_plus_s8(addr, off, value, offset);
      } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
        return store32_gpr64_gpr64_plus_gpr64_plus_s32(addr, off, value, offset);
      } else {
        ASSERT(false);
      }
    case 8:
      if (offset == 0) {
        return store64_gpr64_gpr64_plus_gpr64(addr, off, value);
      } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
        return store64_gpr64_gpr64_plus_gpr64_plus_s8(addr, off, value, offset);
      } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
        return store64_gpr64_gpr64_plus_gpr64_plus_s32(addr, off, value, offset);
      } else {
        ASSERT(false);
      }
    default:
      ASSERT(false);
      return {0};
  }
}

InstructionARM64 load_goal_xmm128(Register dst, Register addr, Register off, int offset) {
  if (offset == 0) {
    return loadvf_gpr64_plus_gpr64(dst, addr, off);
  } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
    return loadvf_gpr64_plus_gpr64_plus_s8(dst, addr, off, offset);
  } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
    return loadvf_gpr64_plus_gpr64_plus_s32(dst, addr, off, offset);
  } else {
    ASSERT(false);
    return {0};
  }
}

InstructionARM64 load_goal_gpr(Register dst,
                               Register addr,
                               Register off,
                               int offset,
                               int size,
                               bool sign_extend) {
  switch (size) {
    case 1:
      if (offset == 0) {
        if (sign_extend) {
          return load8s_gpr64_gpr64_plus_gpr64(dst, addr, off);
        } else {
          return load8u_gpr64_gpr64_plus_gpr64(dst, addr, off);
        }
      } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
        if (sign_extend) {
          return load8s_gpr64_gpr64_plus_gpr64_plus_s8(dst, addr, off, offset);
        } else {
          return load8u_gpr64_gpr64_plus_gpr64_plus_s8(dst, addr, off, offset);
        }
      } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
        if (sign_extend) {
          return load8s_gpr64_gpr64_plus_gpr64_plus_s32(dst, addr, off, offset);
        } else {
          return load8u_gpr64_gpr64_plus_gpr64_plus_s32(dst, addr, off, offset);
        }
      } else {
        ASSERT(false);
      }
    case 2:
      if (offset == 0) {
        if (sign_extend) {
          return load16s_gpr64_gpr64_plus_gpr64(dst, addr, off);
        } else {
          return load16u_gpr64_gpr64_plus_gpr64(dst, addr, off);
        }
      } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
        if (sign_extend) {
          return load16s_gpr64_gpr64_plus_gpr64_plus_s8(dst, addr, off, offset);
        } else {
          return load16u_gpr64_gpr64_plus_gpr64_plus_s8(dst, addr, off, offset);
        }
      } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
        if (sign_extend) {
          return load16s_gpr64_gpr64_plus_gpr64_plus_s32(dst, addr, off, offset);
        } else {
          return load16u_gpr64_gpr64_plus_gpr64_plus_s32(dst, addr, off, offset);
        }
      } else {
        ASSERT(false);
      }
    case 4:
      if (offset == 0) {
        if (sign_extend) {
          return load32s_gpr64_gpr64_plus_gpr64(dst, addr, off);
        } else {
          return load32u_gpr64_gpr64_plus_gpr64(dst, addr, off);
        }
      } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
        if (sign_extend) {
          return load32s_gpr64_gpr64_plus_gpr64_plus_s8(dst, addr, off, offset);
        } else {
          return load32u_gpr64_gpr64_plus_gpr64_plus_s8(dst, addr, off, offset);
        }
      } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
        if (sign_extend) {
          return load32s_gpr64_gpr64_plus_gpr64_plus_s32(dst, addr, off, offset);
        } else {
          return load32u_gpr64_gpr64_plus_gpr64_plus_s32(dst, addr, off, offset);
        }
      } else {
        ASSERT(false);
      }
    case 8:
      if (offset == 0) {
        return load64_gpr64_gpr64_plus_gpr64(dst, addr, off);

      } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
        return load64_gpr64_gpr64_plus_gpr64_plus_s8(dst, addr, off, offset);

      } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
        return load64_gpr64_gpr64_plus_gpr64_plus_s32(dst, addr, off, offset);

      } else {
        ASSERT(false);
      }
    default:
      ASSERT(false);
      return {0};
  }
}

InstructionARM64 lea_reg_plus_off32(Register dest, Register base, s64 offset) {
  ASSERT(dest.is_gpr(instr_set));
  ASSERT(base.is_gpr(instr_set));
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base value in our destination register
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(dest.id()), Rn(base.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, dest.id());
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, dest.id());
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  return InstructionARM64(instrs);
}

InstructionARM64 lea_reg_plus_off8(Register dest, Register base, s64 offset) {
  ASSERT(dest.is_gpr(instr_set));
  ASSERT(base.is_gpr(instr_set));
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  // first establish the base value in our destination register
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(dest.id()), Rn(base.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, dest.id());
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, dest.id());
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  return InstructionARM64(instrs);
}

InstructionARM64 lea_reg_plus_off(Register dest, Register base, s64 offset) {
  if (offset >= INT8_MIN && offset <= INT8_MAX) {
    return lea_reg_plus_off8(dest, base, offset);
  } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
    return lea_reg_plus_off32(dest, base, offset);
  } else {
    ASSERT(false);
    return {0};
  }
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   LOADS n' STORES - XMM32
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

// TODO - rename these to f32 instead of xmm

InstructionARM64 store32_xmm32_gpr64_plus_gpr64(Register addr1,
                                                Register addr2,
                                                Register xmm_value) {
  ASSERT(xmm_value.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_reg_fpsimd.html
  // 32-bit variant
  // STR <St>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
  // BUG (found this session, same class as storevf_gpr64_plus_gpr64/loadvf_gpr64_plus_gpr64
  // above): `option` was hardcoded to UXTB (0b000) instead of UXTX (0b011), producing an
  // unallocated/illegal instruction whenever this path actually ran.
  return InstructionARM64(Base(0b1011110000100000011010, 22), Rt(xmm_value.id()), Rm(addr1.id()),
                          Rn(addr2.id()));
}

InstructionARM64 load32_xmm32_gpr64_plus_gpr64(Register simd_dest, Register addr1, Register addr2) {
  ASSERT(simd_dest.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_reg_fpsimd.html
  // 32-bit variant
  // LDR <St>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
  return InstructionARM64(Base(0b1011110001100000111010, 22), Rt(simd_dest.id()), Rm(addr1.id()),
                          Rn(addr2.id()));
}

InstructionARM64 store32_xmm32_gpr64_plus_gpr64_plus_s8(Register addr1,
                                                        Register addr2,
                                                        Register xmm_value,
                                                        s64 offset) {
  ASSERT(xmm_value.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_fpsimd.html
  // 32-bit variant, unsigned immediate (X16 already holds the fully-resolved address from the
  // ADD(s) above, so this final store always uses a plain #0 pimm - Imm12(0) below is meant to be
  // that "no additional offset" residual, not a real field offset)
  // STR <St>, [<Xn|SP>{, #<pimm>}]
  //
  // BUG (found this session): the low 12 bits of this Base() constant (the same bits Imm12()
  // writes into) were `000000000001` instead of all zero - baking in a stray imm12=1 (= a scaled
  // +4 byte offset for this 32-bit store) that Imm12(0)'s OR could never clear, on top of
  // whatever real offset the ADD(s) above already resolved. Every store through this function
  // silently landed 4 bytes past its intended address. Confirmed byte-for-byte against a real
  // `str s2, [x16]` assembled by the NDK's own clang (0xbd000202) vs this function's pre-fix
  // output (0xbd000602, matching a real `str s2, [x16, #4]` exactly) - the LOAD sibling right
  // below uses a correctly-zeroed 10-bit Base() and was never affected. Root-caused via a real
  // SIGSEGV: `matrix-rotate-y!`'s last `(set! (-> dst data 15) 1.0)` (`goal_src/jak1/engine/math/
  // matrix.gc`) landed 4 bytes past the stack-allocated 64-byte matrix's end, corrupting the
  // caller's (`make-light-kit`, `goal_src/jak1/engine/gfx/mood/mood-tables.gc`) own spilled `arg0`
  // - which then got dereferenced as a GOAL heap offset a few instructions later and crashed. See
  // the development notes.
  instrs.emplace_back(
      InstructionARM64(Base(0b1011110100000000000000, 22), Imm12(0), Rt(xmm_value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load32_xmm32_gpr64_plus_gpr64_plus_s8(Register simd_dest,
                                                       Register addr1,
                                                       Register addr2,
                                                       s64 offset) {
  ASSERT(simd_dest.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_fpsimd.html
  // 32-bit variant
  // LDR <St>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(
      InstructionARM64(Base(0b1011110101, 10), Imm12(0), Rt(simd_dest.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 store32_xmm32_gpr64_plus_gpr64_plus_s32(Register addr1,
                                                         Register addr2,
                                                         Register xmm_value,
                                                         s64 offset) {
  ASSERT(xmm_value.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_fpsimd.html
  // 32-bit variant, unsigned immediate (X16 already holds the fully-resolved address from the
  // ADD(s) above, so this final store always uses a plain #0 pimm - Imm12(0) below is meant to be
  // that "no additional offset" residual, not a real field offset)
  // STR <St>, [<Xn|SP>{, #<pimm>}]
  //
  // BUG (found this session): the low 12 bits of this Base() constant (the same bits Imm12()
  // writes into) were `000000000001` instead of all zero - baking in a stray imm12=1 (= a scaled
  // +4 byte offset for this 32-bit store) that Imm12(0)'s OR could never clear, on top of
  // whatever real offset the ADD(s) above already resolved. Every store through this function
  // silently landed 4 bytes past its intended address. Confirmed byte-for-byte against a real
  // `str s2, [x16]` assembled by the NDK's own clang (0xbd000202) vs this function's pre-fix
  // output (0xbd000602, matching a real `str s2, [x16, #4]` exactly) - the LOAD sibling right
  // below uses a correctly-zeroed 10-bit Base() and was never affected. Root-caused via a real
  // SIGSEGV: `matrix-rotate-y!`'s last `(set! (-> dst data 15) 1.0)` (`goal_src/jak1/engine/math/
  // matrix.gc`) landed 4 bytes past the stack-allocated 64-byte matrix's end, corrupting the
  // caller's (`make-light-kit`, `goal_src/jak1/engine/gfx/mood/mood-tables.gc`) own spilled `arg0`
  // - which then got dereferenced as a GOAL heap offset a few instructions later and crashed. See
  // the development notes.
  instrs.emplace_back(
      InstructionARM64(Base(0b1011110100000000000000, 22), Imm12(0), Rt(xmm_value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 store32_xmm32_gpr64_plus_s32(Register base, Register xmm_value, s64 offset) {
  ASSERT(xmm_value.is_128bit_simd(instr_set));
  ASSERT(base.is_gpr(instr_set));
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(X16), Rn(base.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_fpsimd.html
  // 32-bit variant, unsigned immediate (X16 already holds the fully-resolved address from the
  // ADD(s) above, so this final store always uses a plain #0 pimm - Imm12(0) below is meant to be
  // that "no additional offset" residual, not a real field offset)
  // STR <St>, [<Xn|SP>{, #<pimm>}]
  //
  // BUG (found this session): the low 12 bits of this Base() constant (the same bits Imm12()
  // writes into) were `000000000001` instead of all zero - baking in a stray imm12=1 (= a scaled
  // +4 byte offset for this 32-bit store) that Imm12(0)'s OR could never clear, on top of
  // whatever real offset the ADD(s) above already resolved. Every store through this function
  // silently landed 4 bytes past its intended address. Confirmed byte-for-byte against a real
  // `str s2, [x16]` assembled by the NDK's own clang (0xbd000202) vs this function's pre-fix
  // output (0xbd000602, matching a real `str s2, [x16, #4]` exactly) - the LOAD sibling right
  // below uses a correctly-zeroed 10-bit Base() and was never affected. Root-caused via a real
  // SIGSEGV: `matrix-rotate-y!`'s last `(set! (-> dst data 15) 1.0)` (`goal_src/jak1/engine/math/
  // matrix.gc`) landed 4 bytes past the stack-allocated 64-byte matrix's end, corrupting the
  // caller's (`make-light-kit`, `goal_src/jak1/engine/gfx/mood/mood-tables.gc`) own spilled `arg0`
  // - which then got dereferenced as a GOAL heap offset a few instructions later and crashed. See
  // the development notes.
  instrs.emplace_back(
      InstructionARM64(Base(0b1011110100000000000000, 22), Imm12(0), Rt(xmm_value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 store32_xmm32_gpr64_plus_s8(Register base, Register xmm_value, s64 offset) {
  ASSERT(xmm_value.is_128bit_simd(instr_set));
  ASSERT(base.is_gpr(instr_set));
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(X16), Rn(base.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_fpsimd.html
  // 32-bit variant, unsigned
  // STR <St>, [<Xn|SP>], #<simm>
  instrs.emplace_back(
      InstructionARM64(Base(0b1011110100000000000000, 22), Imm12(0), Rt(xmm_value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load32_xmm32_gpr64_plus_gpr64_plus_s32(Register simd_dest,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset) {
  ASSERT(simd_dest.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_fpsimd.html
  // 32-bit variant
  // LDR <St>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(
      InstructionARM64(Base(0b1011110101, 10), Imm12(0), Rt(simd_dest.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load32_xmm32_gpr64_plus_s32(Register simd_dest, Register base, s64 offset) {
  ASSERT(simd_dest.is_128bit_simd(instr_set));
  ASSERT(base.is_gpr(instr_set));
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(X16), Rn(base.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_fpsimd.html
  // 32-bit variant
  // LDR <St>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(
      InstructionARM64(Base(0b1011110101, 10), Imm12(0), Rt(simd_dest.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load32_xmm32_gpr64_plus_s8(Register simd_dest, Register base, s64 offset) {
  ASSERT(simd_dest.is_128bit_simd(instr_set));
  ASSERT(base.is_gpr(instr_set));
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(X16), Rn(base.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_fpsimd.html
  // 32-bit variant
  // LDR <St>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(
      InstructionARM64(Base(0b1011110101, 10), Imm12(0), Rt(simd_dest.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load_goal_xmm32(Register simd_dest, Register addr, Register off, s64 offset) {
  if (offset == 0) {
    return load32_xmm32_gpr64_plus_gpr64(simd_dest, addr, off);
  } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
    return load32_xmm32_gpr64_plus_gpr64_plus_s8(simd_dest, addr, off, offset);
  } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
    return load32_xmm32_gpr64_plus_gpr64_plus_s32(simd_dest, addr, off, offset);
  } else {
    ASSERT(false);
    return {0};
  }
}

InstructionARM64 store_goal_xmm32(Register addr, Register xmm_value, Register off, s64 offset) {
  if (offset == 0) {
    return store32_xmm32_gpr64_plus_gpr64(addr, off, xmm_value);
  } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
    return store32_xmm32_gpr64_plus_gpr64_plus_s8(addr, off, xmm_value, offset);
  } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
    return store32_xmm32_gpr64_plus_gpr64_plus_s32(addr, off, xmm_value, offset);
  } else {
    ASSERT(false);
    return {0};
  }
}

InstructionARM64 store_reg_offset_xmm32(Register base, Register xmm_value, s64 offset) {
  if (offset >= INT8_MIN && offset <= INT8_MAX) {
    return store32_xmm32_gpr64_plus_s8(base, xmm_value, offset);
  } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
    return store32_xmm32_gpr64_plus_s32(base, xmm_value, offset);
  } else {
    ASSERT(false);
    return {0};
  }
}

InstructionARM64 load_reg_offset_xmm32(Register simd_dest, Register base, s64 offset) {
  if (offset >= INT8_MIN && offset <= INT8_MAX) {
    return load32_xmm32_gpr64_plus_s8(simd_dest, base, offset);
  } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
    return load32_xmm32_gpr64_plus_s32(simd_dest, base, offset);
  } else {
    ASSERT(false);
    return {0};
  }
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   LOADS n' STORES - SIMD (128-bit, QWORDS)
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

InstructionARM64 store128_gpr64_simd128(Register gpr_addr, Register simd_reg) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_fpsimd.html
  // - STR Qn, [Xn] (unsigned offset)
  ASSERT(gpr_addr.is_gpr(instr_set));
  ASSERT(
      simd_reg.is_128bit_simd(instr_set));  // TODO ARM64 - this assertion isn't as useful for ARM
                                            // since Q registers are not unique in terms of their id
  return InstructionARM64(Base(0b0011110110, 10), Rn(gpr_addr.id()), Rt(simd_reg.id()), Imm12(0));
}

InstructionARM64 store128_gpr64_simd128_s32(Register gpr_addr, Register xmm_value, s64 offset) {
  ASSERT(gpr_addr.is_gpr(instr_set));
  ASSERT(xmm_value.is_128bit_simd(
      instr_set));  // TODO ARM64 - this assertion isn't as useful for ARM
                    // since Q registers are not unique in terms of their id
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(X16), Rn(gpr_addr.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_fpsimd.html
  // 128-bit variant, unsigned offset
  // STR <Qt>, [<Xn|SP>{, #<pimm>}]
  // BUG (found this session): `0b0011110110` is only a 10-bit literal (matching the sibling
  // zero-offset `store128_gpr64_simd128` above, which correctly declares it `Base(..., 10)`) but
  // was declared `Base(..., 22)` here - shifting it 12 bits too far right and landing it at bits
  // [19:10] instead of the real [31:22], producing a word with its whole top byte zeroed (nothing
  // resembling a valid STR at all, confirmed against a real `str q9, [x16]` assembled by the NDK's
  // own `clang`). Not yet confirmed hit on real hardware (the crash chased this session went
  // through the sibling `_s8` overload below, not this `_s32` one), but definitely still wrong -
  // fixed proactively per this session's "keep fixing everything you find" instruction.
  instrs.emplace_back(
      InstructionARM64(Base(0b0011110110, 10), Imm12(0), Rt(xmm_value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 store128_gpr64_simd128_s8(Register gpr_addr, Register xmm_value, s64 offset) {
  ASSERT(gpr_addr.is_gpr(instr_set));
  ASSERT(xmm_value.is_128bit_simd(
      instr_set));  // TODO ARM64 - this assertion isn't as useful for ARM
                    // since Q registers are not unique in terms of their id
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(X16), Rn(gpr_addr.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_fpsimd.html
  // 128-bit variant, unsigned offset
  // STR <Qt>, [<Xn|SP>{, #<pimm>}]
  // BUG (found this session): same width-vs-literal mismatch as `store128_gpr64_simd128_s32`
  // above (`0b0011110110` is a 10-bit literal, declared `Base(..., 22)` instead of the correct
  // `Base(..., 10)`) - confirmed on real hardware via a SIGILL inside `update-math-camera`
  // (`goal_src/jak1/engine/gfx/math-camera.gc`), the first GOAL function needing to back up two
  // or more callee-saved XMM/Q registers at a non-zero prologue stack offset
  // (`CodeGenerator::do_goal_function_arm64`'s callee-saved-register-backup loop, `store128_xmm128_reg_offset`
  // -> here for any offset in `[INT8_MIN, INT8_MAX]`, which is every real case except a
  // pathologically large number of saved registers).
  instrs.emplace_back(
      InstructionARM64(Base(0b0011110110, 10), Imm12(0), Rt(xmm_value.id()), Rn(X16)));
  return InstructionARM64(instrs);
}

InstructionARM64 load128_simd128_gpr64(Register simd_dest, Register gpr_addr) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_fpsimd.html
  // - LDR <Qt>, [<Xn|SP>{, #<pimm>}]
  ASSERT(gpr_addr.is_gpr(instr_set));
  ASSERT(simd_dest.is_128bit_simd(
      instr_set));  // TODO ARM64 - this assertion isn't as useful for ARM
                    // since Q registers are not unique in terms of their id
  return InstructionARM64(Base(0b0011110111, 10), Rn(gpr_addr.id()), Rt(simd_dest.id()), Imm12(0));
}

InstructionARM64 load128_simd128_gpr64_s32(Register simd_dest, Register gpr_addr, s64 offset) {
  ASSERT(gpr_addr.is_gpr(instr_set));
  ASSERT(simd_dest.is_128bit_simd(
      instr_set));  // TODO ARM64 - this assertion isn't as useful for ARM
                    // since Q registers are not unique in terms of their id
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(X16), Rn(gpr_addr.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_fpsimd.html
  // - LDR <Qt>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(
      InstructionARM64(Base(0b0011110111, 10), Rn(X16), Rt(simd_dest.id()), Imm12(0)));
  return InstructionARM64(instrs);
}

InstructionARM64 load128_simd128_gpr64_s8(Register simd_dest, Register gpr_addr, s64 offset) {
  ASSERT(gpr_addr.is_gpr(instr_set));
  ASSERT(simd_dest.is_128bit_simd(
      instr_set));  // TODO ARM64 - this assertion isn't as useful for ARM
                    // since Q registers are not unique in terms of their id
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
      // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
      InstructionARM64(Base(0b100100010, 9), Sh(0), Imm12(0), Rd(X16), Rn(gpr_addr.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_fpsimd.html
  // - LDR <Qt>, [<Xn|SP>{, #<pimm>}]
  instrs.emplace_back(
      InstructionARM64(Base(0b0011110111, 10), Rn(X16), Rt(simd_dest.id()), Imm12(0)));
  return InstructionARM64(instrs);
}

InstructionARM64 load128_xmm128_reg_offset(Register simd_dest, Register base, s64 offset) {
  if (offset == 0) {
    return load128_simd128_gpr64(simd_dest, base);
  } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
    return load128_simd128_gpr64_s8(simd_dest, base, offset);
  } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
    return load128_simd128_gpr64_s32(simd_dest, base, offset);
  } else {
    ASSERT(false);
    return {0};
  }
}

InstructionARM64 store128_xmm128_reg_offset(Register base, Register xmm_val, s64 offset) {
  if (offset == 0) {
    return store128_gpr64_simd128(base, xmm_val);
  } else if (offset >= INT8_MIN && offset <= INT8_MAX) {
    return store128_gpr64_simd128_s8(base, xmm_val, offset);
  } else if (offset >= INT32_MIN && offset <= INT32_MAX) {
    return store128_gpr64_simd128_s32(base, xmm_val, offset);
  } else {
    ASSERT(false);
    return {0};
  }
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   PC relative loads and stores
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

// Implement with LDR but that has a 1MB range limit on ARM (not 2GB like on x86)
// Hopefully this is fine, however it could potentially not be if this is loading static data, which
// may not within 1MB of the current instruction -- that all depends on the linker layout.
//
// But keep it simple at first, add good assertions and we'll see what happens when we
// compile for real.

// TODO ARM64 - the offsets here are always 0 at the time the instruction is made,
// then they are patched later.  That patching also needs an assertion.

// const int ARM64_LDR_MIN = -(1 << 18) * 4;
// const int ARM64_LDR_MAX = ((1 << 18) - 1) * 4;

InstructionARM64 load64_pcRel_s32(Register dest, s64 offset) {
  ASSERT(dest.is_gpr(instr_set));
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  // ASSERT_MSG(offset >= ARM64_LDR_MIN && offset <= ARM64_LDR_MAX,
  //            "PC Relative offset is too large for ARM64, fix it.");
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_lit_gen.html
  // LDR <Xt>, <label>
  return InstructionARM64(Base(0b01011000, 8), Imm19(offset / 4), Rt(dest.id()));
}

InstructionARM64 load32s_pcRel_s32(Register dest, s64 offset) {
  ASSERT(dest.is_gpr(instr_set));
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  // ASSERT_MSG(offset >= ARM64_LDR_MIN && offset <= ARM64_LDR_MAX,
  //            "PC Relative offset is too large for ARM64, fix it.");
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldrsw_lit.html
  // LDRSW <Xt>, <label>
  return InstructionARM64(Base(0b10011000, 8), Imm19(offset / 4), Rt(dest.id()));
}

InstructionARM64 load32u_pcRel_s32(Register dest, s64 offset) {
  ASSERT(dest.is_gpr(instr_set));
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  // ASSERT_MSG(offset >= ARM64_LDR_MIN && offset <= ARM64_LDR_MAX,
  //            "PC Relative offset is too large for ARM64, fix it.");
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_lit_gen.html
  // LDR <Wt>, <label>
  return InstructionARM64(Base(0b00011000, 8), Imm19(offset / 4), Rt(dest.id()));
}

InstructionARM64 load16u_pcRel_s32(Register dest, s64 offset) {
  ASSERT(dest.is_gpr(instr_set));
  // NOTE - the offsets passed into these functions are always `0` and then later patched
  // so im not going to worry about properly encoding the offset
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  // ASSERT_MSG(offset >= ARM64_LDR_MIN && offset <= ARM64_LDR_MAX,
  //            "PC Relative offset is too large for ARM64, fix it.");
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/adrp.html
       // ADRP <Xd>, <label>
       InstructionARM64(Base(0b100100000000000000000000000, 27), Rd(X16), Immhi(0), Immlo(0)),
       // https://www.scs.stanford.edu/~zyedidia/arm64/ldrh_imm.html
       // LDRH <Wt>, [<Xn|SP>{, #<pimm>}]
       InstructionARM64(Base(0b0111100101, 10), Imm12(offset), Rt(dest.id()), Rn(X16))});
}

InstructionARM64 load16s_pcRel_s32(Register dest, s64 offset) {
  ASSERT(dest.is_gpr(instr_set));
  // NOTE - the offsets passed into these functions are always `0` and then later patched
  // so im not going to worry about properly encoding the offset
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  // ASSERT_MSG(offset >= ARM64_LDR_MIN && offset <= ARM64_LDR_MAX,
  //            "PC Relative offset is too large for ARM64, fix it.");
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/adrp.html
       // ADRP <Xd>, <label>
       InstructionARM64(Base(0b100100000000000000000000000, 27), Rd(X16), Immhi(0), Immlo(0)),
       // https://www.scs.stanford.edu/~zyedidia/arm64/ldrsh_imm.html
       // LDRSH <Xt>, [<Xn|SP>{, #<pimm>}]
       InstructionARM64(Base(0b0111100110, 10), Imm12(offset), Rt(dest.id()), Rn(X16))});
}

InstructionARM64 load8u_pcRel_s32(Register dest, s64 offset) {
  ASSERT(dest.is_gpr(instr_set));
  // NOTE - the offsets passed into these functions are always `0` and then later patched
  // so im not going to worry about properly encoding the offset
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  // ASSERT_MSG(offset >= ARM64_LDR_MIN && offset <= ARM64_LDR_MAX,
  //            "PC Relative offset is too large for ARM64, fix it.");
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/adrp.html
       // ADRP <Xd>, <label>
       InstructionARM64(Base(0b100100000000000000000000000, 27), Rd(X16), Immhi(0), Immlo(0)),
       // https://www.scs.stanford.edu/~zyedidia/arm64/ldrb_imm.html
       // LDRB <Wt>, [<Xn|SP>{, #<pimm>}]
       InstructionARM64(Base(0b0011100101, 10), Imm12(offset), Rt(dest.id()), Rn(X16))});
}

InstructionARM64 load8s_pcRel_s32(Register dest, s64 offset) {
  ASSERT(dest.is_gpr(instr_set));
  // NOTE - the offsets passed into these functions are always `0` and then later patched
  // so im not going to worry about properly encoding the offset
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  // ASSERT_MSG(offset >= ARM64_LDR_MIN && offset <= ARM64_LDR_MAX,
  //            "PC Relative offset is too large for ARM64, fix it.");
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/adrp.html
       // ADRP <Xd>, <label>
       InstructionARM64(Base(0b100100000000000000000000000, 27), Rd(X16), Immhi(0), Immlo(0)),
       // https://www.scs.stanford.edu/~zyedidia/arm64/ldrsb_imm.html
       // LDRSB <Xt>, [<Xn|SP>{, #<pimm>}]
       InstructionARM64(Base(0b0011100110, 10), Imm12(offset), Rt(dest.id()), Rn(X16))});
}

InstructionARM64 static_load(Register dest, s64 offset, int size, bool sign_extend) {
  switch (size) {
    case 1:
      if (sign_extend) {
        return load8s_pcRel_s32(dest, offset);
      } else {
        return load8u_pcRel_s32(dest, offset);
      }
      break;
    case 2:
      if (sign_extend) {
        return load16s_pcRel_s32(dest, offset);
      } else {
        return load16u_pcRel_s32(dest, offset);
      }
      break;
    case 4:
      if (sign_extend) {
        return load32s_pcRel_s32(dest, offset);
      } else {
        return load32u_pcRel_s32(dest, offset);
      }
      break;
    case 8:
      return load64_pcRel_s32(dest, offset);
    default:
      ASSERT(false);
  }
}

// TODO ARM - no direct store instructions, gotta be two and involve a register

InstructionARM64 store64_pcRel_s32(Register src, s64 offset) {
  ASSERT(src.is_gpr(instr_set));
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/adrp.html
       // ADRP <Xd>, <label>
       InstructionARM64(Base(0b100100000000000000000000000, 27), Rd(X16), Immhi(0), Immlo(0)),
       // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_gen.html
       // STR <Xt>, [<Xn|SP>{, #<pimm>}]
       InstructionARM64(Base(0b1111100100, 10), Imm12(offset), Rt(src.id()), Rn(X16))});
}

InstructionARM64 store32_pcRel_s32(Register src, s64 offset) {
  ASSERT(src.is_gpr(instr_set));
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/adrp.html
       // ADRP <Xd>, <label>
       InstructionARM64(Base(0b100100000000000000000000000, 27), Rd(X16), Immhi(0), Immlo(0)),
       // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_gen.html
       // STR <Wt>, [<Xn|SP>{, #<pimm>}]
       InstructionARM64(Base(0b1011100100, 10), Imm12(offset), Rt(src.id()), Rn(X16))});
}

InstructionARM64 store16_pcRel_s32(Register src, s64 offset) {
  ASSERT(src.is_gpr(instr_set));
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/adrp.html
       // ADRP <Xd>, <label>
       InstructionARM64(Base(0b100100000000000000000000000, 27), Rd(X16), Immhi(0), Immlo(0)),
       // https://www.scs.stanford.edu/~zyedidia/arm64/strh_imm.html
       // STRH <Wt>, [<Xn|SP>{, #<pimm>}]
       InstructionARM64(Base(0b0111100100, 10), Imm12(offset), Rt(src.id()), Rn(X16))});
}

InstructionARM64 store8_pcRel_s32(Register src, s64 offset) {
  ASSERT(src.is_gpr(instr_set));
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/adrp.html
       // ADRP <Xd>, <label>
       InstructionARM64(Base(0b100100000000000000000000000, 27), Rd(X16), Immhi(0), Immlo(0)),
       // https://www.scs.stanford.edu/~zyedidia/arm64/strb_imm.html
       // STRH <Wt>, [<Xn|SP>{, #<pimm>}]
       InstructionARM64(Base(0b0011100100, 10), Imm12(offset), Rt(src.id()), Rn(X16))});
}

InstructionARM64 static_store(Register value, s64 offset, int size) {
  switch (size) {
    case 1:
      return store8_pcRel_s32(value, offset);
    case 2:
      return store16_pcRel_s32(value, offset);
    case 4:
      return store32_pcRel_s32(value, offset);
    case 8:
      return store64_pcRel_s32(value, offset);
    default:
      ASSERT(false);
  }
}

/*!
 * Loads into `dest` the real absolute address of something in a *different* segment - a
 * cross-segment reference, resolved by the runtime linker once both segments are actually loaded
 * (segments can end up at different relative distances from each other on different runs, so this
 * can't be baked in at compile time - same reason x86 needs RIP-relative addressing for this
 * instead of a plain absolute constant).
 *
 * Real ARM64 has no "PC as a plain GPR operand" instruction, and its own PC-relative addressing
 * (ADRP) computes a 4KB-page-aligned base from a 21-bit immediate split across two non-contiguous
 * bit fields (immhi/immlo) - completely incompatible with the runtime linker's existing "write a
 * plain N-byte little-endian integer at this byte offset" patching
 * (ObjectGenerator::emit_link_rip() / game/kernel's per-game klink.cpp cross_seg_dist_link_v3(), shared
 * unchanged with x86 - the mechanism this function reuses). Rather than teach the runtime linker a
 * second, ARM64-specific patching scheme (bit-field re-encoding, and unlike x86, duplicated across
 * every game's klink.cpp), this instead builds a tiny literal pool inline:
 *
 *   ADR   X16, #24          ; X16 = address of the byte immediately after this whole sequence
 *                           ; (a fixed, compile-time-known 24-byte offset from ADR's own address -
 *                           ; needs no patching, unlike the ADRP the older static_addr()/
 *                           ; static_load_f32()/loadvf_rip_plus_s32() attempted and left as a
 *                           ; "TODO: encode it properly" stub)
 *   B     #12               ; jump over the 2-word literal below - see branch_over()'s own comment
 *                           ; for why this can't be left out: every word here is really fetched
 *                           ; and executed as an instruction on real hardware unless something
 *                           ; branches around it, patched or not
 *   .dword 0                ; 8-byte placeholder - THIS is what gets patched, as a *plain* 64-bit
 *                           ; distance value (LINK_DISTANCE_TO_OTHER_SEG_64), because it's now
 *                           ; just a data word sitting in the instruction stream, not bits packed
 *                           ; inside an instruction encoding
 *   LDUR  Xd, [X16, #-16]   ; Xd = the (now-patched) distance
 *   ADD   Xd, X16, Xd       ; Xd = (address right after this sequence) + distance = real target
 *
 * This lines up with link_instruction_static()'s own hardcoded assumption (mirroring x86 RIP
 * semantics) that "mine" - the base the patched distance is relative to - is always "the byte
 * address immediately after the instruction/sequence this link is attached to": ADR's own target
 * (24 bytes ahead of itself) *is* exactly that address, by construction, since this whole 6-word
 * sequence is emitted as a single add_instr() call. Adding the `B` doesn't change ADR's own target
 * or LDUR's own offset from it - both shift by exactly the one word the `B` occupies, so their
 * relative distance to each other (and to "right after the whole sequence") is unaffected; only the
 * absolute immediates (ADR's `#20`->`#24`, this function's own `with_disp` byte offset) needed
 * updating for the sequence being one word longer.
 */
InstructionARM64 load_cross_seg_addr(Register dest) {
  ASSERT(dest.is_gpr(instr_set));
  InstructionARM64 adr(Base(0b000100000000000000000000000, 27), Immlo(24 & 0b11),
                       Immhi((24 >> 2) & 0x7FFFF), Rd(X16));
  InstructionARM64 skip = branch_over(12);  // jump over the 2-word literal right below
  InstructionARM64 literal_lo(0);  // patched: low 32 bits of the cross-segment distance
  InstructionARM64 literal_hi(0);  // patched: high 32 bits (sign sits here for a negative
                                   // distance - the linker writes a real int64_t)
  // LDUR <Xt>, [<Xn|SP>, #<simm>] - plain (non-indexed) signed 9-bit immediate offset load.
  InstructionARM64 load(Base(0b1111100001000000000000, 22), Imm9s(-16), Rt(dest.id()), Rn(X16));
  InstructionARM64 add = add_gpr64_gpr64(dest, X16);
  InstructionARM64 result(adr, skip, literal_lo, literal_hi, load, add);
  result.with_disp(8, 8);  // byte offset 8 (right after the ADR + the branch), 8-byte value
  return result;
}

InstructionARM64 load_symbol_ptr_offset(Register dest, Register st_reg) {
  ASSERT(dest.is_gpr(instr_set));
  ASSERT(st_reg.is_gpr(instr_set));
  // branch_over()'s argument is a byte offset from the *branch instruction's own* address (real
  // ARM64 PC-relative semantics), not from the word after it - so skipping exactly one 4-byte
  // literal needs 4 (the branch's own width) + 4 (the literal) = 8, not 4. branch_over(4) would
  // land the jump *on* the literal instead of past it (confirmed as a real bug this way: crashed
  // real ARM64/Android hardware with SIGILL, PC sitting exactly on the literal's address - see
  // the development notes). Compare load_cross_seg_addr() above, which gets this right for its 2-word
  // literal: branch_over(12) = 4 (branch) + 2*4 (literal).
  InstructionARM64 skip = branch_over(8);  // jump over the literal word right below
  InstructionARM64 literal(0x0afecafe);  // patched by the runtime linker's symlink_v3()
  // LDRSW <Xt>, <label> - PC-relative literal load, sign-extending a 32-bit value to 64 bits in a
  // single instruction (matching x86's "add reg64, imm32", which sign-extends its 32-bit immediate
  // the same way). `label` is 1 word (4 bytes) behind this instruction - right where `literal`
  // above sits, since this whole 4-word sequence is emitted as a single add_instr() call.
  InstructionARM64 ldrsw(Base(0b10011000, 8), Imm19(static_cast<u32>(-1) & 0x7ffff), Rt(dest.id()));
  InstructionARM64 add = add_gpr64_gpr64(dest, st_reg);
  InstructionARM64 result(skip, literal, ldrsw, add);
  result.with_disp(4, 4);  // byte offset 4 (right after the branch), 4-byte value
  return result;
}

InstructionARM64 load_symbol_value(Register dest,
                                   Register st_reg,
                                   Register offset_reg,
                                   Register scratch,
                                   u32 link_placeholder,
                                   bool sign_extend) {
  if (dest == SP) {
    // Loads interpret Rt=31 as XZR, and the address calculation must not change SP.
    // Use the reserved address scratch, then explicitly install the loaded value. (AI-assisted)
    ASSERT(scratch != X16);
    const auto load =
        load_symbol_value(X16, st_reg, offset_reg, scratch, link_placeholder, sign_extend);
    InstructionARM64 result(load, mov_gpr64_gpr64(SP, X16));
    result.with_disp(load.offset_of_disp(), load.get_disp_size());
    return result;
  }
  // Same runtime-patched-literal idea as load_symbol_ptr_offset() above, but this is
  // IR_GetSymbolValue's case: x86's version (load32s_gpr64_gpr64_plus_gpr64_plus_s32(dst, st_reg,
  // offset_reg, LINK_SYM_NO_OFFSET_FLAG)) reads a symbol's *value* - it needs TWO base registers
  // (st_reg and offset_reg - see IR.cpp/Register.h for what "offset" is for) added to the
  // runtime-patched literal, then a 32-bit memory load from the result, not just a pointer
  // computation. Needs one extra scratch register beyond dest (to hold the patched literal
  // separately from the running address in dest) - see IR_GetSymbolValue::to_rai() for where that
  // comes from (same `clobber` trick IR_IntegerMath's mod cases use). `link_placeholder` is passed
  // in by the caller rather than hardcoded here, since it's not just an arbitrary sentinel: on
  // jak2/jak3/jakx, symlink_v3()/klink.cpp specifically recognizes LINK_SYM_NO_OFFSET_FLAG
  // (common/link_types.h) and stores `sym_offset - 1` instead of the usual `sym_offset` for it -
  // getting this exact value wrong would silently mis-link on those games (jak1 has no such
  // special case and treats it like any other non-(-1) placeholder either way).
  ASSERT(dest.is_gpr(instr_set));
  ASSERT(st_reg.is_gpr(instr_set));
  ASSERT(offset_reg.is_gpr(instr_set));
  ASSERT(scratch.is_gpr(instr_set));
  // ADD <Xd>, <Xn>, <Xm> - dest = st_reg + offset_reg (a real 3-operand add, unlike
  // add_gpr64_gpr64()'s 2-operand dst-is-also-a-source form)
  InstructionARM64 add_bases(Base(0b10001011000, 11), Rd(dest.id()), Imm6(0), Rn(st_reg.id()),
                             Rm(offset_reg.id()));
  // branch_over()'s argument is a byte offset from the *branch instruction's own* address (real
  // ARM64 PC-relative semantics), not from the word after it - so skipping exactly one 4-byte
  // literal needs 4 (the branch's own width) + 4 (the literal) = 8, not 4. branch_over(4) would
  // land the jump *on* the literal instead of past it (confirmed as a real bug this way: crashed
  // real ARM64/Android hardware with SIGILL, PC sitting exactly on the literal's address - see
  // the development notes). Compare load_cross_seg_addr() above, which gets this right for its 2-word
  // literal: branch_over(12) = 4 (branch) + 2*4 (literal).
  InstructionARM64 skip = branch_over(8);  // jump over the literal word right below
  InstructionARM64 literal(link_placeholder);  // patched by the runtime linker's symlink_v3()
  // LDRSW <Xt>, <label> - see load_symbol_ptr_offset() above; `label` is 1 word behind this one.
  InstructionARM64 ldrsw_literal(Base(0b10011000, 8), Imm19(static_cast<u32>(-1) & 0x7ffff),
                                Rt(scratch.id()));
  InstructionARM64 add_offset = add_gpr64_gpr64(dest, scratch);  // dest += patched offset
  InstructionARM64 load = sign_extend
                              // LDURSW <Xt>, [<Xn|SP>{, #0}] - 32-bit sign-extending load
                              ? InstructionARM64(Base(0b1011100010000000000000, 22), Imm9s(0),
                                                Rt(dest.id()), Rn(dest.id()))
                              // LDUR <Wt>, [<Xn|SP>{, #0}] - 32-bit zero-extending load
                              : InstructionARM64(Base(0b1011100001000000000000, 22), Imm9s(0),
                                                Rt(dest.id()), Rn(dest.id()));
  InstructionARM64 result(add_bases, skip, literal, ldrsw_literal, add_offset, load);
  result.with_disp(8, 4);  // byte offset 8 (right after add_bases + the branch), 4-byte value
  return result;
}

InstructionARM64 store_symbol_value(Register st_reg,
                                    Register offset_reg,
                                    Register value,
                                    Register scratch,
                                    u32 link_placeholder) {
  ASSERT(st_reg.is_gpr(instr_set));
  ASSERT(offset_reg.is_gpr(instr_set));
  ASSERT(value.is_gpr(instr_set));
  ASSERT(scratch.is_gpr(instr_set));
  // branch_over()'s argument is a byte offset from the *branch instruction's own* address (real
  // ARM64 PC-relative semantics), not from the word after it - so skipping exactly one 4-byte
  // literal needs 4 (the branch's own width) + 4 (the literal) = 8, not 4. branch_over(4) would
  // land the jump *on* the literal instead of past it (confirmed as a real bug this way: crashed
  // real ARM64/Android hardware with SIGILL, PC sitting exactly on the literal's address - see
  // the development notes). Compare load_cross_seg_addr() above, which gets this right for its 2-word
  // literal: branch_over(12) = 4 (branch) + 2*4 (literal).
  InstructionARM64 skip = branch_over(8);  // jump over the literal word right below
  InstructionARM64 literal(link_placeholder);  // patched by the runtime linker's symlink_v3()
  // LDRSW <Xt>, <label> - see load_symbol_ptr_offset() above; `label` is 1 word behind this one.
  InstructionARM64 ldrsw_literal(Base(0b10011000, 8), Imm19(static_cast<u32>(-1) & 0x7ffff),
                                Rt(scratch.id()));
  InstructionARM64 add_st = add_gpr64_gpr64(scratch, st_reg);          // scratch += st_reg
  InstructionARM64 add_offset = add_gpr64_gpr64(scratch, offset_reg);  // scratch += offset_reg
  // STR <Wt>, [<Xn|SP>{, #0}] - 32-bit store, unsigned immediate offset (same encoding family
  // store32_gpr64_gpr64_plus_gpr64_plus_s8() uses, just with the offset baked to 0 here).
  InstructionARM64 store(Base(0b1011100100, 10), Imm12(0), Rt(value.id()), Rn(scratch.id()));
  InstructionARM64 result(skip, literal, ldrsw_literal, add_st, add_offset, store);
  result.with_disp(4, 4);  // byte offset 4 (right after the branch), 4-byte value
  return result;
}

InstructionARM64 static_addr(Register dest, s64 offset) {
  ASSERT(dest.is_gpr(instr_set));
  // Both real callers (IR_StaticVarAddr::do_codegen_x86/arm64, IR_FunctionAddr::do_codegen_x86/arm64
  // - goalc/compiler/IR.cpp) always pass 0 here: `offset` is a linker-patched placeholder, not an
  // already-known value, exactly like load_cross_seg_addr()'s address - they resolve the real
  // address via link_instruction_static()/link_instruction_to_function() once memory layout is
  // known, the same deferred cross-segment addressing load_cross_seg_addr() already handles (its
  // own emit_link_rip() consumer doesn't distinguish "static data" from "another function" from
  // "cross-segment" - it's all just "patch a distance to some other place in memory"). A direct
  // `LDR <Xt>, <label>` with the offset baked into its Imm19 field at *this* function's own
  // encoding time - what used to be here - can't be patched afterward at all (Imm19 is bits packed
  // inside the instruction word, not a separate byte-aligned literal), which is exactly why it
  // used to (correctly) refuse to run with the placeholder 0 its only two callers actually pass.
  ASSERT_MSG(offset == 0, "static_addr's offset is always a to-be-linker-patched placeholder");
  return load_cross_seg_addr(dest);
}

InstructionARM64 static_load_f32(Register simd_dest, s64 offset) {
  ASSERT(simd_dest.is_128bit_simd(instr_set));
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_lit_fpsimd.html
  // LDR <St>, <label>
  return InstructionARM64(Base(0b00011100, 8), Imm19(offset / 4), Rt(simd_dest.id()));
}

InstructionARM64 static_store_f32(Register xmm_value, s64 offset) {
  ASSERT(xmm_value.is_128bit_simd(instr_set));
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/adrp.html
       // ADRP <Xd>, <label>
       InstructionARM64(Base(0b100100000000000000000000000, 27), Rd(X16), Immhi(0), Immlo(0)),
       // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_fpsimd.html
       // STR <St>, [<Xn|SP>{, #<pimm>}]
       InstructionARM64(Base(0b1011110100, 10), Imm12(offset), Rt(xmm_value.id()), Rn(X16))});
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   FUNCTION STUFF
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

InstructionARM64 ret() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ret.html
  // - defaults to using X30 if Rn is absent
  return InstructionARM64(Base(0b1101011001011111000000, 22), Rn(30));
}

InstructionARM64 push_gpr64(Register reg) {
  // ARM64 stack grows down, so we subtract 16 from SP and store the register
  // Equivalent assembly: STR reg, [SP, #-16]!
  // - https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_gen.html
  // We use 16 because in ARM, the stack must be 16-byte aligned.
  // This does mean we are inefficiently using the stack, there are a few better options:
  // - Push in pairs, two registers at a time
  // - Preallocate stack-space
  // But we can't do either of these at this level, this is an optimization that has to come from
  // higher in the stack.  Here we are concerned with just satisfying the need to push a GPR
  ASSERT(reg.is_gpr(instr_set));
  return InstructionARM64(Base(0b1111100000000000000011, 22), Imm9s(-16), Rn(ARM64_REG::SP),
                          Rt(reg.id()));
}

InstructionARM64 pop_gpr64(Register reg) {
  // ldr reg, [sp], #16
  // - https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_gen.html
  ASSERT(reg.is_gpr(instr_set));
  return InstructionARM64(Base(0b1111100001000000000001, 22), Imm9s(16), Rn(ARM64_REG::SP),
                          Rt(reg.id()));
}

InstructionARM64 call_r64(Register reg) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/blr.html
  // BLR <Xn>
  ASSERT(reg.is_gpr(instr_set));
  return InstructionARM64(Base(0b1101011000111111000000, 22), Rn(reg.id()));
}

InstructionARM64 jmp_r64(Register reg) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/br.html
  // BR <Xn>
  ASSERT(reg.is_gpr(instr_set));
  return InstructionARM64(Base(0b1101011000011111000000, 22), Rn(reg.id()));
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   INTEGER MATH
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

InstructionARM64 sub_gpr64_imm8s(Register reg, int64_t imm) {
  return sub_gpr64_imm(reg, imm);
}

InstructionARM64 add_gpr64_imm8s(Register reg, int64_t imm) {
  return add_gpr64_imm(reg, imm);
}

InstructionARM64 sub_gpr64_imm32s(Register reg, int64_t imm) {
  return sub_gpr64_imm(reg, imm);
}

InstructionARM64 add_gpr64_imm32s(Register reg, int64_t imm) {
  return add_gpr64_imm(reg, imm);
}

InstructionARM64 add_gpr64_imm(Register reg, int64_t imm) {
  ASSERT(reg.is_gpr(instr_set));
  if (imm < 0) {
    return sub_gpr64_imm(reg, std::abs(imm));
  }
  // Check to see if we can represent this subtraction in a single instruction
  // if not, then we need to emit multiple partial instructions
  const auto [is_single_instr, imm12, needs_shift] = can_encode_single_imm12(imm);
  if (is_single_instr) {
    // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html
    // ADD <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
    return InstructionARM64(Base(0b100100010, 9), Sh(needs_shift ? 1 : 0), Imm12(imm12),
                            Rd(reg.id()), Rn(reg.id()));
  } else {
    std::vector<InstructionARM64> instrs = construct_multiple_imm12_adds(imm, reg.id());
    return InstructionARM64(instrs);
  }
}

InstructionARM64 sub_gpr64_imm(Register reg, int64_t imm) {
  ASSERT(reg.is_gpr(instr_set));
  if (imm < 0) {
    return add_gpr64_imm(reg, std::abs(imm));
  }
  // Check to see if we can represent this subtraction in a single instruction
  // if not, then we need to emit multiple partial instructions
  const auto [is_single_instr, imm12, needs_shift] = can_encode_single_imm12(imm);
  if (is_single_instr) {
    // https://www.scs.stanford.edu/~zyedidia/arm64/sub_addsub_imm.html
    // SUB <Xd|SP>, <Xn|SP>, #<imm>{, <shift>}
    return InstructionARM64(Base(0b110100010, 9), Sh(needs_shift ? 1 : 0), Imm12(imm12),
                            Rd(reg.id()), Rn(reg.id()));
  } else {
    std::vector<InstructionARM64> instrs = construct_multiple_imm12_subs(imm, reg.id());
    return InstructionARM64(instrs);
  }
}

InstructionARM64 add_gpr64_gpr64(Register dst, Register src) {
  // BUG (found this session, same class as mov_gpr64_gpr64() above): the "ADD (shifted register)"
  // form used below hardwires register 31 in the Rd/Rn positions to mean XZR, not SP - so
  // `(.add sp off)` (used by gkernel.gc's hand-written stack-switching asm-funcs, e.g.
  // `reset-and-call`) silently added into XZR and discarded the result instead of updating the
  // real stack pointer. Needs the "ADD (extended register)" form instead when SP is involved,
  // which does treat 31 as SP in Rd/Rn (Rm still can't be SP/31 in this form, but every real
  // caller here only ever has SP as dst, never as the value being added). Confirmed via a real
  // `add sp, sp, x22` assembled by the NDK's own clang (`0x8b3663ff`).
  if (dst.id() == emitter::SP || src.id() == emitter::SP) {
    ASSERT(src.id() != emitter::SP);  // SP can't be the Rm operand of this instruction form
    // ADD <Xd|SP>, <Xn|SP>, <Xm>, UXTX #0
    // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_ext.html
    // option field (bits[15:13]) = 0b011 = UXTX (plain 64-bit add, no extension) - same option
    // value/bug class as this session's other option-field fixes (see storevf_gpr64_plus_gpr64()
    // above); imm3 (bits[12:10], extend amount) stays 0.
    return InstructionARM64(Base(0b10001011001, 11), Rm(src.id()), Field{0b011 << 13},
                            Rn(dst.id()), Rd(dst.id()));
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
  // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
  return InstructionARM64(Base(0b10001011000, 11), Rd(dst.id()), Imm6(0), Rn(dst.id()),
                          Rm(src.id()));
}

InstructionARM64 sub_gpr64_gpr64(Register dst, Register src) {
  // Same SP/XZR-aliasing bug as add_gpr64_gpr64() above, same fix (extended-register form instead
  // of shifted-register). Confirmed via a real `sub sp, sp, x22` assembled by the NDK's own clang
  // (`0xcb3663ff`).
  if (dst.id() == emitter::SP || src.id() == emitter::SP) {
    ASSERT(src.id() != emitter::SP);  // SP can't be the Rm operand of this instruction form
    // SUB <Xd|SP>, <Xn|SP>, <Xm>, UXTX #0
    // https://www.scs.stanford.edu/~zyedidia/arm64/sub_addsub_ext.html
    return InstructionARM64(Base(0b11001011001, 11), Rm(src.id()), Field{0b011 << 13},
                            Rn(dst.id()), Rd(dst.id()));
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/sub_addsub_shift.html
  // SUB <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
  return InstructionARM64(Base(0b11001011000, 11), Rd(dst.id()), Imm6(0), Rn(dst.id()),
                          Rm(src.id()));
}

InstructionARM64 imul_gpr32_gpr32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/mul_madd.html
  // MUL <Wd>, <Wn>, <Wm>
  return InstructionARM64(Base(0b0001101100000000011111, 22), Rd(dst.id()), Rn(dst.id()),
                          Rm(src.id()));
}

InstructionARM64 imul_gpr64_gpr64(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/mul_madd.html
  // MUL <Xd>, <Xn>, <Xm>
  return InstructionARM64(Base(0b1001101100000000011111, 22), Rd(dst.id()), Rn(dst.id()),
                          Rm(src.id()));
}

InstructionARM64 idiv_gpr32(Register reg) {
  // divides on x86 are annoying, its one of many that involve hard-coded register src/destinations,
  // deal with it last
  ASSERT_MSG(false, "not yet implemented");
  return InstructionARM64(0b0);
}

InstructionARM64 unsigned_div_gpr32(Register reg) {
  // divides on x86 are annoying, its one of many that involve hard-coded register src/destinations,
  // deal with it last
  ASSERT_MSG(false, "not yet implemented");
  return InstructionARM64(0b0);
}

InstructionARM64 cdq() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/asr_asrv.html
  // asr x3, x0, #63
  // (using X3 = edx and X0 = eax)
  // TODO - hardcoded registers, need to check this...
  ASSERT_MSG(false, "not yet implemented");
  return InstructionARM64(Base(0b1001101011000000001010, 22), Rm(63), Rn(ARM64_REG::X0),
                          Rd(ARM64_REG::X3));
}

InstructionARM64 sdiv_gpr32_gpr32_gpr32(Register dst, Register n, Register m) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/sdiv_int.html
  // SDIV <Wd>, <Wn>, <Wm> (32-bit: sf=0) - same "data-processing (2 source)" family as
  // shl/shr/sar_gpr64_reg above (LSLV/LSRV/ASRV opcodes 001000/001001/001010), just sf=0 and
  // opcode=000011 for SDIV.
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(n.is_gpr(instr_set));
  ASSERT(m.is_gpr(instr_set));
  return InstructionARM64(Base(0b0001101011000000000011, 22), Rd(dst.id()), Rn(n.id()), Rm(m.id()));
}

InstructionARM64 udiv_gpr32_gpr32_gpr32(Register dst, Register n, Register m) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/udiv_int.html
  // UDIV <Wd>, <Wn>, <Wm> (32-bit: sf=0, opcode=000010)
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(n.is_gpr(instr_set));
  ASSERT(m.is_gpr(instr_set));
  return InstructionARM64(Base(0b0001101011000000000010, 22), Rd(dst.id()), Rn(n.id()), Rm(m.id()));
}

InstructionARM64 msub_gpr32_gpr32_gpr32_gpr32(Register dst, Register n, Register m, Register a) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/msub_int.html
  // MSUB <Wd>, <Wn>, <Wm>, <Wa> (32-bit: sf=0) - "data-processing (3 source)" family, same one
  // imul_gpr32_gpr32's MUL (really MADD with Ra=WZR baked into its base value) uses above, but
  // with a real Ra operand instead of a fixed zero register, and o0=1 (MSUB) instead of o0=0
  // (MADD).
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(n.is_gpr(instr_set));
  ASSERT(m.is_gpr(instr_set));
  ASSERT(a.is_gpr(instr_set));
  return InstructionARM64(Base(0b00011011000000001, 17), Rd(dst.id()), Rn(n.id()), Rm(m.id()),
                          Ra(a.id()));
}

InstructionARM64 movsx_r64_r32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/sxtw_sbfm.html
  // SXTW <Xd>, <Wn>
  return InstructionARM64(Base(0b1001001101000000011111, 22), Rd(dst.id()), Rn(src.id()));
}

InstructionARM64 cmp_gpr64_gpr64(Register a, Register b) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/cmp_subs_addsub_ext.html
  // CMP <Xn|SP>, <R><m>{, <extend> {#<amount>}}
  // This is the "extended register" family of SUBS/CMP - it *always* applies an extend to the
  // second operand before comparing (there is no plain "just compare two full 64-bit registers"
  // variant in this family; that's the separate "shifted register" encoding, bit 21 = 0, not used
  // here). Two real bugs found this session, both now fixed:
  //
  // 1. The second operand was packed with Rn (bits [9:5], the *first* source register field)
  //    instead of Rm (bits [20:16], the real second-operand field) - a copy-paste mistake. This
  //    left every real `b` register invisible to the actual comparison (the Rm field always
  //    encoded as 0/x0, since the Base pattern's own Rm bits were 0 and nothing ever set them),
  //    and could additionally corrupt Rn itself into a garbage combined register number whenever
  //    `a` and `b`'s low 5 bits overlapped (both fields get bitwise-ORed into the same position).
  //
  // 2. The `option` field (bits [15:13], which extend to apply) was hardcoded to `0b000` = UXTB -
  //    "zero-extend the second operand from a *byte*" - for what is supposed to be an ordinary
  //    full 64-bit pointer/value comparison. Every `cmp_gpr64_gpr64` therefore silently truncated
  //    its second operand to 8 bits before comparing. `0b011` = UXTX is the correct option for a
  //    64-bit-register operand (a no-op "extend", i.e. compare the whole register) - see the ARM
  //    reference manual's own CMP (extended register) pseudocode, or any real toolchain's output
  //    for `cmp x0, x1` (always encodes option=011, never 000).
  //
  // Confirmed on real hardware: this exact pair of bugs turned a GOAL `(if (-> obj field) ...)`
  // truthiness/equality check (comparing a loaded field against `#f`/`s7`) into a byte-truncated
  // comparison against the wrong register entirely (decoded as `cmp x6, w0, uxtb` via the crash
  // handler's backward-dump + NDK objdump technique - see the development notes), which could pick the
  // wrong branch of a `cond`/`if` whenever the truncated byte happened to spuriously match -
  // exactly what happened in `(defmethod new cpu-thread ...)` (`goal_src/jak1/kernel/gkernel.gc`),
  // this session's third real ARM64-crash root cause.
  return InstructionARM64(Base(0b11101011001000000110000000011111, 32), Rn(a.id()), Rm(b.id()));
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   BIT STUFF
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

InstructionARM64 or_gpr64_gpr64(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/orr_log_shift.html
  // ORR <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(src.is_gpr(instr_set));
  return InstructionARM64(Base(0b10101010000, 11), Rd(dst.id()), Rn(dst.id()), Rm(src.id()));
}

InstructionARM64 and_gpr64_gpr64(Register dst, Register src) {
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(src.is_gpr(instr_set));
  return InstructionARM64(Base(0b10001010000, 11), Rd(dst.id()), Rn(dst.id()), Rm(src.id()));
}

InstructionARM64 xor_gpr64_gpr64(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/eor_log_shift.html
  // EOR <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
  ASSERT(dst.is_gpr(instr_set));
  ASSERT(src.is_gpr(instr_set));
  return InstructionARM64(Base(0b11001010000, 11), Rd(dst.id()), Rn(dst.id()), Rm(src.id()));
}

InstructionARM64 not_gpr64(Register reg) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/mvn_orn_log_shift.html
  // MVN <Xd>, <Xm>{, <shift> #<amount>}
  // ==
  // ORN <Xd>, XZR, <Xm>{, <shift> #<amount>}
  ASSERT(reg.is_gpr(instr_set));
  return InstructionARM64(Base(0b101010100010000000000011111, 27), Rd(reg.id()), Rm(reg.id()));
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   SHIFTS
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

InstructionARM64 shl_gpr64_reg(Register reg, Register shift_reg) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/lsl_lslv.html
  // LSL <Xd>, <Xn>, <Xm>
  ASSERT(reg.is_gpr(instr_set));
  ASSERT(shift_reg.is_gpr(instr_set));
  return InstructionARM64(Base(0b1001101011000000001000, 22), Rd(reg.id()), Rn(reg.id()),
                          Rm(shift_reg.id()));
}

InstructionARM64 shr_gpr64_reg(Register reg, Register shift_reg) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/lsr_lsrv.html
  // LSR <Xd>, <Xn>, <Xm>
  ASSERT(reg.is_gpr(instr_set));
  ASSERT(shift_reg.is_gpr(instr_set));
  return InstructionARM64(Base(0b1001101011000000001001, 22), Rd(reg.id()), Rn(reg.id()),
                          Rm(shift_reg.id()));
}

InstructionARM64 sar_gpr64_reg(Register reg, Register shift_reg) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/asr_asrv.html
  // ASR <Xd>, <Xn>, <Xm>
  ASSERT(reg.is_gpr(instr_set));
  ASSERT(shift_reg.is_gpr(instr_set));
  return InstructionARM64(Base(0b1001101011000000001010, 22), Rd(reg.id()), Rn(reg.id()),
                          Rm(shift_reg.id()));
}

InstructionARM64 shl_gpr64_u8(Register reg, uint8_t sa) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/lsl_ubfm.html
  // LSL <Xd>, <Xn>, #<shift>
  // A 64-bit shift immediate is valid for the full 0-63 range (e.g. `sar x, 63` is a common "extract
  // the sign bit as an all-0/all-1 mask" idiom) - `sa < 63` wrongly excluded exactly 63, the one
  // value real GOAL code hit here (a genuine `-- SPILL FAILED --`-style off-by-one, not a stub).
  ASSERT(sa <= 63);
  ASSERT(reg.is_gpr(instr_set));
  return InstructionARM64(Base(0b1101001101, 10), Rd(reg.id()), Rn(reg.id()), Immr((64 - sa) & 63),
                          Imms(63 - sa));
}

InstructionARM64 shr_gpr64_u8(Register reg, uint8_t sa) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/lsr_ubfm.html
  // LSR <Xd>, <Xn>, #<shift>
  // sf	1	0	1	0	0	1	1	0	N
  // See shl_gpr64_u8() above - sa == 63 is a valid, common shift amount that `sa < 63` wrongly
  // rejected.
  ASSERT(sa <= 63);
  ASSERT(reg.is_gpr(instr_set));
  return InstructionARM64(Base(0b1101001101000000111111, 22), Rd(reg.id()), Rn(reg.id()), Immr(sa));
}

InstructionARM64 sar_gpr64_u8(Register reg, uint8_t sa) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/asr_sbfm.html
  // ASR <Xd>, <Xn>, #<shift>
  // See shl_gpr64_u8() above - sa == 63 is a valid, common shift amount that `sa < 63` wrongly
  // rejected.
  ASSERT(sa <= 63);
  ASSERT(reg.is_gpr(instr_set));
  return InstructionARM64(Base(0b1001001101000000111111, 22), Rd(reg.id()), Rn(reg.id()), Immr(sa));
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   CONTROL FLOW
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//
// All of these instructions jump to a target that is zero
// and then its up to the IR to patch the actual target
//
// Critically, on arm these relative targets must be within
// 128MB, which is much less than x86 (~2GB)
//
// However, these functions are only really used for jumps within
// a given function...of which the largest we've seen isn't even in the MB
// of sizes
//
// So for now, keep it simple and don't implement something more
// complicated like veneers, this should be fine.

InstructionARM64 jmp_imm() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/b_uncond.html
  // B <label>
  return InstructionARM64(Base(0b000101, 6), Imm26(0)).with_branch_imm(26);
}

// Now these instructions in ARM are even more limiting, conditional
// branches must be within 1MB relative to the instruction
//
// However once again, that is still WAY below our biggest functions of a few kb
//
// But still...be aware!  There should be some assertions in place in the patching so that
// said issues don't just fly under the radar
//
// Also, these instructions may have to be patched slightly differently since
// ARM uses a single branch instruction for all
//
// It's worth noting that these x86 instructions also have limitations, they cannot
// jump to far labels (labels in other code segments).  But that's more difficult to
// give a numeric value to like with ARM.

InstructionARM64 je_imm() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/b_cond.html
  // B.<cond> <label>
  // 0000 	EQ
  return InstructionARM64(Base(0b01010100, 8), Imm19(0), Cond(0b0000)).with_branch_imm(19);
}

InstructionARM64 jne_imm() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/b_cond.html
  // B.<cond> <label>
  // 0001 	NE
  return InstructionARM64(Base(0b01010100, 8), Imm19(0), Cond(0b0001)).with_branch_imm(19);
}

InstructionARM64 jle_imm() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/b_cond.html
  // B.<cond> <label>
  // 1101 	LE
  return InstructionARM64(Base(0b01010100, 8), Imm19(0), Cond(0b1101)).with_branch_imm(19);
}

InstructionARM64 jge_imm() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/b_cond.html
  // B.<cond> <label>
  // 1010 	GE
  return InstructionARM64(Base(0b01010100, 8), Imm19(0), Cond(0b1010)).with_branch_imm(19);
}

InstructionARM64 jl_imm() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/b_cond.html
  // B.<cond> <label>
  // 1011 	LT
  return InstructionARM64(Base(0b01010100, 8), Imm19(0), Cond(0b1011)).with_branch_imm(19);
}

InstructionARM64 jg_imm() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/b_cond.html
  // B.<cond> <label>
  // 1100 	GT
  return InstructionARM64(Base(0b01010100, 8), Imm19(0), Cond(0b1100)).with_branch_imm(19);
}

InstructionARM64 jbe_imm() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/b_cond.html
  // B.<cond> <label>
  // 1001 	LS
  return InstructionARM64(Base(0b01010100, 8), Imm19(0), Cond(0b1001)).with_branch_imm(19);
}

InstructionARM64 jae_imm() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/b_cond.html
  // B.<cond> <label>
  // 0010 	CS
  return InstructionARM64(Base(0b01010100, 8), Imm19(0), Cond(0b0010)).with_branch_imm(19);
}

InstructionARM64 jb_imm() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/b_cond.html
  // B.<cond> <label>
  // 0011 	CC
  return InstructionARM64(Base(0b01010100, 8), Imm19(0), Cond(0b0011)).with_branch_imm(19);
}

InstructionARM64 ja_imm() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/b_cond.html
  // B.<cond> <label>
  // 1000 	HI
  return InstructionARM64(Base(0b01010100, 8), Imm19(0), Cond(0b1000)).with_branch_imm(19);
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   FLOAT MATH
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

InstructionARM64 cmp_f32_f32(Register a, Register b) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fcmp_float.html
  // Single-precision (ftype == 00 && opc == 00)
  // FCMP <Sn>, <Sm>
  return InstructionARM64(Base(0b00011110001000000010000000000000, 32), Rn(a.id()), Rm(b.id()));
}

InstructionARM64 sqrt_f32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fsqrt_float.html
  // Single-precision (ftype == 00)
  // FSQRT <Sd>, <Sn>
  // Destination is Rd; using Rm changes the unary opcode instead. (AI-assisted)
  return InstructionARM64(Base(0b0001111000100001110000, 22), Rn(src.id()), Rd(dst.id()));
}

InstructionARM64 mul_f32_f32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fmul_float.html
  // Single-precision (ftype == 00)
  // FMUL <Sd>, <Sn>, <Sm>
  return InstructionARM64(Base(0b0001111000100000000010, 22), Rd(dst.id()), Rn(dst.id()),
                          Rm(src.id()));
}

InstructionARM64 div_f32_f32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fdiv_float.html
  // Single-precision (ftype == 00)
  // FDIV <Sd>, <Sn>, <Sm>
  return InstructionARM64(Base(0b0001111000100000000110, 22), Rd(dst.id()), Rn(dst.id()),
                          Rm(src.id()));
}

InstructionARM64 sub_f32_f32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fsub_float.html
  // Single-precision (ftype == 00)
  // FSUB <Sd>, <Sn>, <Sm>
  return InstructionARM64(Base(0b0001111000100000001110, 22), Rd(dst.id()), Rn(dst.id()),
                          Rm(src.id()));
}

InstructionARM64 add_f32_f32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fadd_float.html
  // Single-precision (ftype == 00)
  // FADD <Sd>, <Sn>, <Sm>
  return InstructionARM64(Base(0b0001111000100000001010, 22), Rd(dst.id()), Rn(dst.id()),
                          Rm(src.id()));
}

InstructionARM64 min_f32_f32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fmin_float.html
  // Single-precision (ftype == 00)
  // FMIN <Sd>, <Sn>, <Sm>
  return InstructionARM64(Base(0b0001111000100000010110, 22), Rd(dst.id()), Rn(dst.id()),
                          Rm(src.id()));
}

InstructionARM64 max_f32_f32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fmax_float.html
  // Single-precision (ftype == 00)
  // FMAX <Sd>, <Sn>, <Sm>
  return InstructionARM64(Base(0b0001111000100000010010, 22), Rd(dst.id()), Rn(dst.id()),
                          Rm(src.id()));
}

InstructionARM64 int32_to_f32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/scvtf_float_int.html
  // 32-bit to single-precision (sf == 0 && ftype == 00)
  // SCVTF <Sd>, <Wn>
  return InstructionARM64(Base(0b0001111000100010000000, 22), Rd(dst.id()), Rn(src.id()));
}

InstructionARM64 f32_to_int32(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fcvtzs_float_int.html
  // 32-bit to single-precision (sf == 0 && ftype == 00)
  // FCVTZS <Wd>, <Sn>
  return InstructionARM64(Base(0b0001111000111000000000, 22), Rd(dst.id()), Rn(src.id()));
}

InstructionARM64 nop() {
  // https://www.scs.stanford.edu/~zyedidia/arm64/nop.html
  return InstructionARM64(Base(0b11010101000000110010000000011111, 32));
}

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   UTILITIES
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

InstructionARM64 null() {
  // dummy empty byte
  return InstructionARM64(0b0);
}

/////////////////////////////
// AVX (VF - Vector Float) //
/////////////////////////////

InstructionARM64 nop_vf() {
  // Not sure if this one was even needed for x86, but it does not really exist on ARM64
  // just use a normal nop
  return nop();
}

InstructionARM64 wait_vf() {
  // Another instruction that doesnt really map to arm64 because there is no annoying
  // x87 FPU behaviour
  return nop();
}

InstructionARM64 mov_vf_vf(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/mov_orr_advsimd_reg.html
  // MOV <Vd>.<T>, <Vn>.<T>
  // Q 	<T>
  // 0 	8B
  // 1 	16B
  ASSERT(dst.is_128bit_simd(instr_set));
  ASSERT(src.is_128bit_simd(instr_set));
  // MOV aliases ORR Vd.16B, Vn.16B, Vn.16B: both source fields must match.
  // Leaving Rm at zero mixed unrelated V0 bits into every 128-bit copy. (AI-assisted)
  return InstructionARM64(Base(0b0100111010100000000111, 22), Rd(dst.id()), Rn(src.id()),
                          Rm(src.id()));
}

InstructionARM64 loadvf_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_reg_fpsimd.html
  // 128-bit variant
  // LDR <Qt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
  ASSERT(dst.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  // BUG (found this session, same two mistakes as `storevf_gpr64_plus_gpr64` above): (1) the
  // `option` field was hardcoded to UXTB (0b000) instead of UXTX (0b011), producing an
  // unallocated/illegal instruction, not just a wrong-but-valid one; (2) `Rm(addr1.id())` used
  // `addr1` a second time instead of `addr2` - the real second address register was never even
  // referenced, so even with the option field fixed this would have loaded from the wrong
  // address (addr1+addr1 instead of addr1+addr2) whenever it decoded at all.
  return InstructionARM64(Base(0b0011110011100000011010, 22), Rt(dst.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 loadvf_gpr64_plus_gpr64_plus_s8(Register dst,
                                                 Register addr1,
                                                 Register addr2,
                                                 s64 offset) {
  // BUG (found this session): this used to emit a single post-index `LDR <Qt>, [<Xn|SP>], #<simm>`
  // using ONLY addr1 - completely ignoring addr2 (never combined into the address at all, despite
  // being asserted non-null/distinct/non-SP) and using the offset as a POST-increment side effect
  // on addr1 rather than as part of the actual load address. This function needs to add THREE
  // things together (addr1 + addr2 + offset), and real ARM64 has no addressing mode that does
  // that in one instruction for a 128-bit SIMD load - same "establish base+index+offset in X16
  // first" two-step approach its siblings loadvf_gpr64_plus_gpr64_plus_s32()/
  // storevf_gpr64_plus_gpr64_plus_s8()/storevf_gpr64_plus_gpr64_plus_s32() (immediately below)
  // already correctly use is needed here too - this one function was just missing it. Confirmed as
  // the root cause of a real SIGSEGV on hardware: `IR_LoadConstOffset::do_codegen_arm64()`'s
  // INT_128/VECTOR_FLOAT branch -> `load_goal_xmm128()` -> here (whenever a 128-bit GOAL field read
  // has a nonzero offset fitting in 8 bits) never added the heap-base register (x22) at all,
  // dereferencing a raw GOAL heap-offset as if it were a real host pointer - crashed inside
  // `set-display` (`goal_src/jak1/engine/gfx/hw/display.gc`) reading `(-> disp gif-tag0 qword)`.
  // See the development notes.
  ASSERT(dst.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  // first establish the base+index+offset value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_fpsimd.html
  // 128-bit variant
  // LDR <Qt>, [<Xn|SP>], #<simm>
  instrs.emplace_back(
      InstructionARM64(Base(0b0011110011000000000001, 22), Rt(dst.id()), Rn(X16), Imm9s(0)));
  return InstructionARM64(instrs);
}

InstructionARM64 loadvf_gpr64_plus_gpr64_plus_s32(Register dst,
                                                  Register addr1,
                                                  Register addr2,
                                                  s64 offset) {
  ASSERT(dst.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_fpsimd.html
  // 128-bit variant
  // LDR <Qt>, [<Xn|SP>], #<simm>
  instrs.emplace_back(
      InstructionARM64(Base(0b0011110011000000000001, 22), Rt(dst.id()), Rn(X16), Imm9s(0)));
  return InstructionARM64(instrs);
}

InstructionARM64 storevf_gpr64_plus_gpr64(Register value, Register addr1, Register addr2) {
  ASSERT(value.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_reg_fpsimd.html
  // STR <Qt>, [<Xn|SP>, (<Wm>|<Xm>){, <extend> {<amount>}}]
  // BUG (found this session): the `option` field (bits [15:13], which extend/shift to apply to
  // the index register) was hardcoded to 0b000 = UXTB (extend from a byte) instead of 0b011 =
  // UXTX (the correct option for a plain 64-bit register offset, a no-op "extend" - i.e. just add
  // the full register) - same class of bug as `cmp_gpr64_gpr64`'s option field earlier this
  // session, confirmed the same way (cross-checked the exact instruction word against a real
  // `str q0, [x0, x1]` assembled by the NDK's own `clang`). This produced a genuinely unallocated/
  // illegal ARM64 instruction (not just a wrong-but-valid one) - confirmed on real hardware via a
  // SIGILL crashing inside `matrix-identity!` (`goal_src/jak1/engine/math/matrix.gc`), the first
  // GOAL function this whole project's history to store a 128-bit zero constant at a zero field
  // offset (`IR_StoreConstOffset::do_codegen_arm64` -> `store_goal_vf` -> here, whenever `offset
  // == 0`).
  return InstructionARM64(Base(0b0011110010100000011010, 22), Rt(value.id()), Rn(addr1.id()),
                          Rm(addr2.id()));
}

InstructionARM64 storevf_gpr64_plus_gpr64_plus_s8(Register value,
                                                  Register addr1,
                                                  Register addr2,
                                                  s64 offset) {
  ASSERT(value.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT8_MIN && offset <= INT8_MAX);
  // first establish the base+index+offset value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_fpsimd.html
  // STR <Qt>, [<Xn|SP>], #<simm>
  instrs.emplace_back(
      InstructionARM64(Base(0b0011110010000000000001, 22), Rt(value.id()), Rn(X16), Imm9s(0)));
  return InstructionARM64(instrs);
}

InstructionARM64 storevf_gpr64_plus_gpr64_plus_s32(Register value,
                                                   Register addr1,
                                                   Register addr2,
                                                   s64 offset) {
  ASSERT(value.is_128bit_simd(instr_set));
  ASSERT(addr1.is_gpr(instr_set));
  ASSERT(addr2.is_gpr(instr_set));
  ASSERT(addr1 != addr2);
  ASSERT(addr1 != SP);
  ASSERT(addr2 != SP);
  ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
  // first establish the base+index+offset value in x16
  std::vector<InstructionARM64> instrs = {
      // https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_shift.html
      // ADD <Xd>, <Xn>, <Xm>{, <shift> #<amount>}
      InstructionARM64(Base(0b10001011000, 11), Rd(X16), Imm6(0), Rn(addr1.id()), Rm(addr2.id())),
  };
  if (offset < 0) {
    // we'll subtract instead
    offset = std::abs(offset);
    const auto sub_instrs = construct_multiple_imm12_subs(offset, X16);
    instrs.insert(instrs.end(), sub_instrs.begin(), sub_instrs.end());
  } else {
    const auto add_instrs = construct_multiple_imm12_adds(offset, X16);
    instrs.insert(instrs.end(), add_instrs.begin(), add_instrs.end());
  }
  // https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_fpsimd.html
  // STR <Qt>, [<Xn|SP>], #<simm>
  instrs.emplace_back(
      InstructionARM64(Base(0b0011110010000000000001, 22), Rt(value.id()), Rn(X16), Imm9s(0)));
  return InstructionARM64(instrs);
}

InstructionARM64 loadvf_rip_plus_s32(Register dest, s64 offset) {
  ASSERT(dest.is_128bit_simd(instr_set));
  ASSERT_MSG(offset != 0,
             "PC Relative offset isn't 0 at encoding time, actually encode it properly!");
  return InstructionARM64(
      {// https://www.scs.stanford.edu/~zyedidia/arm64/adrp.html
       // ADRP <Xd>, <label>
       InstructionARM64(Base(0b100100000000000000000000000, 27), Rd(X16), Immhi(0), Immlo(0)),
       // https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_fpsimd.html
       // LDR <Qt>, [<Xn|SP>{, #<pimm>}]
       InstructionARM64(Base(0b0011110111, 10), Imm12(offset), Rt(dest.id()), Rn(X16))});
}

InstructionARM64 blend_vf(Register dst, Register src1, Register src2, u8 mask) {
  // x86's BLENDPS-style select: for each of the 4 32-bit lanes, mask bit i chooses src2's lane
  // (bit set) or src1's lane (bit clear). Deliberately NOT a new SIMD encoding derivation (no
  // per-lane insert or mask-register select instruction has anything to cross-check against in
  // this file yet) - built entirely out of instructions already verified elsewhere here:
  // `keep_mask`/`select_mask` are compile-time-constant (known from `mask` right now, not
  // runtime-linker-patched like every other literal pool in this file) 16-byte all-1s/all-0s
  // per-lane literals, loaded via a real "LDR <Qt>, <label>" (same opc=10/128-bit convention as the
  // GPR-base-relative 128-bit load right above), then combined with the already-real
  // parallel_bitwise_and()/or(). V30/V31 are private emitter scratch, outside both normal
  // allocation and spill reloads. V14/V15 are spill temporaries and must not be overwritten
  // here: doing so corrupted Jak 2's matrix inverse under register pressure. (AI-assisted)
  ASSERT(dst.is_128bit_simd(instr_set));
  ASSERT(src1.is_128bit_simd(instr_set));
  ASSERT(src2.is_128bit_simd(instr_set));
  u32 keep_mask[4];
  u32 select_mask[4];
  for (int i = 0; i < 4; i++) {
    bool take_src2 = (mask & (1 << i)) != 0;
    keep_mask[i] = take_src2 ? 0 : 0xffffffff;
    select_mask[i] = take_src2 ? 0xffffffff : 0;
  }
  Register keep(V30, true);
  Register select(V31, true);
  InstructionARM64 skip = branch_over(4 + 32);  // jump over the 8-word (32-byte) literal pool
  InstructionARM64 keep_lo(keep_mask[0]);
  InstructionARM64 keep_lo2(keep_mask[1]);
  InstructionARM64 keep_hi(keep_mask[2]);
  InstructionARM64 keep_hi2(keep_mask[3]);
  InstructionARM64 sel_lo(select_mask[0]);
  InstructionARM64 sel_lo2(select_mask[1]);
  InstructionARM64 sel_hi(select_mask[2]);
  InstructionARM64 sel_hi2(select_mask[3]);
  // LDR <Qt>, <label> - PC-relative 128-bit literal load (opc=10, see load128_simd128_gpr64_s8()
  // just above for the same opc convention on the immediate-offset form of this instruction).
  // `keep_mask` sits 8 words behind this instruction (word 1, right after `skip` at word 0).
  InstructionARM64 ldr_keep(Base(0b10011100, 8), Imm19(static_cast<u32>(-8) & 0x7ffff),
                            Rt(keep.id()));
  // `select_mask` sits 5 words behind this instruction (word 5, right after the 4-word keep_mask).
  InstructionARM64 ldr_select(Base(0b10011100, 8), Imm19(static_cast<u32>(-5) & 0x7ffff),
                              Rt(select.id()));
  InstructionARM64 and_keep = parallel_bitwise_and(keep, src1, keep);
  InstructionARM64 and_select = parallel_bitwise_and(select, src2, select);
  InstructionARM64 combine = parallel_bitwise_or(dst, keep, select);
  return InstructionARM64(skip, keep_lo, keep_lo2, keep_hi, keep_hi2, sel_lo, sel_lo2, sel_hi,
                          sel_hi2, ldr_keep, ldr_select, and_keep, and_select, combine);
}

InstructionARM64 swizzle_vf(Register dst, Register src, u8 controlBytes) {
  // General x86 SHUFPS-style single-source 4-lane 32-bit shuffle: controlBytes packs 4x 2-bit
  // fields, one per destination lane, each selecting which of src's 4 lanes (0-3) to read - the
  // same encoding splat_vf()'s 4 callers already rely on (X/Y/Z/W = 0x00/0x55/0xAA/0xFF, i.e. "read
  // source lane N for every destination lane"), just fully general here. Reuses the same
  // TBL-with-a-compile-time-constant-index-literal approach vpshuflw()/vpshufhw() below use for
  // their one fixed case, generalized: unlike those two, controlBytes is genuinely used with many
  // different values throughout goal_src, so this computes the 16 index bytes from it directly
  // rather than special-casing one value. See tbl_16b()'s own comment for the encoding caveat this
  // shares with it (and with splat_vf()'s DUP) - no already-working sibling to cross-check against,
  // no hardware to verify on this session.
  ASSERT(dst.is_128bit_simd(instr_set));
  ASSERT(src.is_128bit_simd(instr_set));
  u32 idx_words[4];
  for (int lane = 0; lane < 4; lane++) {
    u32 src_lane = (controlBytes >> (2 * lane)) & 0b11;
    u32 b0 = src_lane * 4 + 0;
    u32 b1 = src_lane * 4 + 1;
    u32 b2 = src_lane * 4 + 2;
    u32 b3 = src_lane * 4 + 3;
    idx_words[lane] = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
  }
  Register idx_reg(V30, true);
  InstructionARM64 skip = branch_over(4 + 16);  // jump over the 4-word (16-byte) index literal
  InstructionARM64 idx0(idx_words[0]);
  InstructionARM64 idx1(idx_words[1]);
  InstructionARM64 idx2(idx_words[2]);
  InstructionARM64 idx3(idx_words[3]);
  // LDR <Qt>, <label> - see blend_vf()/vpshuflw() above for the same 128-bit literal load; index
  // literal is 4 words behind this instruction (right after `skip`).
  InstructionARM64 ldr_idx(Base(0b10011100, 8), Imm19(static_cast<u32>(-4) & 0x7ffff),
                           Rt(idx_reg.id()));
  InstructionARM64 tbl = tbl_16b(dst, src, idx_reg);
  return InstructionARM64(skip, idx0, idx1, idx2, idx3, ldr_idx, tbl);
}

InstructionARM64 shuffle_vf(Register dst, Register src, u8 dx, u8 dy, u8 dz, u8 dw) {
  ASSERT_MSG(false, "not yet implemented");
  return InstructionARM64(0b0);
}

InstructionARM64 splat_vf(Register dst, Register src, Register::VF_ELEMENT element) {
  // A broadcast can use one DUP rather than swizzle_vf's general table lookup:
  // DUP <Vd>.4S, <Vn>.S[index] copies one 32-bit lane of Vn to all 4 lanes of Vd in a single
  // instruction. https://www.scs.stanford.edu/~zyedidia/arm64/dup_advsimd_elt.html
  //
  // All four lane encodings are checked against the NDK assembler in the emitter tests.
  // (AI-assisted)
  int index;
  switch (element) {
    case Register::VF_ELEMENT::X:
      index = 0;
      break;
    case Register::VF_ELEMENT::Y:
      index = 1;
      break;
    case Register::VF_ELEMENT::Z:
      index = 2;
      break;
    case Register::VF_ELEMENT::W:
      index = 3;
      break;
    default:
      ASSERT(false);
      return {0};
  }
  ASSERT(dst.is_128bit_simd(instr_set));
  ASSERT(src.is_128bit_simd(instr_set));
  // imm5 for a 32-bit ("S") element: bit2 set selects word size, bits[4:3] carry the 2-bit index.
  u32 imm5 = (static_cast<u32>(index) << 3) | 0b00100;
  return InstructionARM64(Base(0b0100111000000000000001, 22), Imm5(imm5), Rn(src.id()),
                          Rd(dst.id()));
}

InstructionARM64 xor_vf(Register dst, Register src1, Register src2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/eor_advsimd.html
  // EOR <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0110111000100000000111, 22), Rn(src1.id()), Rm(src2.id()),
                          Rd(dst.id()));
}

InstructionARM64 sub_vf(Register dst, Register src1, Register src2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fsub_advsimd.html
  // FSUB <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  // 4 single precision floats
  return InstructionARM64(Base(0b0100111010100000110101, 22), Rn(src1.id()), Rm(src2.id()),
                          Rd(dst.id()));
}

InstructionARM64 add_vf(Register dst, Register src1, Register src2) {
  // FADD <Vd>.4S, <Vn>.4S, <Vm>.4S (not integer ADD). (AI-assisted)
  // 4 single precision floats
  return InstructionARM64(Base(0b0100111000100000110101, 22), Rn(src1.id()), Rm(src2.id()),
                          Rd(dst.id()));
}

InstructionARM64 mul_vf(Register dst, Register src1, Register src2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fmul_advsimd_vec.html
  // FMUL <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  // 4 single precision floats
  return InstructionARM64(Base(0b0110111000100000110111, 22), Rn(src1.id()), Rm(src2.id()),
                          Rd(dst.id()));
}

InstructionARM64 max_vf(Register dst, Register src1, Register src2) {
  // FMAX <Vd>.4S, <Vn>.4S, <Vm>.4S; no FEAT_FAMINMAX requirement. (AI-assisted)
  // 4 single precision floats
  return InstructionARM64(Base(0b0100111000100000111101, 22), Rn(src1.id()), Rm(src2.id()),
                          Rd(dst.id()));
}

InstructionARM64 min_vf(Register dst, Register src1, Register src2) {
  // FMIN <Vd>.4S, <Vn>.4S, <Vm>.4S; no FEAT_FAMINMAX requirement. (AI-assisted)
  // 4 single precision floats
  return InstructionARM64(Base(0b0100111010100000111101, 22), Rn(src1.id()), Rm(src2.id()),
                          Rd(dst.id()));
}

InstructionARM64 div_vf(Register dst, Register src1, Register src2) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fdiv_advsimd.html
  // FDIV <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  // 4 single precision floats
  return InstructionARM64(Base(0b0110111000100000111111, 22), Rn(src1.id()), Rm(src2.id()),
                          Rd(dst.id()));
}

InstructionARM64 sqrt_vf(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fsqrt_advsimd.html
  // FSQRT <Vd>.<T>, <Vn>.<T>
  // 4 single precision floats
  return InstructionARM64(Base(0b0110111010100001111110, 22), Rn(src.id()), Rd(dst.id()));
}

InstructionARM64 itof_vf(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/scvtf_advsimd_int.html
  // SCVTF <Vd>.<T>, <Vn>.<T>
  // s32 int -> 4 single precision floats
  return InstructionARM64(Base(0b0100111000100001110110, 22), Rn(src.id()), Rd(dst.id()));
}

InstructionARM64 ftoi_vf(Register dst, Register src) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/fcvtzs_advsimd_int.html
  // FCVTZS <Vd>.<T>, <Vn>.<T>
  // 4 single precision floats -> s32 ints
  // TODO - double check rounding mode
  return InstructionARM64(Base(0b0100111010100001101110, 22), Rn(src.id()), Rd(dst.id()));
}

// TODO - rename these instructions

// The "Advanced SIMD shift by immediate" class encodes the shift amount across TWO fields -
// immh (4 bits, also selects the element size: 0001=8-bit, 001x=16-bit, 01xx=32-bit, 1xxx=64-bit)
// and immb (the low 3 bits) - as a single combined 7-bit value, not immb alone. The five functions
// below originally baked a fixed immh into their Base() literal and passed the raw shift count
// through Immb() by itself (a 3-bit field) - correct only by accident for shift amounts small
// enough that immb's 3 bits didn't need to borrow from immh at all, and asserting outright (a real,
// previously-undiscovered bug, not a missing stub) the moment real GOAL code needed a bigger shift.
// Real formula: for a right shift (SSHR/USHR) on `esize`-bit elements, combined = 2*esize - shift
// (shift must be in [1, esize] - there's no valid immediate encoding for a right-shift-by-0, unlike
// x86); for a left shift (SHL), combined = esize + shift (shift in [0, esize-1]). immh = combined
// >> 3, immb = combined & 7.

// - arithmetic_shift_right_32bit_vf
InstructionARM64 pw_sra(Register dst, Register src, u8 imm) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/sshr_advsimd.html
  // - vector, 4S
  // SSHR <Vd>.<T>, <Vn>.<T>, #<shift>
  ASSERT(imm >= 1 && imm <= 32);
  u32 combined = 64 - imm;
  return InstructionARM64(Base(0b0100111100000000000001, 22), Immh(combined >> 3),
                          Immb(combined & 0b111), Rn(src.id()), Rd(dst.id()));
}

// - logical_shift_right_32bit_vf
InstructionARM64 pw_srl(Register dst, Register src, u8 imm) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ushr_advsimd.html
  // - vector, 4S
  // USHR <Vd>.<T>, <Vn>.<T>, #<shift>
  ASSERT(imm >= 1 && imm <= 32);
  u32 combined = 64 - imm;
  return InstructionARM64(Base(0b0110111100000000000001, 22), Immh(combined >> 3),
                          Immb(combined & 0b111), Rn(src.id()), Rd(dst.id()));
}

// - logical_shift_left_32bit_vf
InstructionARM64 pw_sll(Register dst, Register src, u8 imm) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/shl_advsimd.html
  // - vector, 4S
  // SHL <Vd>.<T>, <Vn>.<T>, #<shift>
  ASSERT(imm <= 31);
  u32 combined = 32 + imm;
  return InstructionARM64(Base(0b0100111100000000010101, 22), Immh(combined >> 3),
                          Immb(combined & 0b111), Rn(src.id()), Rd(dst.id()));
}

// - logical_shift_right_16bit_vf
InstructionARM64 ph_srl(Register dst, Register src, u8 imm) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ushr_advsimd.html
  // - vector, 8H
  // USHR <Vd>.<T>, <Vn>.<T>, #<shift>
  ASSERT(imm >= 1 && imm <= 16);
  u32 combined = 32 - imm;
  return InstructionARM64(Base(0b0110111100000000000001, 22), Immh(combined >> 3),
                          Immb(combined & 0b111), Rn(src.id()), Rd(dst.id()));
}

// - logical_shift_left_16bit_vf
InstructionARM64 ph_sll(Register dst, Register src, u8 imm) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/shl_advsimd.html
  // - vector, 8H
  // SHL <Vd>.<T>, <Vn>.<T>, #<shift>
  ASSERT(imm <= 15);
  u32 combined = 16 + imm;
  return InstructionARM64(Base(0b0100111100000000010101, 22), Immh(combined >> 3),
                          Immb(combined & 0b111), Rn(src.id()), Rd(dst.id()));
}

InstructionARM64 parallel_add_byte(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/add_advsimd.html
  // - vector, 16B
  // ADD <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0100111000100000100001, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 parallel_bitwise_or(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/orr_advsimd_reg.html
  // - vector, 16B
  // ORR <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0100111010100000000111, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 parallel_bitwise_xor(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/eor_advsimd.html
  // - vector, 16B
  // EOR <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0110111000100000000111, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 parallel_bitwise_and(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/and_advsimd.html
  // - vector, 16B
  // AND <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0100111000100000000111, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 pextub_swapped(Register dst, Register src0, Register src1) {
  // Interleave the upper halves, not odd elements. ZIP2 Vd.16B, Vn.16B, Vm.16B. (AI-assisted)
  return InstructionARM64(Base(0b0100111000000000011110, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 pextuh_swapped(Register dst, Register src0, Register src1) {
  // ZIP2 Vd.8H, Vn.8H, Vm.8H. (AI-assisted)
  return InstructionARM64(Base(0b0100111001000000011110, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 pextuw_swapped(Register dst, Register src0, Register src1) {
  // ZIP2 Vd.4S, Vn.4S, Vm.4S. (AI-assisted)
  return InstructionARM64(Base(0b0100111010000000011110, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 pextlb_swapped(Register dst, Register src0, Register src1) {
  // Interleave the lower halves, not even elements. ZIP1 Vd.16B, Vn.16B, Vm.16B. (AI-assisted)
  return InstructionARM64(Base(0b0100111000000000001110, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 pextlh_swapped(Register dst, Register src0, Register src1) {
  // ZIP1 Vd.8H, Vn.8H, Vm.8H. (AI-assisted)
  return InstructionARM64(Base(0b0100111001000000001110, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 pextlw_swapped(Register dst, Register src0, Register src1) {
  // ZIP1 Vd.4S, Vn.4S, Vm.4S. (AI-assisted)
  return InstructionARM64(Base(0b0100111010000000001110, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 parallel_compare_e_b(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/cmeq_advsimd_reg.html
  // - vector, 16B
  // CMEQ <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0110111000100000100011, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 parallel_compare_e_h(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/cmeq_advsimd_reg.html
  // - vector, 8H
  // CMEQ <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0110111001100000100011, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 parallel_compare_e_w(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/cmeq_advsimd_reg.html
  // - vector, 4S
  // CMEQ <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0110111010100000100011, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 parallel_compare_gt_b(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/cmgt_advsimd_reg.html
  // - vector, 16B
  // CMGT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0100111000100000001101, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 parallel_compare_gt_h(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/cmgt_advsimd_reg.html
  // - vector, 8H
  // CMGT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0100111001100000001101, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 parallel_compare_gt_w(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/cmgt_advsimd_reg.html
  // - vector, 4S
  // CMGT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0100111010100000001101, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

// TODO - rename this monstrosity from x86

InstructionARM64 vpunpcklqdq(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/zip1_advsimd.html
  // - vector, 2D
  // ZIP1 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0100111011000000001110, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

InstructionARM64 pcpyld_swapped(Register dst, Register src0, Register src1) {
  return vpunpcklqdq(dst, src0, src1);
}

InstructionARM64 pcpyud(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/zip2_advsimd.html
  // - vector, 2D
  // ZIP2 <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0100111011000000011110, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

// TODO - more x86 rename candidates

// lane-wise-32bit-substraction
InstructionARM64 vpsubd(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/sub_advsimd.html
  // - vector, 4S
  // SUB <Vd>.<T>, <Vn>.<T>, <Vm>.<T>
  return InstructionARM64(Base(0b0110111010100000100001, 22), Rn(src0.id()), Rm(src1.id()),
                          Rd(dst.id()));
}

// shift-right-logical-entire-simd-reg
InstructionARM64 vpsrldq(Register dst, Register src, u8 imm) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ext_advsimd.html
  // - 16B
  // EXT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>, #<index>
  return InstructionARM64(Base(0b0110111000000000000000, 22), Rn(src.id()), Rm(src.id()),
                          Rd(dst.id()), Imm4(imm));
}

// shift-left-logical-entire-simd-reg
InstructionARM64 vpslldq(Register dst, Register src, u8 imm) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/ext_advsimd.html
  // - 16B
  // EXT <Vd>.<T>, <Vn>.<T>, <Vm>.<T>, #<index>
  return InstructionARM64(Base(0b0110111000000000000000, 22), Rn(src.id()), Rm(src.id()),
                          Rd(dst.id()), Imm4((16 - imm) & 0xF));
}

// TBL <Vd>.16B, {<Vn>.16B}, <Vm>.16B - single-register byte-table lookup: each of Vd's 16 bytes is
// Vn's byte at the index Vm's corresponding byte names (0-15; out-of-range indices zero that byte,
// per TBL - as opposed to TBX, which would leave it unchanged - not needed by either caller below).
// https://www.scs.stanford.edu/~zyedidia/arm64/tbl_advsimd_sngl.html
//
// CAUTION - like splat_vf()'s DUP, this was NOT cross-checked bit-for-bit against an
// already-implemented sibling in the same "Advanced SIMD table lookup" family (none exists here
// yet) - derived from recalled ISA structure, no live reference or hardware to verify against this
// session.
InstructionARM64 tbl_16b(Register dst, Register src, Register index) {
  return InstructionARM64(Base(0b0100111000000000000000, 22), Rm(index.id()), Rn(src.id()),
                          Rd(dst.id()));
}

// Only ever called with imm == 0x88 in this whole codebase
// (compile_asm_ppach()/compile_asm_ppacb(), Compiler/compilation/Asm.cpp) - a fixed "[lane0, lane2,
// lane0, lane2]" pattern applied to the low (vpshuflw) or high (vpshufhw) 4 halfwords, leaving the
// other 4 halfwords untouched. Rather than derive a general PSHUFLW/PSHUFHW-from-imm8 decoder for a
// case with a single real caller, hardcode the one real byte-permutation as a TBL index literal -
// the general form remains unimplemented.
InstructionARM64 vpshuflw(Register dst, Register src, u8 imm) {
  ASSERT_MSG(imm == 0x88,
             "vpshuflw is only implemented for imm=0x88, the only value used anywhere in goal_src");
  ASSERT(dst.is_128bit_simd(instr_set));
  ASSERT(src.is_128bit_simd(instr_set));
  // low halfwords -> [h0, h2, h0, h2]; high halfwords unchanged.
  Register idx_reg(V30, true);
  InstructionARM64 skip = branch_over(4 + 16);  // jump over the 4-word (16-byte) index literal
  InstructionARM64 idx0(0x05040100);            // bytes 0,1,4,5
  InstructionARM64 idx1(0x05040100);            // bytes 0,1,4,5
  InstructionARM64 idx2(0x0b0a0908);            // bytes 8,9,10,11 (unchanged)
  InstructionARM64 idx3(0x0f0e0d0c);            // bytes 12,13,14,15 (unchanged)
  // LDR <Qt>, <label> - same 128-bit literal load as blend_vf() uses; index literal is 4 words
  // behind this instruction (right after `skip`).
  InstructionARM64 ldr_idx(Base(0b10011100, 8), Imm19(static_cast<u32>(-4) & 0x7ffff),
                           Rt(idx_reg.id()));
  InstructionARM64 tbl = tbl_16b(dst, src, idx_reg);
  return InstructionARM64(skip, idx0, idx1, idx2, idx3, ldr_idx, tbl);
}

InstructionARM64 vpshufhw(Register dst, Register src, u8 imm) {
  ASSERT_MSG(imm == 0x88,
             "vpshufhw is only implemented for imm=0x88, the only value used anywhere in goal_src");
  ASSERT(dst.is_128bit_simd(instr_set));
  ASSERT(src.is_128bit_simd(instr_set));
  // high halfwords -> [h4, h6, h4, h6]; low halfwords unchanged.
  Register idx_reg(V30, true);
  InstructionARM64 skip = branch_over(4 + 16);
  InstructionARM64 idx0(0x03020100);  // bytes 0,1,2,3 (unchanged)
  InstructionARM64 idx1(0x07060504);  // bytes 4,5,6,7 (unchanged)
  InstructionARM64 idx2(0x0d0c0908);  // bytes 8,9,12,13
  InstructionARM64 idx3(0x0d0c0908);  // bytes 8,9,12,13
  InstructionARM64 ldr_idx(Base(0b10011100, 8), Imm19(static_cast<u32>(-4) & 0x7ffff),
                           Rt(idx_reg.id()));
  InstructionARM64 tbl = tbl_16b(dst, src, idx_reg);
  return InstructionARM64(skip, idx0, idx1, idx2, idx3, ldr_idx, tbl);
}

InstructionARM64 vpackuswb(Register dst, Register src0, Register src1) {
  // https://www.scs.stanford.edu/~zyedidia/arm64/sqxtun_advsimd.html
  // SQXTUN{2} <Vd>.<Tb>, <Vn>.<Ta>
  return InstructionARM64({
      // sqxtun  vDst.8b,  vSrc0.8h
      InstructionARM64(Base(0b0010111000100001001010, 22), Rn(src0.id()), Rd(dst.id())),
      // sqxtun2 vDst.16b, vSrc1.8h
      InstructionARM64(Base(0b0110111000100001001010, 22), Rn(src1.id()), Rd(dst.id())),
  });
}
}  // namespace ARM64
}  // namespace IGen
}  // namespace emitter
