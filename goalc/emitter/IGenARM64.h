#pragma once

#include "goalc/emitter/Instruction.h"
#include "goalc/emitter/Register.h"

namespace emitter {
namespace IGen {
namespace ARM64 {
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   MOVES
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
/*!
 * Move data from src to dst. Moves all 64-bits of the GPR.
 */
InstructionARM64 mov_gpr64_gpr64(Register dst, Register src);

/*!
 * Move a 64-bit constant into a register.
 */
InstructionARM64 mov_gpr64_u64(Register dst, uint64_t val);

/*!
 * Move a 32-bit constant into a register. Zeros the upper 32 bits.
 */
InstructionARM64 mov_gpr64_u32(Register dst, uint64_t val);

/*!
 * Move a signed 32-bit constant into a register. Sign extends for the upper 32 bits.
 * When possible prefer mov_gpr64_u32. (use this only for negative values...)
 * This is always bigger than mov_gpr64_u32, but smaller than a mov_gpr_u64.
 */
InstructionARM64 mov_gpr64_s32(Register dst, int64_t val);

/*!
 * Move 32-bits of xmm to 32 bits of gpr (no sign extension).
 */
InstructionARM64 movd_gpr32_f32(Register dst, Register src);

/*!
 * Move 32-bits of gpr to 32-bits of xmm (no sign extension)
 */
InstructionARM64 movd_f32_gpr32(Register dst, Register src);

/*!
 * Move 64-bits of xmm to 64 bits of gpr (no sign extension).
 */
InstructionARM64 movq_gpr64_f64(Register dst, Register src);

/*!
 * Move 64-bits of gpr to 64-bits of xmm (no sign extension)
 */
InstructionARM64 movq_f64_gpr64(Register dst, Register src);

/*!
 * Move 32-bits between xmm's
 */
InstructionARM64 mov_f32_f32(Register dst, Register src);

// todo - GPR64 -> XMM64 (zext)
// todo - XMM -> GPR64

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   GOAL Loads and Stores
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

/*!
 * movsx dst, BYTE PTR [addr1 + addr2]
 * addr1 and addr2 have to be different registers.
 * Cannot use rsp.
 */
InstructionARM64 load8s_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2);

InstructionARM64 store8_gpr64_gpr64_plus_gpr64(Register addr1, Register addr2, Register value);

InstructionARM64 load8s_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                       Register addr1,
                                                       Register addr2,
                                                       s64 offset);

InstructionARM64 store8_gpr64_gpr64_plus_gpr64_plus_s8(Register addr1,
                                                       Register addr2,
                                                       Register value,
                                                       s64 offset);

InstructionARM64 load8s_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset);

InstructionARM64 store8_gpr64_gpr64_plus_gpr64_plus_s32(Register addr1,
                                                        Register addr2,
                                                        Register value,
                                                        s64 offset);

/*!
 * movzx dst, BYTE PTR [addr1 + addr2]
 * addr1 and addr2 have to be different registers.
 * Cannot use rsp.
 */
InstructionARM64 load8u_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2);

InstructionARM64 load8u_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                       Register addr1,
                                                       Register addr2,
                                                       s64 offset);

InstructionARM64 load8u_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset);

/*!
 * movsx dst, WORD PTR [addr1 + addr2]
 * addr1 and addr2 have to be different registers.
 * Cannot use rsp.
 */
InstructionARM64 load16s_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2);

InstructionARM64 store16_gpr64_gpr64_plus_gpr64(Register addr1, Register addr2, Register value);

InstructionARM64 store16_gpr64_gpr64_plus_gpr64_plus_s8(Register addr1,
                                                        Register addr2,
                                                        Register value,
                                                        s64 offset);

InstructionARM64 store16_gpr64_gpr64_plus_gpr64_plus_s32(Register addr1,
                                                         Register addr2,
                                                         Register value,
                                                         s64 offset);

InstructionARM64 load16s_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset);

InstructionARM64 load16s_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                         Register addr1,
                                                         Register addr2,
                                                         s64 offset);

/*!
 * movzx dst, WORD PTR [addr1 + addr2]
 * addr1 and addr2 have to be different registers.
 * Cannot use rsp.
 */
InstructionARM64 load16u_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2);

InstructionARM64 load16u_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset);

InstructionARM64 load16u_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                         Register addr1,
                                                         Register addr2,
                                                         s64 offset);

/*!
 * movsxd dst, DWORD PTR [addr1 + addr2]
 * addr1 and addr2 have to be different registers.
 * Cannot use rsp.
 */
InstructionARM64 load32s_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2);

InstructionARM64 store32_gpr64_gpr64_plus_gpr64(Register addr1, Register addr2, Register value);

InstructionARM64 load32s_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset);

InstructionARM64 store32_gpr64_gpr64_plus_gpr64_plus_s8(Register addr1,
                                                        Register addr2,
                                                        Register value,
                                                        s64 offset);

InstructionARM64 load32s_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                         Register addr1,
                                                         Register addr2,
                                                         s64 offset);

InstructionARM64 store32_gpr64_gpr64_plus_gpr64_plus_s32(Register addr1,
                                                         Register addr2,
                                                         Register value,
                                                         s64 offset);

/*!
 * movzxd dst, DWORD PTR [addr1 + addr2]
 * addr1 and addr2 have to be different registers.
 * Cannot use rsp.
 */
InstructionARM64 load32u_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2);

InstructionARM64 load32u_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset);

InstructionARM64 load32u_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                         Register addr1,
                                                         Register addr2,
                                                         s64 offset);

/*!
 * mov dst, QWORD PTR [addr1 + addr2]
 * addr1 and addr2 have to be different registers.
 * Cannot use rsp.
 */
InstructionARM64 load64_gpr64_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2);

InstructionARM64 store64_gpr64_gpr64_plus_gpr64(Register addr1, Register addr2, Register value);

InstructionARM64 load64_gpr64_gpr64_plus_gpr64_plus_s8(Register dst,
                                                       Register addr1,
                                                       Register addr2,
                                                       s64 offset);

InstructionARM64 store64_gpr64_gpr64_plus_gpr64_plus_s8(Register addr1,
                                                        Register addr2,
                                                        Register value,
                                                        s64 offset);

InstructionARM64 load64_gpr64_gpr64_plus_gpr64_plus_s32(Register dst,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset);

InstructionARM64 store64_gpr64_gpr64_plus_gpr64_plus_s32(Register addr1,
                                                         Register addr2,
                                                         Register value,
                                                         s64 offset);

InstructionARM64 store_goal_vf(Register addr, Register value, Register off, s64 offset);

InstructionARM64 store_goal_gpr(Register addr, Register value, Register off, int offset, int size);

InstructionARM64 load_goal_xmm128(Register dst, Register addr, Register off, int offset);

/*!
 * Load memory at addr + offset, where addr is a GOAL pointer and off is the offset register.
 * This will pick the appropriate fancy addressing mode instruction.
 */
InstructionARM64 load_goal_gpr(Register dst,
                               Register addr,
                               Register off,
                               int offset,
                               int size,
                               bool sign_extend);

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   LOADS n' STORES - XMM32
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
InstructionARM64 store32_xmm32_gpr64_plus_gpr64(Register addr1, Register addr2, Register xmm_value);

InstructionARM64 load32_xmm32_gpr64_plus_gpr64(Register simd_dest, Register addr1, Register addr2);

InstructionARM64 store32_xmm32_gpr64_plus_gpr64_plus_s8(Register addr1,
                                                        Register addr2,
                                                        Register xmm_value,
                                                        s64 offset);

InstructionARM64 load32_xmm32_gpr64_plus_gpr64_plus_s8(Register simd_dest,
                                                       Register addr1,
                                                       Register addr2,
                                                       s64 offset);

InstructionARM64 store32_xmm32_gpr64_plus_gpr64_plus_s32(Register addr1,
                                                         Register addr2,
                                                         Register xmm_value,
                                                         s64 offset);

InstructionARM64 lea_reg_plus_off32(Register dest, Register base, s64 offset);

InstructionARM64 lea_reg_plus_off8(Register dest, Register base, s64 offset);

InstructionARM64 lea_reg_plus_off(Register dest, Register base, s64 offset);

InstructionARM64 store32_xmm32_gpr64_plus_s32(Register base, Register xmm_value, s64 offset);

InstructionARM64 store32_xmm32_gpr64_plus_s8(Register base, Register xmm_value, s64 offset);

InstructionARM64 load32_xmm32_gpr64_plus_gpr64_plus_s32(Register simd_dest,
                                                        Register addr1,
                                                        Register addr2,
                                                        s64 offset);

InstructionARM64 load32_xmm32_gpr64_plus_s32(Register simd_dest, Register base, s64 offset);

InstructionARM64 load32_xmm32_gpr64_plus_s8(Register simd_dest, Register base, s64 offset);

InstructionARM64 load_goal_xmm32(Register simd_dest, Register addr, Register off, s64 offset);

InstructionARM64 store_goal_xmm32(Register addr, Register xmm_value, Register off, s64 offset);

InstructionARM64 store_reg_offset_xmm32(Register base, Register xmm_value, s64 offset);

InstructionARM64 load_reg_offset_xmm32(Register simd_dest, Register base, s64 offset);

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   LOADS n' STORES - XMM128
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

/*!
 * Store a 128-bit xmm into an address stored in a register, no offset
 */
InstructionARM64 store128_gpr64_simd128(Register gpr_addr, Register xmm_value);

InstructionARM64 store128_gpr64_simd128_s32(Register gpr_addr, Register xmm_value, s64 offset);

InstructionARM64 store128_gpr64_simd128_s8(Register gpr_addr, Register xmm_value, s64 offset);

InstructionARM64 load128_simd128_gpr64(Register simd_dest, Register gpr_addr);

InstructionARM64 load128_simd128_gpr64_s32(Register simd_dest, Register gpr_addr, s64 offset);

InstructionARM64 load128_simd128_gpr64_s8(Register simd_dest, Register gpr_addr, s64 offset);

InstructionARM64 load128_xmm128_reg_offset(Register simd_dest, Register base, s64 offset);

InstructionARM64 store128_xmm128_reg_offset(Register base, Register xmm_val, s64 offset);

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   RIP loads and stores
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

InstructionARM64 load64_pcRel_s32(Register dest, s64 offset);

InstructionARM64 load32s_pcRel_s32(Register dest, s64 offset);

InstructionARM64 load32u_pcRel_s32(Register dest, s64 offset);

InstructionARM64 load16u_pcRel_s32(Register dest, s64 offset);

InstructionARM64 load16s_pcRel_s32(Register dest, s64 offset);

InstructionARM64 load8u_pcRel_s32(Register dest, s64 offset);

InstructionARM64 load8s_pcRel_s32(Register dest, s64 offset);

InstructionARM64 static_load(Register dest, s64 offset, int size, bool sign_extend);

InstructionARM64 store64_pcRel_s32(Register src, s64 offset);

InstructionARM64 store32_pcRel_s32(Register src, s64 offset);

InstructionARM64 store16_pcRel_s32(Register src, s64 offset);

InstructionARM64 store8_pcRel_s32(Register src, s64 offset);

InstructionARM64 static_store(Register value, s64 offset, int size);

// Loads the real absolute address of something in a different segment into `dest`, resolved by
// the runtime linker (same cross-segment link mechanism x86's RIP-relative addressing uses, just
// a wider literal instead of bits packed into an instruction) - see the implementation for the
// full writeup of why this exists instead of a "proper" ADRP-based PC-relative load.
InstructionARM64 load_cross_seg_addr(Register dest);

InstructionARM64 static_addr(Register dst, s64 offset);

InstructionARM64 static_load_f32(Register simd_dest, s64 offset);

InstructionARM64 static_store_f32(Register xmm_value, s64 offset);

// Computes `dest = st_reg + <a 32-bit value the runtime symbol linker patches in>` - used for
// IR_LoadSymbolPointer's "arbitrary named symbol" case (goalc/compiler/IR.cpp), where the actual
// value (either an offset from the symbol table, or - if the placeholder below was left as -1 - an
// absolute address; see symlink_v3() in game/kernel/*/klink.cpp, unchanged by this) can't be known
// until the object file is loaded. x86's equivalent (`lea_reg_plus_off32`, called with a
// placeholder immediate patched by ObjectGenerator::link_instruction_symbol_ptr()) just writes a
// real disp32 into a `add` instruction's own encoding - not possible here, since ARM64's ADD-
// immediate only has a 12-bit field. Same "give the runtime linker a real, plain N-byte literal to
// overwrite, and load it with a real instruction" approach as load_cross_seg_addr() above, reusing
// the existing symlink_v3() patcher exactly as-is via `link_instruction_symbol_mem()` (registered
// through the disp mechanism - see with_disp() on InstructionARM64) instead of teaching it a new,
// ARM64-specific patch shape.
InstructionARM64 load_symbol_ptr_offset(Register dest, Register st_reg);

// Same idea as load_symbol_ptr_offset() above, but for IR_GetSymbolValue: also adds `offset_reg`
// (x86's second base register for this access) and performs an actual 32-bit memory load (sign- or
// zero-extended per `sign_extend`) from the resulting address into `dest`, rather than just
// computing a pointer. `scratch` must be a register distinct from `dest`/`st_reg`/`offset_reg` -
// see IR_GetSymbolValue::to_rai() (goalc/compiler/IR.cpp) for where it comes from. `link_placeholder`
// must be passed through unchanged from whatever the caller would have given
// link_instruction_symbol_mem()'s x86 equivalent (e.g. LINK_SYM_NO_OFFSET_FLAG) - see the .cpp for
// why this isn't just an arbitrary sentinel.
InstructionARM64 load_symbol_value(Register dest,
                                   Register st_reg,
                                   Register offset_reg,
                                   Register scratch,
                                   u32 link_placeholder,
                                   bool sign_extend);

// Store counterpart of load_symbol_value() above, for IR_SetSymbolValue - writes `value`'s low 32
// bits to `[st_reg + offset_reg + <runtime-patched literal>]`. Since there's no destination
// register to double as the address accumulator here (unlike load_symbol_value(), this is a pure
// store), `scratch` alone builds the whole address, one add at a time: loaded with the patched
// literal first, then st_reg, then offset_reg added in - no second temporary needed.
InstructionARM64 store_symbol_value(Register st_reg,
                                    Register offset_reg,
                                    Register value,
                                    Register scratch,
                                    u32 link_placeholder);

// TODO, special load/stores of 128 bit values.

// TODO, consider specialized stack loads and stores?
InstructionARM64 load64_gpr64_plus_s32(Register dst_reg, int32_t offset, Register src_reg);

/*!
 * Store 64-bits from gpr into memory located at 64-bit reg + 32-bit signed offset.
 */
InstructionARM64 store64_gpr64_plus_s32(Register addr, int32_t offset, Register value);

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   FUNCTION STUFF
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
/*!
 * Function return. Pops the 64-bit return address (real) off the stack and jumps to it.
 */
InstructionARM64 ret();

/*!
 * Instruction to push gpr (64-bits) onto the stack
 */
InstructionARM64 push_gpr64(Register reg);

/*!
 * Instruction to pop 64 bit gpr from the stack
 */
InstructionARM64 pop_gpr64(Register reg);

/*!
 * Call a function stored in a 64-bit gpr
 */
InstructionARM64 call_r64(Register reg_);

/*!
 * Jump to an x86-64 address stored in a 64-bit gpr.
 */
InstructionARM64 jmp_r64(Register reg_);

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   INTEGER MATH
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
InstructionARM64 sub_gpr64_imm8s(Register reg, int64_t imm);

InstructionARM64 sub_gpr64_imm32s(Register reg, int64_t imm);

InstructionARM64 add_gpr64_imm8s(Register reg, int64_t v);

InstructionARM64 add_gpr64_imm32s(Register reg, int64_t v);

InstructionARM64 add_gpr64_imm(Register reg, int64_t imm);

InstructionARM64 sub_gpr64_imm(Register reg, int64_t imm);

InstructionARM64 add_gpr64_gpr64(Register dst, Register src);

InstructionARM64 sub_gpr64_gpr64(Register dst, Register src);

/*!
 * Multiply gprs (32-bit, signed).
 * (Note - probably worth doing imul on gpr64's to implement the EE's unsigned multiply)
 */
InstructionARM64 imul_gpr32_gpr32(Register dst, Register src);

/*!
 * Multiply gprs (64-bit, signed).
 * DANGER - this treats all operands as 64-bit. This is not like the EE.
 */
InstructionARM64 imul_gpr64_gpr64(Register dst, Register src);

/*!
 * Divide (idiv, 32 bit)
 */
InstructionARM64 idiv_gpr32(Register reg);

InstructionARM64 unsigned_div_gpr32(Register reg);

/*!
 * Convert doubleword to quadword for division.
 */
InstructionARM64 cdq();

/*!
 * Real ARM64 3-operand divide/mod helpers, used directly by IR_IntegerMath::do_codegen_arm64
 * (goalc/compiler/IR.cpp) instead of the x86-shaped idiv_gpr32()/unsigned_div_gpr32()/cdq() above -
 * ARM64's sdiv/udiv take an arbitrary destination and two arbitrary source registers, with no
 * RAX/RDX-style implicit pairing at all, so there's no reason to route them through the same
 * single-operand, implicit-register-destination interface x86 needs. See the development notes.
 */

/*!
 * SDIV <Wd>, <Wn>, <Wm> - 32-bit signed divide: dst = n / m.
 */
InstructionARM64 sdiv_gpr32_gpr32_gpr32(Register dst, Register n, Register m);

/*!
 * UDIV <Wd>, <Wn>, <Wm> - 32-bit unsigned divide: dst = n / m.
 */
InstructionARM64 udiv_gpr32_gpr32_gpr32(Register dst, Register n, Register m);

/*!
 * MSUB <Wd>, <Wn>, <Wm>, <Wa> - 32-bit multiply-subtract: dst = a - (n * m). Used to compute a
 * remainder as `a - (a/m)*m` after a preceding sdiv/udiv has put the quotient in `n`.
 */
InstructionARM64 msub_gpr32_gpr32_gpr32_gpr32(Register dst, Register n, Register m, Register a);

/*!
 * Move from gpr32 to gpr64, with sign extension.
 * Needed for multiplication/divsion madness.
 */
InstructionARM64 movsx_r64_r32(Register dst, Register src);

/*!
 * Compare gpr64.  This sets the flags for the jumps.
 * todo UNTESTED
 */
InstructionARM64 cmp_gpr64_gpr64(Register a, Register b);

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   BIT STUFF
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

/*!
 * Or of two gprs
 */
InstructionARM64 or_gpr64_gpr64(Register dst, Register src);

/*!
 * And of two gprs
 */
InstructionARM64 and_gpr64_gpr64(Register dst, Register src);

/*!
 * Xor of two gprs
 */
InstructionARM64 xor_gpr64_gpr64(Register dst, Register src);

/*!
 * Bitwise not a gpr
 */
InstructionARM64 not_gpr64(Register reg);

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   SHIFTS
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

/*!
 * Shift 64-bit gpr left by a shift amount in a register (ie. forced to be CL register on x86)
 */
InstructionARM64 shl_gpr64_reg(Register reg, Register shift_reg);

/*!
 * Shift 64-bit gpr right (logical) by a shift amount in a register (ie. forced to be CL register on
 * x86)
 */
InstructionARM64 shr_gpr64_reg(Register reg, Register shift_reg);

/*!
 * Shift 64-bit gpr right (arithmetic) a shift amount in a register (ie. forced to be CL register on
 * x86)
 */
InstructionARM64 sar_gpr64_reg(Register reg, Register shift_reg);

/*!
 * Shift 64-ptr left (logical) by the constant shift amount "sa".
 */
InstructionARM64 shl_gpr64_u8(Register reg, uint8_t sa);

/*!
 * Shift 64-ptr right (logical) by the constant shift amount "sa".
 */
InstructionARM64 shr_gpr64_u8(Register reg, uint8_t sa);

/*!
 * Shift 64-ptr right (arithmetic) by the constant shift amount "sa".
 */
InstructionARM64 sar_gpr64_u8(Register reg, uint8_t sa);

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   CONTROL FLOW
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

/*!
 * Jump, 32-bit constant offset.  The offset is by default 0 and must be patched later.
 */
InstructionARM64 jmp_imm();

/*!
 * Jump if equal.
 */
InstructionARM64 je_imm();

/*!
 * Jump not equal.
 */
InstructionARM64 jne_imm();

/*!
 * Jump less than or equal.
 */
InstructionARM64 jle_imm();

/*!
 * Jump greater than or equal.
 */
InstructionARM64 jge_imm();

/*!
 * Jump less than
 */
InstructionARM64 jl_imm();

/*!
 * Jump greater than
 */
InstructionARM64 jg_imm();

/*!
 * Jump below or equal
 */
InstructionARM64 jbe_imm();

/*!
 * Jump above or equal
 */
InstructionARM64 jae_imm();

/*!
 * Jump below
 */
InstructionARM64 jb_imm();

/*!
 * Jump above
 */
InstructionARM64 ja_imm();

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   FLOAT MATH
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

/*!
 * Compare two floats and set flag register for jump (ucomiss)
 */
InstructionARM64 cmp_f32_f32(Register a, Register b);

InstructionARM64 sqrt_f32(Register dst, Register src);

/*!
 * Multiply two floats in xmm's
 */
InstructionARM64 mul_f32_f32(Register dst, Register src);

/*!
 * Divide two floats in xmm's
 */
InstructionARM64 div_f32_f32(Register dst, Register src);

/*!
 * Subtract two floats in xmm's
 */
InstructionARM64 sub_f32_f32(Register dst, Register src);

/*!
 * Add two floats in xmm's
 */
InstructionARM64 add_f32_f32(Register dst, Register src);

/*!
 * Floating point minimum.
 */
InstructionARM64 min_f32_f32(Register dst, Register src);

/*!
 * Floating point maximum.
 */
InstructionARM64 max_f32_f32(Register dst, Register src);

/*!
 * Convert GPR int32 to float (single precision)
 */
InstructionARM64 int32_to_f32(Register dst, Register src);

/*!
 * Convert float to GPR int32(single precision) (truncate)
 */
InstructionARM64 f32_to_int32(Register dst, Register src);

InstructionARM64 nop();

// TODO - rsqrt / abs / sqrt

//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
//   UTILITIES
//;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

/*!
 * A "null" instruction.  This instruction does not generate any bytes
 * but can be referred to by a label.  Useful to insert in place of a real instruction
 * if the real instruction has been optimized out.
 */
InstructionARM64 null();

/////////////////////////////
// AVX (VF - Vector Float) //
/////////////////////////////

InstructionARM64 nop_vf();

InstructionARM64 wait_vf();

InstructionARM64 mov_vf_vf(Register dst, Register src);

InstructionARM64 loadvf_gpr64_plus_gpr64(Register dst, Register addr1, Register addr2);

InstructionARM64 loadvf_gpr64_plus_gpr64_plus_s8(Register dst,
                                                 Register addr1,
                                                 Register addr2,
                                                 s64 offset);

InstructionARM64 loadvf_gpr64_plus_gpr64_plus_s32(Register dst,
                                                  Register addr1,
                                                  Register addr2,
                                                  s64 offset);

InstructionARM64 storevf_gpr64_plus_gpr64(Register value, Register addr1, Register addr2);

InstructionARM64 storevf_gpr64_plus_gpr64_plus_s8(Register value,
                                                  Register addr1,
                                                  Register addr2,
                                                  s64 offset);

InstructionARM64 storevf_gpr64_plus_gpr64_plus_s32(Register value,
                                                   Register addr1,
                                                   Register addr2,
                                                   s64 offset);

InstructionARM64 loadvf_rip_plus_s32(Register dest, s64 offset);

// TODO - rip relative loads and stores.

InstructionARM64 blend_vf(Register dst, Register src1, Register src2, u8 mask);

InstructionARM64 shuffle_vf(Register dst, Register src, u8 dx, u8 dy, u8 dz, u8 dw);

/*
  Generic Swizzle (re-arrangment of packed FPs) operation, the control bytes are quite involved.
  Here's a brief run-down:
  - 8-bits / 4 groups of 2 bits
  - Right-to-left, each group is used to determine which element in `src` gets copied into
  `dst`'s element (W->X).
  - GROUP OPTIONS
  - 00b - Copy the least-significant element (X)
  - 01b - Copy the second element (from the right) (Y)
  - 10b - Copy the third element (from the right) (Z)
  - 11b - Copy the most significant element (W)
  Examples
  ; xmm1 = (1.5, 2.5, 3.5, 4.5) (W,Z,Y,X in x86 land)
  SHUFPS xmm1, xmm1, 0xff ; Copy the most significant element to all positions
  > (1.5, 1.5, 1.5, 1.5)
  SHUFPS xmm1, xmm1, 0x39 ; Rotate right
  > (4.5, 1.5, 2.5, 3.5)
  */
InstructionARM64 swizzle_vf(Register dst, Register src, u8 controlBytes);

/*
  Splats a single element in 'src' to all elements in 'dst'
  For example (pseudocode):
  xmm1 = (1.5, 2.5, 3.5, 4.5)
  xmm2 = (1, 2, 3, 4)
  splat_vf(xmm1, xmm2, XMM_ELEMENT::X);
  xmm1 = (4, 4, 4, 4)
  */
InstructionARM64 splat_vf(Register dst, Register src, Register::VF_ELEMENT element);

InstructionARM64 xor_vf(Register dst, Register src1, Register src2);

InstructionARM64 sub_vf(Register dst, Register src1, Register src2);

InstructionARM64 add_vf(Register dst, Register src1, Register src2);

InstructionARM64 mul_vf(Register dst, Register src1, Register src2);

InstructionARM64 max_vf(Register dst, Register src1, Register src2);

InstructionARM64 min_vf(Register dst, Register src1, Register src2);

InstructionARM64 div_vf(Register dst, Register src1, Register src2);

InstructionARM64 sqrt_vf(Register dst, Register src);

InstructionARM64 itof_vf(Register dst, Register src);

InstructionARM64 ftoi_vf(Register dst, Register src);

InstructionARM64 pw_sra(Register dst, Register src, u8 imm);

InstructionARM64 pw_srl(Register dst, Register src, u8 imm);

InstructionARM64 ph_srl(Register dst, Register src, u8 imm);

InstructionARM64 pw_sll(Register dst, Register src, u8 imm);

InstructionARM64 ph_sll(Register dst, Register src, u8 imm);

InstructionARM64 parallel_add_byte(Register dst, Register src0, Register src1);

InstructionARM64 parallel_bitwise_or(Register dst, Register src0, Register src1);

InstructionARM64 parallel_bitwise_xor(Register dst, Register src0, Register src1);

InstructionARM64 parallel_bitwise_and(Register dst, Register src0, Register src1);

// Reminder - a word in MIPS = 32bits = a DWORD in x86
//     MIPS   ||   x86
// -----------------------
// byte       || byte
// halfword   || word
// word       || dword
// doubleword || quadword

// -- Unpack High Data Instructions
InstructionARM64 pextub_swapped(Register dst, Register src0, Register src1);

InstructionARM64 pextuh_swapped(Register dst, Register src0, Register src1);

InstructionARM64 pextuw_swapped(Register dst, Register src0, Register src1);

// -- Unpack Low Data Instructions
InstructionARM64 pextlb_swapped(Register dst, Register src0, Register src1);

InstructionARM64 pextlh_swapped(Register dst, Register src0, Register src1);

InstructionARM64 pextlw_swapped(Register dst, Register src0, Register src1);

// Equal to than comparison as 16 bytes (8 bits)
InstructionARM64 parallel_compare_e_b(Register dst, Register src0, Register src1);

// Equal to than comparison as 8 halfwords (16 bits)
InstructionARM64 parallel_compare_e_h(Register dst, Register src0, Register src1);

// Equal to than comparison as 4 words (32 bits)
InstructionARM64 parallel_compare_e_w(Register dst, Register src0, Register src1);

// Greater than comparison as 16 bytes (8 bits)
InstructionARM64 parallel_compare_gt_b(Register dst, Register src0, Register src1);

// Greater than comparison as 8 halfwords (16 bits)
InstructionARM64 parallel_compare_gt_h(Register dst, Register src0, Register src1);

// Greater than comparison as 4 words (32 bits)
InstructionARM64 parallel_compare_gt_w(Register dst, Register src0, Register src1);

InstructionARM64 vpunpcklqdq(Register dst, Register src0, Register src1);

InstructionARM64 pcpyld_swapped(Register dst, Register src0, Register src1);

InstructionARM64 pcpyud(Register dst, Register src0, Register src1);

InstructionARM64 vpsubd(Register dst, Register src0, Register src1);

InstructionARM64 vpsrldq(Register dst, Register src, u8 imm);

InstructionARM64 vpslldq(Register dst, Register src, u8 imm);

InstructionARM64 vpshuflw(Register dst, Register src, u8 imm);

InstructionARM64 vpshufhw(Register dst, Register src, u8 imm);

InstructionARM64 vpackuswb(Register dst, Register src0, Register src1);
}  // namespace ARM64
}  // namespace IGen
}  // namespace emitter
