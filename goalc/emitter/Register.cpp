#include "Register.h"

#include <stdexcept>

namespace emitter {
RegisterInfo RegisterInfo::make_register_info() {
  RegisterInfo info;

  info.m_process_reg = R13;
  info.m_st_reg = R14;
  info.m_offset_reg = R15;
  info.m_gpr_ret_reg = RAX;
  info.m_xmm_ret_reg = XMM0;

  info.m_info[RAX] = {false, false, "rax"};  // return, temp
  info.m_info[RCX] = {false, false, "rcx"};  // gpr arg 3, temp
  info.m_info[RDX] = {false, false, "rdx"};  // gpr arg 2, temp
  info.m_info[RBX] = {true, false, "rbx"};   // saved
  info.m_info[RSP] = {false, true, "rsp"};   // stack pointer
  info.m_info[RBP] = {true, false, "rbp"};   // saved
  info.m_info[RSI] = {false, false, "rsi"};  // gpr arg 1, temp
  info.m_info[RDI] = {false, false, "rdi"};  // gpr arg 0, temp

  info.m_info[R8] = {false, false, "r8"};   // gpr arg 4, temp
  info.m_info[R9] = {false, false, "r9"};   // gpr arg 5, temp
  info.m_info[R10] = {true, false, "r10"};  // gpr arg 6, saved
  info.m_info[R11] = {true, false, "r11"};  // gpr arg 7, saved
  info.m_info[R12] = {true, false, "r12"};  // saved
  info.m_info[R13] = {false, true, "r13"};  // pp
  info.m_info[R14] = {false, true, "r14"};  // st
  info.m_info[R15] = {false, true, "r15"};  // offset.

  info.m_info[XMM0] = {false, false, "xmm0"};
  info.m_info[XMM1] = {false, false, "xmm1"};
  info.m_info[XMM2] = {false, false, "xmm2"};
  info.m_info[XMM3] = {false, false, "xmm3"};
  info.m_info[XMM4] = {false, false, "xmm4"};
  info.m_info[XMM5] = {false, false, "xmm5"};
  info.m_info[XMM6] = {false, false, "xmm6"};
  info.m_info[XMM7] = {false, false, "xmm7"};
  info.m_info[XMM8] = {true, false, "xmm8"};
  info.m_info[XMM9] = {true, false, "xmm9"};
  info.m_info[XMM10] = {true, false, "xmm10"};
  info.m_info[XMM11] = {true, false, "xmm11"};
  info.m_info[XMM12] = {true, false, "xmm12"};
  info.m_info[XMM13] = {true, false, "xmm13"};
  info.m_info[XMM14] = {true, false, "xmm14"};
  info.m_info[XMM15] = {true, false, "xmm15"};

  info.m_gpr_arg_regs = std::array<Register, N_ARGS>({RDI, RSI, RDX, RCX, R8, R9, R10, R11});
  // skip xmm0 so it can be used for return.
  info.m_xmm_arg_regs =
      std::array<Register, N_ARGS>({XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7, XMM8});
  info.m_saved_gprs = std::array<Register, N_SAVED_GPRS>({RBX, RBP, R10, R11, R12});
  info.m_saved_xmms =
      std::array<Register, N_SAVED_XMMS>({XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15});

  for (size_t i = 0; i < N_SAVED_GPRS; i++) {
    info.m_saved_all[i] = info.m_saved_gprs[i];
  }
  for (size_t i = 0; i < N_SAVED_XMMS; i++) {
    info.m_saved_all[i + N_SAVED_GPRS] = info.m_saved_xmms[i];
  }

  // todo - experiment with better orders for allocation.
  info.m_gpr_alloc_order = {RAX, RCX, RDX, RBX, RBP, RSI, RDI, R8, R9, R10};  // arbitrary
  info.m_xmm_alloc_order = {XMM0, XMM1, XMM2, XMM3,  XMM4,  XMM5,  XMM6,
                            XMM7, XMM8, XMM9, XMM10, XMM11, XMM12, XMM13};

  // these should only be temp registers!
  info.m_gpr_temp_only_alloc_order = {RAX, RCX, RDX, RSI, RDI, R8, R9};
  info.m_xmm_temp_only_alloc_order = {XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7};

  info.m_gpr_spill_temp_alloc_order = {RAX, RCX, RDX, RBX, RBP, RSI,
                                       RDI, R8,  R9,  R10, R11, R12};  // arbitrary
  info.m_xmm_spill_temp_alloc_order = {XMM0, XMM1, XMM2,  XMM3,  XMM4,  XMM5,  XMM6,  XMM7,
                                       XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15};
  return info;
}

/*!
 * ARM64 counterpart to make_register_info(). Mirrors its x86 structure and array sizes as
 * closely as possible (this project's calling convention/register allocator code is shared
 * between both targets and expects the same table shapes), using the register roles ARM64_REG's
 * own comments already describe: X0-X7 for integer args (matching AAPCS64), X8 as the
 * return/temp register (playing RAX's role here, not AAPCS64's real meaning for X8), X20/X21/X22
 * as the special pp/st/offset registers (matching R13/R14/R15's role on x86), and V0-V15 (only -
 * not the full V0-V31 some hardware has, per the "TODO ARM - check at runtime" comment on the
 * enum) for 128-bit vector args/temps/saved, built via a small helper since a bare V0-style
 * constant alone can't distinguish itself from X0 (both are 0) - see Register::m_is_arm64_vector.
 */
RegisterInfo RegisterInfo::make_arm64_register_info() {
  RegisterInfo info;
  auto vreg = [](int id) { return Register(id, true); };

  info.m_process_reg = X20;
  info.m_st_reg = X21;
  info.m_offset_reg = X22;
  info.m_gpr_ret_reg = X8;
  info.m_xmm_ret_reg = vreg(V0);

  info.m_info[X0] = {false, false, "x0"};
  info.m_info[X1] = {false, false, "x1"};
  info.m_info[X2] = {false, false, "x2"};
  info.m_info[X3] = {false, false, "x3"};
  info.m_info[X4] = {false, false, "x4"};
  info.m_info[X5] = {false, false, "x5"};
  info.m_info[X6] = {false, false, "x6"};
  info.m_info[X7] = {false, false, "x7"};
  info.m_info[X8] = {false, false, "x8"};  // return, temp
  info.m_info[X9] = {false, false, "x9"};
  info.m_info[X10] = {false, false, "x10"};
  info.m_info[X11] = {false, false, "x11"};
  info.m_info[X12] = {false, false, "x12"};
  info.m_info[X13] = {false, false, "x13"};  // temp, not-saved
  info.m_info[X14] = {false, false, "x14"};  // temp, not-saved
  info.m_info[X15] = {false, false, "x15"};  // temp, not-saved
  // X16/X17 are conventionally reserved for the linker/veneers, X18 is the AAPCS64 platform
  // register - none of the three are ever handed out by an alloc order above, but mark them
  // `special` (not `temp`) so the IR_FunctionCall::to_rai() "clobber every temp register" loop in
  // IR.cpp doesn't also silently clobber them, and so Register::print()/RegisterInfo::get_info()
  // never return a blank name for them (both previously fell through to a default-constructed,
  // nameless `Info` for X13-X18, since none of the six had ever been given an entry here).
  info.m_info[X16] = {false, true, "x16"};
  info.m_info[X17] = {false, true, "x17"};
  info.m_info[X18] = {false, true, "x18"};
  info.m_info[X19] = {true, false, "x19"};  // saved
  info.m_info[X20] = {false, true, "x20"};  // pp
  info.m_info[X21] = {false, true, "x21"};  // st
  info.m_info[X22] = {false, true, "x22"};  // offset
  info.m_info[X23] = {true, false, "x23"};  // saved
  info.m_info[X24] = {true, false, "x24"};  // saved
  info.m_info[X25] = {true, false, "x25"};  // saved
  info.m_info[X26] = {true, false, "x26"};  // saved
  info.m_info[SP] = {false, true, "sp"};

  info.m_info[V0 + 32] = {false, false, "v0"};
  info.m_info[V1 + 32] = {false, false, "v1"};
  info.m_info[V2 + 32] = {false, false, "v2"};
  info.m_info[V3 + 32] = {false, false, "v3"};
  info.m_info[V4 + 32] = {false, false, "v4"};
  info.m_info[V5 + 32] = {false, false, "v5"};
  info.m_info[V6 + 32] = {false, false, "v6"};
  info.m_info[V7 + 32] = {false, false, "v7"};
  info.m_info[V8 + 32] = {true, false, "v8"};  // saved
  info.m_info[V9 + 32] = {true, false, "v9"};  // saved
  info.m_info[V10 + 32] = {true, false, "v10"};  // saved
  info.m_info[V11 + 32] = {true, false, "v11"};  // saved
  info.m_info[V12 + 32] = {true, false, "v12"};  // saved
  info.m_info[V13 + 32] = {true, false, "v13"};  // saved
  info.m_info[V14 + 32] = {true, false, "v14"};  // saved
  info.m_info[V15 + 32] = {true, false, "v15"};  // saved

  info.m_gpr_arg_regs = std::array<Register, N_ARGS>({X0, X1, X2, X3, X4, X5, X6, X7});
  // skip v0 so it can be used for return, same reasoning as x86 skipping xmm0.
  info.m_xmm_arg_regs = std::array<Register, N_ARGS>(
      {vreg(V1), vreg(V2), vreg(V3), vreg(V4), vreg(V5), vreg(V6), vreg(V7), vreg(V8)});
  info.m_saved_gprs = std::array<Register, N_SAVED_GPRS>({X19, X23, X24, X25, X26});
  info.m_saved_xmms = std::array<Register, N_SAVED_XMMS>(
      {vreg(V8), vreg(V9), vreg(V10), vreg(V11), vreg(V12), vreg(V13), vreg(V14), vreg(V15)});

  for (size_t i = 0; i < N_SAVED_GPRS; i++) {
    info.m_saved_all[i] = info.m_saved_gprs[i];
  }
  for (size_t i = 0; i < N_SAVED_XMMS; i++) {
    info.m_saved_all[i + N_SAVED_GPRS] = info.m_saved_xmms[i];
  }

  info.m_gpr_alloc_order = {X0, X1, X2, X3, X4, X5, X6, X7, X9, X10};
  info.m_xmm_alloc_order = {vreg(V0), vreg(V1), vreg(V2), vreg(V3),  vreg(V4),  vreg(V5), vreg(V6),
                            vreg(V7), vreg(V8), vreg(V9), vreg(V10), vreg(V11), vreg(V12), vreg(V13)};

  // NOTE: the last element of this list is handed out by several IR nodes
  // (IR_GetSymbolValue/IR_SetSymbolValue/IR_IntegerMath's IMOD_32/UMOD_32 - see IR.cpp's
  // `get_gpr_temp_alloc_order().back()` call sites) as a hardcoded "clobber" scratch register for
  // their ARM64-specific multi-instruction codegen, which genuinely needs that register to stay
  // distinct from the instruction's own destination register for the whole sequence - not just
  // informational, unlike x86 (whose codegen never actually touches the clobbered register, per
  // each call site's "harmless no-op on x86" comment). It used to end in X6, which is ALSO in
  // `m_gpr_alloc_order` right above - the register allocator's clobber model explicitly allows a
  // variable to share its clobbered register whenever the instruction eventually writes that
  // variable (see Allocator_v2.cpp's check_register_assign(), "we write it after the clobber"),
  // so whenever the allocator happened to assign X6 to one of those IR nodes' own destination
  // variable, its ARM64 codegen would silently clobber its own destination mid-instruction -
  // confirmed as the real crash this session (the development notes: "add x6,x6,x6" -> SIGSEGV
  // dereferencing a doubled garbage address) and a second, silent (non-crashing but wrong-result)
  // instance in IMOD_32/UMOD_32's msub sequence. X17 is conventionally reserved for the
  // linker/veneers (see the comment above on X16/X17) and deliberately absent from
  // `m_gpr_alloc_order`, so it can never be assigned to a real GOAL variable in the first place -
  // ending the list with it instead removes the collision entirely rather than relying on the
  // allocator's clobber model to happen to avoid it.
  info.m_gpr_temp_only_alloc_order = {X0, X1, X2, X3, X4, X5, X17};
  info.m_xmm_temp_only_alloc_order = {vreg(V0), vreg(V1), vreg(V2), vreg(V3),
                                      vreg(V4), vreg(V5), vreg(V6), vreg(V7)};

  info.m_gpr_spill_temp_alloc_order = {X0, X1, X2, X3, X4, X5, X6, X7, X9, X10, X11, X12};
  // V14/V15 must remain available for spills; emitter scratch uses V30/V31. (AI-assisted)
  info.m_xmm_spill_temp_alloc_order = {
      vreg(V0), vreg(V1), vreg(V2),  vreg(V3),  vreg(V4),  vreg(V5),  vreg(V6),  vreg(V7),
      vreg(V8), vreg(V9), vreg(V10), vreg(V11), vreg(V12), vreg(V13), vreg(V14), vreg(V15)};
  return info;
}

RegisterInfo gRegInfo = RegisterInfo::make_register_info();

std::string to_string(HWRegKind kind) {
  switch (kind) {
    case HWRegKind::GPR:
      return "gpr";
    case HWRegKind::XMM:
      return "xmm";
    default:
      throw std::runtime_error("Unsupported HWRegKind");
  }
}

HWRegKind reg_class_to_hw(RegClass reg_class) {
  switch (reg_class) {
    case RegClass::VECTOR_FLOAT:
    case RegClass::FLOAT:
    case RegClass::INT_128:
      return HWRegKind::XMM;
    case RegClass::GPR_64:
      return HWRegKind::GPR;
    default:
      ASSERT(false);
      return HWRegKind::INVALID;
  }
}

std::string Register::print() const {
  return gRegInfo.get_info(*this).name;
}

}  // namespace emitter
