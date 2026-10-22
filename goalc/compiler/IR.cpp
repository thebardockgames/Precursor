#include "IR.h"

#include <utility>

#include "common/symbols.h"

#include "goalc/compiler/Env.h"
#include "goalc/emitter/IGen.h"
#include "goalc/emitter/IGenARM64.h"

#include "fmt/format.h"

// TODO ARM64 - just silencing errors while things are not implemented obviously
#pragma GCC diagnostic ignored "-Wunused-parameter"

using namespace emitter;
namespace {
Register get_reg(const RegVal* rv, const AllocationResult& allocs, emitter::IR_Record irec) {
  if (rv->rlet_constraint().has_value()) {
    auto& range = allocs.ass_as_ranges;
    auto reg = rv->rlet_constraint().value();
    if (rv->ireg().id < int(range.size())) {
      auto& lr = range.at(rv->ireg().id);
      if (lr.has_info_at(irec.ir_id)) {
        auto ass_reg = range.at(rv->ireg().id).get(irec.ir_id);
        if (ass_reg.kind == Assignment::Kind::REGISTER) {
          ASSERT(ass_reg.reg == reg);
        } else {
          ASSERT(false);
        }
      } else {
        ASSERT(false);
      }
    } else {
      ASSERT(false);
    }
    return reg;
  } else {
    auto& ass = allocs.ass_as_ranges.at(rv->ireg().id).get(irec.ir_id);
    ASSERT(ass.kind == Assignment::Kind::REGISTER);
    return ass.reg;
  }
}

int get_stack_offset(const RegVal* rv, const AllocationResult& allocs) {
  if (rv->rlet_constraint().has_value()) {
    // should be impossible. Can't take the address of an inline assembly form register.
    ASSERT(false);
  } else {
    ASSERT(rv->forced_on_stack());
    auto& ass = allocs.ass_as_ranges.at(rv->ireg().id);
    auto stack_slot = allocs.get_slot_for_spill(ass.stack_slot());
    ASSERT(stack_slot >= 0);
    return stack_slot * 8;
  }
}

Register get_no_color_reg(const RegVal* rv) {
  if (!rv->rlet_constraint().has_value()) {
    throw std::runtime_error(
        "Accessed a non-rlet constrained variable without the coloring system.");
  }
  return rv->rlet_constraint().value();
}

Register get_reg_asm(const RegVal* rv,
                     const AllocationResult& allocs,
                     emitter::IR_Record irec,
                     bool use_coloring) {
  return use_coloring ? get_reg(rv, allocs, irec) : get_no_color_reg(rv);
}

void load_constant(u64 value,
                   emitter::ObjectGenerator* gen,
                   emitter::IR_Record irec,
                   Register dest_reg) {
  s64 svalue = value;
  if (svalue == 0) {
    gen->add_instr(IGen::xor_gpr64_gpr64(*gen, dest_reg, dest_reg), irec);
  } else if (svalue > 0) {
    if (svalue < UINT32_MAX) {
      gen->add_instr(IGen::mov_gpr64_u32(*gen, dest_reg, value), irec);
    } else {
      // need a real 64 bit load
      gen->add_instr(IGen::mov_gpr64_u64(*gen, dest_reg, value), irec);
    }
  } else {
    if (svalue >= INT32_MIN) {
      gen->add_instr(IGen::mov_gpr64_s32(*gen, dest_reg, svalue), irec);
    } else {
      // need a real 64 bit load
      gen->add_instr(IGen::mov_gpr64_u64(*gen, dest_reg, value), irec);
    }
  }
}

void regset_common(emitter::ObjectGenerator* gen,
                   const AllocationResult& allocs,
                   emitter::IR_Record irec,
                   const RegVal* dst,
                   const RegVal* src,
                   bool use_coloring) {
  auto src_reg = use_coloring ? get_reg(src, allocs, irec) : get_no_color_reg(src);
  auto dst_reg = use_coloring ? get_reg(dst, allocs, irec) : get_no_color_reg(dst);
  auto src_class = src->ireg().reg_class;
  auto dst_class = dst->ireg().reg_class;

  bool src_is_xmm128 = (src_class == RegClass::VECTOR_FLOAT || src_class == RegClass::INT_128);
  bool dst_is_xmm128 = (dst_class == RegClass::VECTOR_FLOAT || dst_class == RegClass::INT_128);

  if (src_class == RegClass::GPR_64 && dst_class == RegClass::GPR_64) {
    if (src_reg == dst_reg) {
      // eliminate move
      gen->count_eliminated_move();
      gen->add_instr(IGen::null(*gen), irec);
    } else {
      gen->add_instr(IGen::mov_gpr64_gpr64(*gen, dst_reg, src_reg), irec);
    }
  } else if (src_class == RegClass::FLOAT && dst_class == RegClass::FLOAT) {
    if (src_reg == dst_reg) {
      // eliminate move
      gen->count_eliminated_move();
      gen->add_instr(IGen::null(*gen), irec);
    } else {
      gen->add_instr(IGen::mov_f32_f32(*gen, dst_reg, src_reg), irec);
    }
  } else if (src_is_xmm128 && dst_is_xmm128) {
    if (src_reg == dst_reg) {
      // eliminate move
      gen->count_eliminated_move();
      gen->add_instr(IGen::null(*gen), irec);
    } else {
      gen->add_instr(IGen::mov_vf_vf(*gen, dst_reg, src_reg), irec);
    }
  } else if (src_class == RegClass::FLOAT && dst_class == RegClass::GPR_64) {
    // xmm 1x -> gpr
    gen->add_instr(IGen::movd_gpr32_f32(*gen, dst_reg, src_reg), irec);
    // don't forget to sign extend
    gen->add_instr(IGen::movsx_r64_r32(*gen, dst_reg, dst_reg), irec);
  } else if (src_class == RegClass::GPR_64 && dst_class == RegClass::FLOAT) {
    // gpr -> xmm 1x
    gen->add_instr(IGen::movd_f32_gpr32(*gen, dst_reg, src_reg), irec);
  } else if (src_is_xmm128 && dst_class == RegClass::FLOAT) {
    gen->add_instr(IGen::mov_f32_f32(*gen, dst_reg, src_reg), irec);
  } else if (src_class == RegClass::FLOAT && dst_is_xmm128) {
    gen->add_instr(IGen::mov_f32_f32(*gen, dst_reg, src_reg), irec);
  } else if (src_class == RegClass::GPR_64 && dst_is_xmm128) {
    gen->add_instr(IGen::movq_f64_gpr64(*gen, dst_reg, src_reg), irec);
  } else if (src_is_xmm128 && dst_class == RegClass::GPR_64) {
    gen->add_instr(IGen::movq_gpr64_f64(*gen, dst_reg, src_reg), irec);
  } else {
    ASSERT(false);  // unhandled move.
  }
}
}  // namespace

///////////
// Return
///////////
IR_Return::IR_Return(const RegVal* return_reg, const RegVal* value, emitter::Register ret_reg)
    : m_return_reg(return_reg), m_value(value), m_ret_reg(ret_reg) {}
std::string IR_Return::print() {
  return fmt::format("ret {} {}", m_return_reg->print(), m_value->print());
}

RegAllocInstr IR_Return::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_return_reg->ireg());
  rai.read.push_back(m_value->ireg());
  if (m_value->ireg().reg_class == m_return_reg->ireg().reg_class) {
    rai.is_move = true;  // only true if we aren't moving from register kind to register kind
  }
  return rai;
}

void IR_Return::add_constraints(std::vector<IRegConstraint>* constraints, int my_id) {
  IRegConstraint c;
  if (dynamic_cast<const None*>(m_return_reg)) {
    return;
  }

  c.ireg = m_return_reg->ireg();
  c.instr_idx = my_id;
  c.desired_register = m_ret_reg;
  constraints->push_back(c);
}

void IR_Return::do_codegen_x86(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  auto val_reg = get_reg(m_value, allocs, irec);
  auto dest_reg = get_reg(m_return_reg, allocs, irec);

  if (val_reg == dest_reg) {
    gen->add_instr(IGen::null(*gen), irec);
  } else {
    regset_common(gen, allocs, irec, m_return_reg, m_value, true);
    // gen->add_instr(IGen::mov_gpr64_gpr64(dest_reg, val_reg), irec);
  }
}

void IR_Return::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  // regset_common/IGen::null already dispatch per-architecture internally - identical to the x86
  // version above.
  auto val_reg = get_reg(m_value, allocs, irec);
  auto dest_reg = get_reg(m_return_reg, allocs, irec);

  if (val_reg == dest_reg) {
    gen->add_instr(IGen::null(*gen), irec);
  } else {
    regset_common(gen, allocs, irec, m_return_reg, m_value, true);
  }
}

/////////////////////
// LoadConstant64
/////////////////////
IR_LoadConstant64::IR_LoadConstant64(const RegVal* dest, u64 value)
    : m_dest(dest), m_value(value) {}

std::string IR_LoadConstant64::print() {
  return fmt::format("mov-ic {}, {}", m_dest->print(), m_value);
}

RegAllocInstr IR_LoadConstant64::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_LoadConstant64::do_codegen_x86(emitter::ObjectGenerator* gen,
                                       const AllocationResult& allocs,
                                       emitter::IR_Record irec) {
  auto dest_reg = get_reg(m_dest, allocs, irec);
  load_constant(m_value, gen, irec, dest_reg);
}

void IR_LoadConstant64::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                         const AllocationResult& allocs,
                                         emitter::IR_Record irec) {
  // load_constant already dispatches per-architecture internally (mov_gpr64_u32/u64/s32/
  // xor_gpr64_gpr64 all have real ARM64 implementations) - identical to the x86 version above.
  auto dest_reg = get_reg(m_dest, allocs, irec);
  load_constant(m_value, gen, irec, dest_reg);
}

/////////////////////
// LoadSymbolPointer
/////////////////////
IR_LoadSymbolPointer::IR_LoadSymbolPointer(const RegVal* dest, std::string name)
    : m_dest(dest), m_name(std::move(name)) {}

std::string IR_LoadSymbolPointer::print() {
  return fmt::format("mov-symptr {}, '{}", m_dest->print(), m_name);
}

RegAllocInstr IR_LoadSymbolPointer::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_LoadSymbolPointer::do_codegen_x86(emitter::ObjectGenerator* gen,
                                          const AllocationResult& allocs,
                                          emitter::IR_Record irec) {
  auto dest_reg = get_reg(m_dest, allocs, irec);
  if (m_name == "#f") {
    static_assert(false_symbol_offset() == 0, "false symbol location");
    if (dest_reg.is_xmm(gen->instr_set())) {
      gen->add_instr(IGen::movq_f64_gpr64(*gen, dest_reg, gRegInfo.get_st_reg()), irec);
    } else {
      gen->add_instr(IGen::mov_gpr64_gpr64(*gen, dest_reg, gRegInfo.get_st_reg()), irec);
    }
  } else if (m_name == "#t") {
    gen->add_instr(IGen::lea_reg_plus_off8(*gen, dest_reg, gRegInfo.get_st_reg(),
                                           true_symbol_offset(gen->version())),
                   irec);
  } else if (m_name == "_empty_") {
    gen->add_instr(IGen::lea_reg_plus_off8(*gen, dest_reg, gRegInfo.get_st_reg(),
                                           empty_pair_offset_from_s7(gen->version())),
                   irec);
  } else {
    auto instr = gen->add_instr(
        IGen::lea_reg_plus_off32(*gen, dest_reg, gRegInfo.get_st_reg(), 0x0afecafe), irec);
    gen->link_instruction_symbol_ptr(instr, m_name);
  }
}

void IR_LoadSymbolPointer::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                            const AllocationResult& allocs,
                                            emitter::IR_Record irec) {
  // #f/#t/_empty_ are plain ports - all three use small, compile-time-known constant offsets
  // (lea_reg_plus_off8/mov_gpr64_gpr64/movq_f64_gpr64 already have real ARM64 encodings, no
  // runtime link involved at all). The general case needs real ARM64-specific handling - see
  // IGen::ARM64::load_symbol_ptr_offset() for why x86's lea_reg_plus_off32 + link_instruction_
  // symbol_ptr() (which patches a plain disp32 baked into the `add`'s own encoding) can't just be
  // reused as-is.
  auto dest_reg = get_reg(m_dest, allocs, irec);
  if (m_name == "#f") {
    static_assert(false_symbol_offset() == 0, "false symbol location");
    if (dest_reg.is_xmm(gen->instr_set())) {
      gen->add_instr(IGen::movq_f64_gpr64(*gen, dest_reg, gRegInfo.get_st_reg()), irec);
    } else {
      gen->add_instr(IGen::mov_gpr64_gpr64(*gen, dest_reg, gRegInfo.get_st_reg()), irec);
    }
  } else if (m_name == "#t") {
    gen->add_instr(IGen::lea_reg_plus_off8(*gen, dest_reg, gRegInfo.get_st_reg(),
                                           true_symbol_offset(gen->version())),
                   irec);
  } else if (m_name == "_empty_") {
    gen->add_instr(IGen::lea_reg_plus_off8(*gen, dest_reg, gRegInfo.get_st_reg(),
                                           empty_pair_offset_from_s7(gen->version())),
                   irec);
  } else {
    auto instr = gen->add_instr(
        Instruction(IGen::ARM64::load_symbol_ptr_offset(dest_reg, gRegInfo.get_st_reg())), irec);
    gen->link_instruction_symbol_mem(instr, m_name);
  }
}

/////////////////////
// SetSymbolValue
/////////////////////

IR_SetSymbolValue::IR_SetSymbolValue(const SymbolVal* dest, const RegVal* src)
    : m_dest(dest), m_src(src) {}

std::string IR_SetSymbolValue::print() {
  return fmt::format("mov '{}, {}", m_dest->name(), m_src->print());
}

RegAllocInstr IR_SetSymbolValue::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(m_src->ireg());
  // ARM64's do_codegen_arm64 needs a scratch register to build the store address in (st_reg +
  // offset_reg + a runtime-patched literal) - same `clobber` trick as IR_GetSymbolValue/
  // IR_IntegerMath's mod cases. Unused, harmless no-op on x86.
  rai.clobber.push_back(emitter::gRegInfo.get_gpr_temp_alloc_order().back());
  return rai;
}

void IR_SetSymbolValue::do_codegen_x86(emitter::ObjectGenerator* gen,
                                       const AllocationResult& allocs,
                                       emitter::IR_Record irec) {
  auto src_reg = get_reg(m_src, allocs, irec);
  auto instr = gen->add_instr(
      IGen::store32_gpr64_gpr64_plus_gpr64_plus_s32(
          *gen, gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(), src_reg, LINK_SYM_NO_OFFSET_FLAG),
      irec);
  gen->link_instruction_symbol_mem(instr, m_dest->name());
}

void IR_SetSymbolValue::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                         const AllocationResult& allocs,
                                         emitter::IR_Record irec) {
  // Store counterpart of IR_GetSymbolValue::do_codegen_arm64 - see
  // IGen::ARM64::store_symbol_value() for why this can't reuse x86's single
  // store32_gpr64_gpr64_plus_gpr64_plus_s32() call.
  auto src_reg = get_reg(m_src, allocs, irec);
  auto scratch = emitter::gRegInfo.get_gpr_temp_alloc_order().back();
  auto instr = gen->add_instr(
      Instruction(IGen::ARM64::store_symbol_value(gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(),
                                                  src_reg, scratch, LINK_SYM_NO_OFFSET_FLAG)),
      irec);
  gen->link_instruction_symbol_mem(instr, m_dest->name());
}

/////////////////////
// GetSymbolValue
/////////////////////

IR_GetSymbolValue::IR_GetSymbolValue(const RegVal* dest, const SymbolVal* src, bool sext)
    : m_dest(dest), m_src(src), m_sext(sext) {}

std::string IR_GetSymbolValue::print() {
  return fmt::format("mov {}, '{}", m_dest->print(), m_src->name());
}

RegAllocInstr IR_GetSymbolValue::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  // ARM64's do_codegen_arm64 needs a scratch register distinct from dest to hold the
  // runtime-patched symbol offset while it still has to add dest (partially built into st_reg +
  // offset_reg) to it - same `clobber` trick as IR_IntegerMath's IMOD_32/UMOD_32 (see that node's
  // to_rai() for the fuller explanation). Unused, harmless no-op on x86.
  rai.clobber.push_back(emitter::gRegInfo.get_gpr_temp_alloc_order().back());
  return rai;
}

void IR_GetSymbolValue::do_codegen_x86(emitter::ObjectGenerator* gen,
                                       const AllocationResult& allocs,
                                       emitter::IR_Record irec) {
  auto dst_reg = get_reg(m_dest, allocs, irec);
  if (m_sext) {
    auto instr = gen->add_instr(IGen::load32s_gpr64_gpr64_plus_gpr64_plus_s32(
                                    *gen, dst_reg, gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(),
                                    LINK_SYM_NO_OFFSET_FLAG),
                                irec);
    gen->link_instruction_symbol_mem(instr, m_src->name());
  } else {
    auto instr = gen->add_instr(IGen::load32u_gpr64_gpr64_plus_gpr64_plus_s32(
                                    *gen, dst_reg, gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(),
                                    LINK_SYM_NO_OFFSET_FLAG),
                                irec);
    gen->link_instruction_symbol_mem(instr, m_src->name());
  }
}

void IR_GetSymbolValue::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                         const AllocationResult& allocs,
                                         emitter::IR_Record irec) {
  // See IGen::ARM64::load_symbol_value() for why this can't reuse x86's single
  // load32(s|u)_gpr64_gpr64_plus_gpr64_plus_s32() call - ARM64 has no way to bake a
  // runtime-patched value into a small bit-packed immediate field the way x86's disp32 does.
  auto dst_reg = get_reg(m_dest, allocs, irec);
  auto scratch = emitter::gRegInfo.get_gpr_temp_alloc_order().back();
  auto instr = gen->add_instr(
      Instruction(IGen::ARM64::load_symbol_value(dst_reg, gRegInfo.get_st_reg(),
                                                 gRegInfo.get_offset_reg(), scratch,
                                                 LINK_SYM_NO_OFFSET_FLAG, m_sext)),
      irec);
  gen->link_instruction_symbol_mem(instr, m_src->name());
}

/////////////////////
// RegSet
/////////////////////

IR_RegSet::IR_RegSet(const RegVal* dest, const RegVal* src) : m_dest(dest), m_src(src) {}

RegAllocInstr IR_RegSet::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  rai.read.push_back(m_src->ireg());
  if (m_dest->ireg().reg_class == m_src->ireg().reg_class) {
    rai.is_move = true;  // only true if we aren't moving from register kind to register kind
  }
  return rai;
}

void IR_RegSet::do_codegen_x86(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  regset_common(gen, allocs, irec, m_dest, m_src, true);
}

void IR_RegSet::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  // regset_common already dispatches per-architecture internally - identical to the x86 version.
  regset_common(gen, allocs, irec, m_dest, m_src, true);
}

std::string IR_RegSet::print() {
  return fmt::format("mov {}, {}", m_dest->print(), m_src->print());
}

/////////////////////
// GotoLabel
/////////////////////

IR_GotoLabel::IR_GotoLabel(const Label* dest) : m_dest(dest) {
  m_resolved = true;
}

IR_GotoLabel::IR_GotoLabel() {
  m_resolved = false;
}

std::string IR_GotoLabel::print() {
  return fmt::format("goto {}", m_dest->print());
}

RegAllocInstr IR_GotoLabel::to_rai() {
  ASSERT(m_resolved);
  RegAllocInstr rai;
  rai.jumps.push_back(m_dest->idx);
  rai.fallthrough = false;
  return rai;
}

void IR_GotoLabel::do_codegen_x86(emitter::ObjectGenerator* gen,
                                  const AllocationResult& allocs,
                                  emitter::IR_Record irec) {
  (void)allocs;
  auto instr = gen->add_instr(IGen::jmp_imm(*gen), irec);
  gen->link_instruction_jump(instr, gen->get_future_ir_record_in_same_func(irec, m_dest->idx));
}

void IR_GotoLabel::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                    const AllocationResult& allocs,
                                    emitter::IR_Record irec) {
  (void)allocs;
  // IGen::jmp_imm() dispatches to the real ARM64 unconditional `B` encoding - see
  // IR_ConditionalBranch::do_codegen_arm64 just above for the full story on how its Imm26 target
  // gets patched.
  auto instr = gen->add_instr(IGen::jmp_imm(*gen), irec);
  gen->link_instruction_jump(instr, gen->get_future_ir_record_in_same_func(irec, m_dest->idx));
}

void IR_GotoLabel::resolve(const Label* dest) {
  ASSERT(!m_resolved);
  m_dest = dest;
  m_resolved = true;
}

/////////////////////
// FunctionCall
/////////////////////

IR_FunctionCall::IR_FunctionCall(const RegVal* func,
                                 const RegVal* ret,
                                 std::vector<RegVal*> args,
                                 std::vector<emitter::Register> arg_regs,
                                 std::optional<emitter::Register> ret_reg)
    : m_func(func),
      m_ret(ret),
      m_args(std::move(args)),
      m_arg_regs(std::move(arg_regs)),
      m_ret_reg(ret_reg) {}

std::string IR_FunctionCall::print() {
  std::string result = fmt::format("call {} (ret {}) (args ", m_func->print(), m_ret->print());
  for (const auto& x : m_args) {
    result += fmt::format("{} ", x->print());
  }
  result.pop_back();
  result.push_back(')');
  return result;
}

RegAllocInstr IR_FunctionCall::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(m_func->ireg());
  rai.write.push_back(m_func->ireg());  // todo, can we avoid this?
  rai.write.push_back(m_ret->ireg());
  for (auto& arg : m_args) {
    rai.read.push_back(arg->ireg());
  }

  rai.clobber = emitter::gRegInfo.get_temp_registers();

  return rai;
}

void IR_FunctionCall::add_constraints(std::vector<IRegConstraint>* constraints, int my_id) {
  for (size_t i = 0; i < m_args.size(); i++) {
    IRegConstraint c;
    c.ireg = m_args.at(i)->ireg();
    c.instr_idx = my_id;
    c.desired_register = m_arg_regs.at(i);
    constraints->push_back(c);
  }

  if (m_ret_reg) {
    IRegConstraint c;
    c.ireg = m_ret->ireg();
    c.desired_register = *m_ret_reg;
    c.instr_idx = my_id;
    constraints->push_back(c);
  }
}

void IR_FunctionCall::do_codegen_x86(emitter::ObjectGenerator* gen,
                                     const AllocationResult& allocs,
                                     emitter::IR_Record irec) {
  auto freg = get_reg(m_func, allocs, irec);
  gen->add_instr(IGen::add_gpr64_gpr64(*gen, freg, emitter::gRegInfo.get_offset_reg()), irec);
  gen->add_instr(IGen::call_r64(*gen, freg), irec);
  // todo, can we do a sub to undo the modification to the register? does that actually work?
}

void IR_FunctionCall::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                       const AllocationResult& allocs,
                                       emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::add_gpr64_gpr64()/call_r64() both already dispatch to
  // real ARM64 encodings (call_r64 -> BLR <Xn>). Stack alignment at the call boundary is a
  // property of the *calling* function's own prologue (do_goal_function_arm64(), CodeGenerator.cpp)
  // maintaining 16-byte alignment throughout its frame, not something each individual call site
  // needs to re-establish.
  auto freg = get_reg(m_func, allocs, irec);
  gen->add_instr(IGen::add_gpr64_gpr64(*gen, freg, emitter::gRegInfo.get_offset_reg()), irec);
  gen->add_instr(IGen::call_r64(*gen, freg), irec);
}

/////////////////////
// RegValAddr
/////////////////////

IR_RegValAddr::IR_RegValAddr(const RegVal* dest, const RegVal* src) : m_dest(dest), m_src(src) {}

std::string IR_RegValAddr::print() {
  return fmt::format("mov {}, &{}", m_dest->print(), m_src->print());
}

RegAllocInstr IR_RegValAddr::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  // we don't actually read the value in m_src, so we don't need to add it here.
  return rai;
}

void IR_RegValAddr::do_codegen_x86(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  int stack_offset = get_stack_offset(m_src, allocs);
  auto dst = get_reg(m_dest, allocs, irec);
  // x86 pointer to var
  gen->add_instr(IGen::lea_reg_plus_off(*gen, dst, RSP, stack_offset), irec);
  // x86 -> GOAL pointer
  gen->add_instr(IGen::sub_gpr64_gpr64(*gen, dst, emitter::gRegInfo.get_offset_reg()), irec);
}

void IR_RegValAddr::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                     const AllocationResult& allocs,
                                     emitter::IR_Record irec) {
  // Same structure as do_codegen_x86 above, just SP instead of RSP (both real ARM64/x86 stack
  // pointers respectively) - IGen::lea_reg_plus_off()'s ARM64 backend already supports an SP base
  // (real `ADD <Xd|SP>, <Xn|SP>, #imm` encoding) with a compile-time-known offset, no linker
  // patching involved here at all (unlike IR_StaticVarAddr/IR_FunctionAddr above).
  int stack_offset = get_stack_offset(m_src, allocs);
  auto dst = get_reg(m_dest, allocs, irec);
  gen->add_instr(IGen::lea_reg_plus_off(*gen, dst, emitter::SP, stack_offset), irec);
  gen->add_instr(IGen::sub_gpr64_gpr64(*gen, dst, emitter::gRegInfo.get_offset_reg()), irec);
}

/////////////////////
// StaticVarAddr
/////////////////////

IR_StaticVarAddr::IR_StaticVarAddr(const RegVal* dest, const StaticObject* src)
    : m_dest(dest), m_src(src) {}

std::string IR_StaticVarAddr::print() {
  return fmt::format("mov-sva {}, {}", m_dest->print(), m_src->print());
}

RegAllocInstr IR_StaticVarAddr::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_StaticVarAddr::do_codegen_x86(emitter::ObjectGenerator* gen,
                                      const AllocationResult& allocs,
                                      emitter::IR_Record irec) {
  auto dr = get_reg(m_dest, allocs, irec);
  auto instr = gen->add_instr(IGen::static_addr(*gen, dr, 0), irec);
  gen->link_instruction_static(instr, m_src->rec, m_src->get_addr_offset());
  gen->add_instr(IGen::sub_gpr64_gpr64(*gen, dr, emitter::gRegInfo.get_offset_reg()), irec);
}

void IR_StaticVarAddr::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                        const AllocationResult& allocs,
                                        emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::static_addr() now dispatches to a real, patchable
  // ARM64 sequence (see IGen::ARM64::static_addr(), which just delegates to load_cross_seg_addr()),
  // and IGen::sub_gpr64_gpr64() already dispatches generically too.
  auto dr = get_reg(m_dest, allocs, irec);
  auto instr = gen->add_instr(IGen::static_addr(*gen, dr, 0), irec);
  gen->link_instruction_static(instr, m_src->rec, m_src->get_addr_offset());
  gen->add_instr(IGen::sub_gpr64_gpr64(*gen, dr, emitter::gRegInfo.get_offset_reg()), irec);
}

/////////////////////
// FunctionAddr
/////////////////////

IR_FunctionAddr::IR_FunctionAddr(const RegVal* dest, FunctionEnv* src) : m_dest(dest), m_src(src) {}

std::string IR_FunctionAddr::print() {
  return fmt::format("mov-fa {}, {}", m_dest->print(), m_src->print());
}

RegAllocInstr IR_FunctionAddr::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_FunctionAddr::do_codegen_x86(emitter::ObjectGenerator* gen,
                                     const AllocationResult& allocs,
                                     emitter::IR_Record irec) {
  auto dr = get_reg(m_dest, allocs, irec);
  auto instr = gen->add_instr(IGen::static_addr(*gen, dr, 0), irec);
  gen->link_instruction_to_function(instr, gen->get_existing_function_record(m_src->idx_in_file));
  gen->add_instr(IGen::sub_gpr64_gpr64(*gen, dr, emitter::gRegInfo.get_offset_reg()), irec);
}

void IR_FunctionAddr::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                       const AllocationResult& allocs,
                                       emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above and to IR_StaticVarAddr::do_codegen_arm64 just above - same
  // deferred cross-segment addressing, just targeting a function record instead of a static object.
  auto dr = get_reg(m_dest, allocs, irec);
  auto instr = gen->add_instr(IGen::static_addr(*gen, dr, 0), irec);
  gen->link_instruction_to_function(instr, gen->get_existing_function_record(m_src->idx_in_file));
  gen->add_instr(IGen::sub_gpr64_gpr64(*gen, dr, emitter::gRegInfo.get_offset_reg()), irec);
}

/////////////////////
// IntegerMath
/////////////////////

IR_IntegerMath::IR_IntegerMath(IntegerMathKind kind, RegVal* dest, RegVal* arg)
    : m_kind(kind), m_dest(dest), m_arg(arg) {}

IR_IntegerMath::IR_IntegerMath(IntegerMathKind kind, RegVal* dest, u8 shift_amount)
    : m_kind(kind), m_dest(dest), m_shift_amount(shift_amount) {}

std::string IR_IntegerMath::print() {
  switch (m_kind) {
    case IntegerMathKind::ADD_64:
      return fmt::format("addi {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::SUB_64:
      return fmt::format("subi {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::IMUL_32:
      return fmt::format("imul {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::IMUL_64:
      return fmt::format("imul64 {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::IDIV_32:
      return fmt::format("idiv {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::UDIV_32:
      return fmt::format("udiv {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::IMOD_32:
      return fmt::format("imod {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::UMOD_32:
      return fmt::format("umod {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::SARV_64:
      return fmt::format("sarv {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::SHLV_64:
      return fmt::format("shlv {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::SHRV_64:
      return fmt::format("shrv {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::SAR_64:
      return fmt::format("sar {}, {}", m_dest->print(), m_shift_amount);
    case IntegerMathKind::SHL_64:
      return fmt::format("shl {}, {}", m_dest->print(), m_shift_amount);
    case IntegerMathKind::SHR_64:
      return fmt::format("shr {}, {}", m_dest->print(), m_shift_amount);
    case IntegerMathKind::AND_64:
      return fmt::format("and {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::OR_64:
      return fmt::format("or {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::XOR_64:
      return fmt::format("xor {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::NOT_64:
      return fmt::format("not {}", m_dest->print());
    default:
      throw std::runtime_error("Unsupported IntegerMathKind");
  }
}

RegAllocInstr IR_IntegerMath::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  rai.read.push_back(m_dest->ireg());

  if (m_kind != IntegerMathKind::NOT_64 && m_kind != IntegerMathKind::SHL_64 &&
      m_kind != IntegerMathKind::SAR_64 && m_kind != IntegerMathKind::SHR_64) {
    rai.read.push_back(m_arg->ireg());
  }

  if (m_kind == IntegerMathKind::IDIV_32 || m_kind == IntegerMathKind::IMOD_32 ||
      m_kind == IntegerMathKind::UDIV_32 || m_kind == IntegerMathKind::UMOD_32) {
    rai.exclude.emplace_back(emitter::RDX);
  }

  if (m_kind == IntegerMathKind::IMOD_32 || m_kind == IntegerMathKind::UMOD_32) {
    // ARM64's do_codegen_arm64 needs a 3rd scratch register here for the quotient (sdiv/udiv's
    // result), distinct from both the dividend and the divisor, since a remainder needs all three
    // values live at once for the closing msub - see do_codegen_arm64 below. x86's mod codegen
    // has no use for this (the quotient already lands in the discarded RDX via cdq+idiv), so this
    // clobber is a harmless no-op there. gRegInfo is already switched to whichever architecture is
    // active for this compile (see Compiler's constructor), so reading a fixed slot out of it here
    // - same trick IR_FunctionCall::to_rai() uses for its clobber-every-temp-register loop -
    // already gives the right answer for either architecture without to_rai() needing to know
    // which one it is.
    rai.clobber.push_back(emitter::gRegInfo.get_gpr_temp_alloc_order().back());
  }
  return rai;
}

void IR_IntegerMath::do_codegen_x86(emitter::ObjectGenerator* gen,
                                    const AllocationResult& allocs,
                                    emitter::IR_Record irec) {
  switch (m_kind) {
    case IntegerMathKind::ADD_64:
      gen->add_instr(
          IGen::add_gpr64_gpr64(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::SUB_64:
      gen->add_instr(
          IGen::sub_gpr64_gpr64(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::AND_64:
      gen->add_instr(
          IGen::and_gpr64_gpr64(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::OR_64:
      gen->add_instr(
          IGen::or_gpr64_gpr64(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::XOR_64:
      gen->add_instr(
          IGen::xor_gpr64_gpr64(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::NOT_64:
      gen->add_instr(IGen::not_gpr64(*gen, get_reg(m_dest, allocs, irec)), irec);
      ASSERT(!m_arg);
      break;
    case IntegerMathKind::SHLV_64:
      // TODO ARM - register provided but unused on x86
      gen->add_instr(IGen::shl_gpr64_reg(*gen, get_reg(m_dest, allocs, irec), 0), irec);
      // TODO ARM - x86 forces you to use CL, which is dumb, but the register allocator
      // has that logic baked in somewhere
      // ARM has no such constraint, so we should be able to use any register for the shift amount
      ASSERT(get_reg(m_arg, allocs, irec) == emitter::RCX);
      break;
    case IntegerMathKind::SHRV_64:
      // TODO ARM - register provided but unused on x86
      gen->add_instr(IGen::shr_gpr64_reg(*gen, get_reg(m_dest, allocs, irec), 0), irec);
      ASSERT(get_reg(m_arg, allocs, irec) == emitter::RCX);
      break;
    case IntegerMathKind::SARV_64:
      // TODO ARM - register provided but unused on x86
      gen->add_instr(IGen::sar_gpr64_reg(*gen, get_reg(m_dest, allocs, irec), 0), irec);
      ASSERT(get_reg(m_arg, allocs, irec) == emitter::RCX);
      break;
    case IntegerMathKind::SHL_64:
      gen->add_instr(IGen::shl_gpr64_u8(*gen, get_reg(m_dest, allocs, irec), m_shift_amount), irec);
      break;
    case IntegerMathKind::SHR_64:
      gen->add_instr(IGen::shr_gpr64_u8(*gen, get_reg(m_dest, allocs, irec), m_shift_amount), irec);
      break;
    case IntegerMathKind::SAR_64:
      gen->add_instr(IGen::sar_gpr64_u8(*gen, get_reg(m_dest, allocs, irec), m_shift_amount), irec);
      break;
    case IntegerMathKind::IMUL_32: {
      // just a 32-bit multiply, signed/unsigned doesn't affect lower 32 bits of result.
      auto dr = get_reg(m_dest, allocs, irec);
      gen->add_instr(IGen::imul_gpr32_gpr32(*gen, dr, get_reg(m_arg, allocs, irec)), irec);
      // the PS2 sign extends the result even if we used multu. We replicate this here.
      gen->add_instr(IGen::movsx_r64_r32(*gen, dr, dr), irec);
    } break;
    case IntegerMathKind::IMUL_64: {
      auto dr = get_reg(m_dest, allocs, irec);
      gen->add_instr(IGen::imul_gpr64_gpr64(*gen, dr, get_reg(m_arg, allocs, irec)), irec);
    } break;
    case IntegerMathKind::IDIV_32: {
      gen->add_instr(IGen::cdq(*gen), irec);
      gen->add_instr(IGen::idiv_gpr32(*gen, get_reg(m_arg, allocs, irec)), irec);
      gen->add_instr(IGen::movsx_r64_r32(*gen, get_reg(m_dest, allocs, irec), emitter::RAX), irec);
    } break;
    case IntegerMathKind::UDIV_32: {
      // zero extend, not sign extend to avoid overflow
      gen->add_instr(IGen::xor_gpr64_gpr64(*gen, Register(RDX), Register(RDX)), irec);
      gen->add_instr(IGen::unsigned_div_gpr32(*gen, get_reg(m_arg, allocs, irec)), irec);
      // note: this probably needs hardware testing to know for sure if the PS2 actually sign
      // extends here or not. Nothing seems to break either way, and PCSX2/Dobie interpreters both
      // sign extend, so that seems like the safest option.
      gen->add_instr(IGen::movsx_r64_r32(*gen, get_reg(m_dest, allocs, irec), emitter::RAX), irec);
    } break;
    case IntegerMathKind::IMOD_32: {
      gen->add_instr(IGen::cdq(*gen), irec);
      gen->add_instr(IGen::idiv_gpr32(*gen, get_reg(m_arg, allocs, irec)), irec);
      gen->add_instr(IGen::movsx_r64_r32(*gen, get_reg(m_dest, allocs, irec), emitter::RDX), irec);
    } break;
    case IntegerMathKind::UMOD_32: {
      // zero extend, not sign extend to avoid overflow
      gen->add_instr(IGen::xor_gpr64_gpr64(*gen, Register(RDX), Register(RDX)), irec);
      gen->add_instr(IGen::unsigned_div_gpr32(*gen, get_reg(m_arg, allocs, irec)), irec);
      // see note on udiv, same applies here.
      gen->add_instr(IGen::movsx_r64_r32(*gen, get_reg(m_dest, allocs, irec), emitter::RDX), irec);
    } break;
    default:
      ASSERT(false);
  }
}

void IR_IntegerMath::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                      const AllocationResult& allocs,
                                      emitter::IR_Record irec) {
  // ADD_64/SUB_64/AND_64/OR_64/XOR_64/NOT_64/IMUL_32/IMUL_64/SHL_64/SAR_64/SHR_64/SHLV_64/
  // SARV_64/SHRV_64 are plain ports: ARM64's versions of these all take arbitrary registers, no
  // implicit-register conventions to preserve (see IGenARM64.cpp). IDIV_32/UDIV_32/IMOD_32/
  // UMOD_32 are NOT ports of the x86 code below - see the development notes ("IR_IntegerMath needs a
  // real design decision, not a port") for why: ARM64's sdiv/udiv take an arbitrary destination
  // and two arbitrary source registers (no RAX/RDX pairing at all), so they go through the
  // dedicated 3-operand IGen::ARM64::sdiv_gpr32_gpr32_gpr32()/udiv_.../msub_...() helpers instead
  // of the x86-shaped, single-operand IGen::idiv_gpr32()/unsigned_div_gpr32()/cdq() used below.
  switch (m_kind) {
    case IntegerMathKind::ADD_64:
      gen->add_instr(
          IGen::add_gpr64_gpr64(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::SUB_64:
      gen->add_instr(
          IGen::sub_gpr64_gpr64(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::AND_64:
      gen->add_instr(
          IGen::and_gpr64_gpr64(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::OR_64:
      gen->add_instr(
          IGen::or_gpr64_gpr64(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::XOR_64:
      gen->add_instr(
          IGen::xor_gpr64_gpr64(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::NOT_64:
      gen->add_instr(IGen::not_gpr64(*gen, get_reg(m_dest, allocs, irec)), irec);
      ASSERT(!m_arg);
      break;
    case IntegerMathKind::SHLV_64:
      gen->add_instr(
          IGen::shl_gpr64_reg(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::SHRV_64:
      gen->add_instr(
          IGen::shr_gpr64_reg(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::SARV_64:
      gen->add_instr(
          IGen::sar_gpr64_reg(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case IntegerMathKind::SHL_64:
      gen->add_instr(IGen::shl_gpr64_u8(*gen, get_reg(m_dest, allocs, irec), m_shift_amount), irec);
      break;
    case IntegerMathKind::SHR_64:
      gen->add_instr(IGen::shr_gpr64_u8(*gen, get_reg(m_dest, allocs, irec), m_shift_amount), irec);
      break;
    case IntegerMathKind::SAR_64:
      gen->add_instr(IGen::sar_gpr64_u8(*gen, get_reg(m_dest, allocs, irec), m_shift_amount), irec);
      break;
    case IntegerMathKind::IMUL_32: {
      // just a 32-bit multiply, signed/unsigned doesn't affect lower 32 bits of result.
      auto dr = get_reg(m_dest, allocs, irec);
      gen->add_instr(IGen::imul_gpr32_gpr32(*gen, dr, get_reg(m_arg, allocs, irec)), irec);
      // the PS2 sign extends the result even if we used multu. We replicate this here.
      gen->add_instr(IGen::movsx_r64_r32(*gen, dr, dr), irec);
    } break;
    case IntegerMathKind::IMUL_64: {
      auto dr = get_reg(m_dest, allocs, irec);
      gen->add_instr(IGen::imul_gpr64_gpr64(*gen, dr, get_reg(m_arg, allocs, irec)), irec);
    } break;
    case IntegerMathKind::IDIV_32: {
      auto dr = get_reg(m_dest, allocs, irec);
      auto ar = get_reg(m_arg, allocs, irec);
      gen->add_instr(Instruction(IGen::ARM64::sdiv_gpr32_gpr32_gpr32(dr, dr, ar)), irec);
      // same PS2 sign-extension replication as IMUL_32/x86's IDIV_32 above.
      gen->add_instr(IGen::movsx_r64_r32(*gen, dr, dr), irec);
    } break;
    case IntegerMathKind::UDIV_32: {
      auto dr = get_reg(m_dest, allocs, irec);
      auto ar = get_reg(m_arg, allocs, irec);
      gen->add_instr(Instruction(IGen::ARM64::udiv_gpr32_gpr32_gpr32(dr, dr, ar)), irec);
      // x86's UDIV_32 also sign-extends despite being unsigned (see its comment above) - matching
      // that PS2-emulation choice here rather than picking a new behavior for ARM64.
      gen->add_instr(IGen::movsx_r64_r32(*gen, dr, dr), irec);
    } break;
    case IntegerMathKind::IMOD_32: {
      // n (dividend) stays live in dr throughout - msub below needs it alongside the quotient and
      // the divisor, so unlike IDIV_32 this can't compute the quotient in place into dr.
      auto dr = get_reg(m_dest, allocs, irec);
      auto ar = get_reg(m_arg, allocs, irec);
      auto scratch = emitter::gRegInfo.get_gpr_temp_alloc_order().back();
      gen->add_instr(Instruction(IGen::ARM64::sdiv_gpr32_gpr32_gpr32(scratch, dr, ar)), irec);
      // dst = dr(a=n) - scratch(quotient) * ar(m)
      gen->add_instr(Instruction(IGen::ARM64::msub_gpr32_gpr32_gpr32_gpr32(dr, scratch, ar, dr)),
                    irec);
      gen->add_instr(IGen::movsx_r64_r32(*gen, dr, dr), irec);
    } break;
    case IntegerMathKind::UMOD_32: {
      auto dr = get_reg(m_dest, allocs, irec);
      auto ar = get_reg(m_arg, allocs, irec);
      auto scratch = emitter::gRegInfo.get_gpr_temp_alloc_order().back();
      gen->add_instr(Instruction(IGen::ARM64::udiv_gpr32_gpr32_gpr32(scratch, dr, ar)), irec);
      gen->add_instr(Instruction(IGen::ARM64::msub_gpr32_gpr32_gpr32_gpr32(dr, scratch, ar, dr)),
                    irec);
      gen->add_instr(IGen::movsx_r64_r32(*gen, dr, dr), irec);
    } break;
    default:
      ASSERT(false);
  }
}

/////////////////////
// FloatMath
/////////////////////

IR_FloatMath::IR_FloatMath(FloatMathKind kind, RegVal* dest, RegVal* arg)
    : m_kind(kind), m_dest(dest), m_arg(arg) {}

std::string IR_FloatMath::print() {
  switch (m_kind) {
    case FloatMathKind::DIV_SS:
      return fmt::format("divss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::MUL_SS:
      return fmt::format("mulss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::ADD_SS:
      return fmt::format("addss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::SUB_SS:
      return fmt::format("subss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::MAX_SS:
      return fmt::format("maxss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::MIN_SS:
      return fmt::format("minss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::SQRT_SS:
      return fmt::format("sqrtss {}, {}", m_dest->print(), m_arg->print());
    default:
      throw std::runtime_error("Unsupported FloatMathKind");
  }
}

RegAllocInstr IR_FloatMath::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  if (m_kind != FloatMathKind::SQRT_SS) {
    rai.read.push_back(m_dest->ireg());
  }
  rai.read.push_back(m_arg->ireg());
  return rai;
}

void IR_FloatMath::do_codegen_x86(emitter::ObjectGenerator* gen,
                                  const AllocationResult& allocs,
                                  emitter::IR_Record irec) {
  switch (m_kind) {
    case FloatMathKind::DIV_SS:
      gen->add_instr(
          IGen::div_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::MUL_SS:
      gen->add_instr(
          IGen::mul_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::ADD_SS:
      gen->add_instr(
          IGen::add_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::SUB_SS:
      gen->add_instr(
          IGen::sub_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::MAX_SS:
      gen->add_instr(
          IGen::max_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::MIN_SS:
      gen->add_instr(
          IGen::min_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::SQRT_SS:
      gen->add_instr(
          IGen::sqrt_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    default:
      ASSERT(false);
  }
}

void IR_FloatMath::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                    const AllocationResult& allocs,
                                    emitter::IR_Record irec) {
  // identical to do_codegen_x86 above - IGen::*_f32_f32/sqrt_f32 already dispatch to real
  // IGenARM64.cpp encodings with the same (dst, src) convention.
  switch (m_kind) {
    case FloatMathKind::DIV_SS:
      gen->add_instr(
          IGen::div_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::MUL_SS:
      gen->add_instr(
          IGen::mul_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::ADD_SS:
      gen->add_instr(
          IGen::add_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::SUB_SS:
      gen->add_instr(
          IGen::sub_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::MAX_SS:
      gen->add_instr(
          IGen::max_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::MIN_SS:
      gen->add_instr(
          IGen::min_f32_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
          irec);
      break;
    case FloatMathKind::SQRT_SS:
      gen->add_instr(
          IGen::sqrt_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    default:
      ASSERT(false);
  }
}

/////////////////////
// StaticVarLoad
/////////////////////

IR_StaticVarLoad::IR_StaticVarLoad(const RegVal* dest, const StaticObject* src)
    : m_dest(dest), m_src(src) {}

std::string IR_StaticVarLoad::print() {
  return fmt::format("mov-svl {}, [{}]", m_dest->print(), m_src->print());
}

RegAllocInstr IR_StaticVarLoad::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_StaticVarLoad::do_codegen_x86(emitter::ObjectGenerator* gen,
                                      const AllocationResult& allocs,
                                      emitter::IR_Record irec) {
  auto load_info = m_src->get_load_info();
  ASSERT(m_src->get_addr_offset() == 0);

  if (m_dest->ireg().reg_class == RegClass::FLOAT) {
    ASSERT(load_info.load_signed == false);
    ASSERT(load_info.load_size == 4);
    ASSERT(load_info.requires_load == true);

    auto instr =
        gen->add_instr(IGen::static_load_f32(*gen, get_reg(m_dest, allocs, irec), 0), irec);
    gen->link_instruction_static(instr, m_src->rec, 0);
  } else if (m_dest->ireg().reg_class == RegClass::VECTOR_FLOAT) {
    // we don't check the load info intentionally because we want to allow loading an entire
    // vector structure.
    auto instr =
        gen->add_instr(IGen::loadvf_rip_plus_s32(*gen, get_reg(m_dest, allocs, irec), 0), irec);
    gen->link_instruction_static(instr, m_src->rec, 0);
  } else {
    ASSERT(false);
  }
}

void IR_StaticVarLoad::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                        const AllocationResult& allocs,
                                        emitter::IR_Record irec) {
  auto load_info = m_src->get_load_info();
  ASSERT(m_src->get_addr_offset() == 0);

  // X17 is reserved and never allocated to a GOAL virtual register (see
  // RegisterInfo::make_arm64_register_info()) - used here purely as scratch to hold the resolved
  // cross-segment address before the real (float/vector) load from it. m_dest itself can't hold
  // this address directly: it's a FLOAT/VECTOR_FLOAT-class register, but
  // IGen::ARM64::load_cross_seg_addr() needs a GPR.
  Register addr_reg(X17);

  if (m_dest->ireg().reg_class == RegClass::FLOAT) {
    ASSERT(load_info.load_signed == false);
    ASSERT(load_info.load_size == 4);
    ASSERT(load_info.requires_load == true);

    auto instr = gen->add_instr(Instruction(IGen::ARM64::load_cross_seg_addr(addr_reg)), irec);
    gen->link_instruction_static(instr, m_src->rec, 0);
    gen->add_instr(IGen::load_reg_offset_xmm32(*gen, get_reg(m_dest, allocs, irec), addr_reg, 0),
                   irec);
  } else if (m_dest->ireg().reg_class == RegClass::VECTOR_FLOAT) {
    // we don't check the load info intentionally because we want to allow loading an entire
    // vector structure.
    auto instr = gen->add_instr(Instruction(IGen::ARM64::load_cross_seg_addr(addr_reg)), irec);
    gen->link_instruction_static(instr, m_src->rec, 0);
    gen->add_instr(
        IGen::load128_xmm128_reg_offset(*gen, get_reg(m_dest, allocs, irec), addr_reg, 0), irec);
  } else {
    ASSERT(false);
  }
}

/////////////////////
// ConditionalBranch
/////////////////////

std::string Condition::print() const {
  switch (kind) {
    case ConditionKind::NOT_EQUAL:
      return a->print() + " != " + b->print();
    case ConditionKind::EQUAL:
      return a->print() + " == " + b->print();
    case ConditionKind::LEQ:
      return a->print() + " <= " + b->print();
    case ConditionKind::GEQ:
      return a->print() + " >= " + b->print();
    case ConditionKind::LT:
      return a->print() + " < " + b->print();
    case ConditionKind::GT:
      return a->print() + " > " + b->print();
    default:
      throw std::runtime_error("unknown condition type in GoalCondition::print()");
  }
}

RegAllocInstr Condition::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(a->ireg());
  rai.read.push_back(b->ireg());
  return rai;
}

IR_ConditionalBranch::IR_ConditionalBranch(const Condition& _condition, Label _label)
    : condition(_condition), label(_label) {}

std::string IR_ConditionalBranch::print() {
  // todo, float/signed info?
  return fmt::format("j({}) {}", condition.print(), label.print());
}

RegAllocInstr IR_ConditionalBranch::to_rai() {
  auto rai = condition.to_rai();
  ASSERT(m_resolved);
  rai.jumps.push_back(label.idx);
  return rai;
}

void IR_ConditionalBranch::do_codegen_x86(emitter::ObjectGenerator* gen,
                                          const AllocationResult& allocs,
                                          emitter::IR_Record irec) {
  Instruction jump_instr = InstructionX86(0);
  ASSERT(m_resolved);
  switch (condition.kind) {
    case ConditionKind::EQUAL:
      jump_instr = IGen::je_imm(*gen);
      break;
    case ConditionKind::NOT_EQUAL:
      jump_instr = IGen::jne_imm(*gen);
      break;
    case ConditionKind::LEQ:
      if (condition.is_signed) {
        jump_instr = IGen::jle_imm(*gen);
      } else {
        jump_instr = IGen::jbe_imm(*gen);
      }
      break;
    case ConditionKind::GEQ:
      if (condition.is_signed) {
        jump_instr = IGen::jge_imm(*gen);
      } else {
        jump_instr = IGen::jae_imm(*gen);
      }
      break;

    case ConditionKind::LT:
      if (condition.is_signed) {
        jump_instr = IGen::jl_imm(*gen);
      } else {
        jump_instr = IGen::jb_imm(*gen);
      }
      break;
    case ConditionKind::GT:
      if (condition.is_signed) {
        jump_instr = IGen::jg_imm(*gen);
      } else {
        jump_instr = IGen::ja_imm(*gen);
      }
      break;
    default:
      ASSERT(false);
  }

  if (condition.is_float) {
    gen->add_instr(IGen::cmp_f32_f32(*gen, get_reg(condition.a, allocs, irec),
                                     get_reg(condition.b, allocs, irec)),
                   irec);
  } else {
    gen->add_instr(IGen::cmp_gpr64_gpr64(*gen, get_reg(condition.a, allocs, irec),
                                         get_reg(condition.b, allocs, irec)),
                   irec);
  }

  auto jump_rec = gen->add_instr(jump_instr, irec);
  gen->link_instruction_jump(jump_rec, gen->get_future_ir_record_in_same_func(irec, label.idx));
}

void IR_ConditionalBranch::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                            const AllocationResult& allocs,
                                            emitter::IR_Record irec) {
  // Identical structure to do_codegen_x86 above - IGen::je_imm() etc. already dispatch to the real
  // ARM64 B.<cond> encodings (IGenARM64.cpp), and ObjectGenerator::link_instruction_jump() /
  // handle_temp_jump_links() now know how to patch an ARM64 branch's word-packed, PC-relative
  // immediate (see InstructionARM64::branch_imm_width() and the development notes).
  Instruction jump_instr = InstructionARM64(0);
  ASSERT(m_resolved);
  switch (condition.kind) {
    case ConditionKind::EQUAL:
      jump_instr = IGen::je_imm(*gen);
      break;
    case ConditionKind::NOT_EQUAL:
      jump_instr = IGen::jne_imm(*gen);
      break;
    case ConditionKind::LEQ:
      if (condition.is_signed) {
        jump_instr = IGen::jle_imm(*gen);
      } else {
        jump_instr = IGen::jbe_imm(*gen);
      }
      break;
    case ConditionKind::GEQ:
      if (condition.is_signed) {
        jump_instr = IGen::jge_imm(*gen);
      } else {
        jump_instr = IGen::jae_imm(*gen);
      }
      break;

    case ConditionKind::LT:
      if (condition.is_signed) {
        jump_instr = IGen::jl_imm(*gen);
      } else {
        jump_instr = IGen::jb_imm(*gen);
      }
      break;
    case ConditionKind::GT:
      if (condition.is_signed) {
        jump_instr = IGen::jg_imm(*gen);
      } else {
        jump_instr = IGen::ja_imm(*gen);
      }
      break;
    default:
      ASSERT(false);
  }

  if (condition.is_float) {
    gen->add_instr(IGen::cmp_f32_f32(*gen, get_reg(condition.a, allocs, irec),
                                     get_reg(condition.b, allocs, irec)),
                   irec);
  } else {
    gen->add_instr(IGen::cmp_gpr64_gpr64(*gen, get_reg(condition.a, allocs, irec),
                                         get_reg(condition.b, allocs, irec)),
                   irec);
  }

  auto jump_rec = gen->add_instr(jump_instr, irec);
  gen->link_instruction_jump(jump_rec, gen->get_future_ir_record_in_same_func(irec, label.idx));
}

/////////////////////
// LoadConstantOffset
/////////////////////

IR_LoadConstOffset::IR_LoadConstOffset(const RegVal* dest,
                                       int offset,
                                       const RegVal* base,
                                       MemLoadInfo info,
                                       bool use_coloring)
    : IR_Asm(use_coloring), m_dest(dest), m_offset(offset), m_base(base), m_info(info) {}

std::string IR_LoadConstOffset::print() {
  return fmt::format("mov {}, [{} + {}]", m_dest->print(), m_base->print(), m_offset);
}

RegAllocInstr IR_LoadConstOffset::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  rai.read.push_back(m_base->ireg());
  return rai;
}

void IR_LoadConstOffset::do_codegen_x86(emitter::ObjectGenerator* gen,
                                        const AllocationResult& allocs,
                                        emitter::IR_Record irec) {
  auto dest_reg = m_use_coloring ? get_reg(m_dest, allocs, irec) : get_no_color_reg(m_dest);
  auto base_reg = m_use_coloring ? get_reg(m_base, allocs, irec) : get_no_color_reg(m_base);

  if (m_dest->ireg().reg_class == RegClass::GPR_64) {
    gen->add_instr(IGen::load_goal_gpr(*gen, dest_reg, base_reg, emitter::gRegInfo.get_offset_reg(),
                                       m_offset, m_info.size, m_info.sign_extend),
                   irec);
  } else if (m_dest->ireg().reg_class == RegClass::FLOAT && m_info.size == 4 &&
             m_info.sign_extend == false && m_info.reg == RegClass::FLOAT) {
    gen->add_instr(IGen::load_goal_xmm32(*gen, dest_reg, base_reg,
                                         emitter::gRegInfo.get_offset_reg(), m_offset),
                   irec);
  } else if ((m_dest->ireg().reg_class == RegClass::VECTOR_FLOAT ||
              m_dest->ireg().reg_class == RegClass::INT_128) &&
             m_info.size == 16 && m_info.sign_extend == false &&
             m_info.reg == m_dest->ireg().reg_class) {
    gen->add_instr(IGen::load_goal_xmm128(*gen, dest_reg, base_reg,
                                          emitter::gRegInfo.get_offset_reg(), m_offset),
                   irec);
  } else {
    throw std::runtime_error("IR_LoadConstOffset::do_codegen_x86 not supported");
  }
}

void IR_LoadConstOffset::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                          const AllocationResult& allocs,
                                          emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::load_goal_gpr()/load_goal_xmm32()/load_goal_xmm128()
  // already have real, complete ARM64 implementations (all just compile-time-known-offset GPR
  // arithmetic, no linker patching involved), just never wired up to this node.
  auto dest_reg = m_use_coloring ? get_reg(m_dest, allocs, irec) : get_no_color_reg(m_dest);
  auto base_reg = m_use_coloring ? get_reg(m_base, allocs, irec) : get_no_color_reg(m_base);

  if (m_dest->ireg().reg_class == RegClass::GPR_64) {
    gen->add_instr(IGen::load_goal_gpr(*gen, dest_reg, base_reg, emitter::gRegInfo.get_offset_reg(),
                                       m_offset, m_info.size, m_info.sign_extend),
                   irec);
  } else if (m_dest->ireg().reg_class == RegClass::FLOAT && m_info.size == 4 &&
             m_info.sign_extend == false && m_info.reg == RegClass::FLOAT) {
    gen->add_instr(IGen::load_goal_xmm32(*gen, dest_reg, base_reg,
                                         emitter::gRegInfo.get_offset_reg(), m_offset),
                   irec);
  } else if ((m_dest->ireg().reg_class == RegClass::VECTOR_FLOAT ||
              m_dest->ireg().reg_class == RegClass::INT_128) &&
             m_info.size == 16 && m_info.sign_extend == false &&
             m_info.reg == m_dest->ireg().reg_class) {
    gen->add_instr(IGen::load_goal_xmm128(*gen, dest_reg, base_reg,
                                          emitter::gRegInfo.get_offset_reg(), m_offset),
                   irec);
  } else {
    throw std::runtime_error("IR_LoadConstOffset::do_codegen_arm64 not supported");
  }
}

///////////////////////
// StoreConstantOffset
///////////////////////
IR_StoreConstOffset::IR_StoreConstOffset(const RegVal* value,
                                         int offset,
                                         const RegVal* base,
                                         int size,
                                         bool use_coloring)
    : IR_Asm(use_coloring), m_value(value), m_offset(offset), m_base(base), m_size(size) {}

std::string IR_StoreConstOffset::print() {
  return fmt::format("move [{} + {}], {}", m_base->print(), m_offset, m_value->print());
}

RegAllocInstr IR_StoreConstOffset::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(m_value->ireg());
  rai.read.push_back(m_base->ireg());
  return rai;
}

void IR_StoreConstOffset::do_codegen_x86(emitter::ObjectGenerator* gen,
                                         const AllocationResult& allocs,
                                         emitter::IR_Record irec) {
  auto base_reg = m_use_coloring ? get_reg(m_base, allocs, irec) : get_no_color_reg(m_base);
  auto value_reg = m_use_coloring ? get_reg(m_value, allocs, irec) : get_no_color_reg(m_value);

  if (m_value->ireg().reg_class == RegClass::GPR_64) {
    gen->add_instr(IGen::store_goal_gpr(*gen, base_reg, value_reg,
                                        emitter::gRegInfo.get_offset_reg(), m_offset, m_size),
                   irec);
  } else if (m_value->ireg().reg_class == RegClass::FLOAT && m_size == 4) {
    gen->add_instr(IGen::store_goal_xmm32(*gen, base_reg, value_reg,
                                          emitter::gRegInfo.get_offset_reg(), m_offset),
                   irec);
  } else if ((m_value->ireg().reg_class == RegClass::VECTOR_FLOAT ||
              m_value->ireg().reg_class == RegClass::INT_128) &&
             m_size == 16) {
    gen->add_instr(IGen::store_goal_vf(*gen, base_reg, value_reg,
                                       emitter::gRegInfo.get_offset_reg(), m_offset),
                   irec);
  } else {
    throw std::runtime_error(
        fmt::format("IR_StoreConstOffset::do_codegen_x86 can't handle this (c {} sz {})",
                    fmt::underlying(m_value->ireg().reg_class), m_size));
  }
}

void IR_StoreConstOffset::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                           const AllocationResult& allocs,
                                           emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::store_goal_gpr()/store_goal_xmm32()/store_goal_vf()
  // already have real, complete ARM64 implementations, just never wired up to this node.
  auto base_reg = m_use_coloring ? get_reg(m_base, allocs, irec) : get_no_color_reg(m_base);
  auto value_reg = m_use_coloring ? get_reg(m_value, allocs, irec) : get_no_color_reg(m_value);

  if (m_value->ireg().reg_class == RegClass::GPR_64) {
    gen->add_instr(IGen::store_goal_gpr(*gen, base_reg, value_reg,
                                        emitter::gRegInfo.get_offset_reg(), m_offset, m_size),
                   irec);
  } else if (m_value->ireg().reg_class == RegClass::FLOAT && m_size == 4) {
    gen->add_instr(IGen::store_goal_xmm32(*gen, base_reg, value_reg,
                                          emitter::gRegInfo.get_offset_reg(), m_offset),
                   irec);
  } else if ((m_value->ireg().reg_class == RegClass::VECTOR_FLOAT ||
              m_value->ireg().reg_class == RegClass::INT_128) &&
             m_size == 16) {
    gen->add_instr(IGen::store_goal_vf(*gen, base_reg, value_reg,
                                       emitter::gRegInfo.get_offset_reg(), m_offset),
                   irec);
  } else {
    throw std::runtime_error(
        fmt::format("IR_StoreConstOffset::do_codegen_arm64 can't handle this (c {} sz {})",
                    fmt::underlying(m_value->ireg().reg_class), m_size));
  }
}

///////////////////////
// Null
///////////////////////
std::string IR_Null::print() {
  return "null";
}

RegAllocInstr IR_Null::to_rai() {
  return {};
}

void IR_Null::do_codegen_x86(emitter::ObjectGenerator* gen,
                             const AllocationResult& allocs,
                             emitter::IR_Record irec) {
  (void)gen;
  (void)allocs;
  (void)irec;
}

void IR_Null::do_codegen_arm64(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  (void)gen;
  (void)allocs;
  (void)irec;
}

///////////////////////
// ValueReset
///////////////////////
IR_ValueReset::IR_ValueReset(std::vector<RegVal*> args) : m_args(std::move(args)) {}

std::string IR_ValueReset::print() {
  return "value-reset";
}

RegAllocInstr IR_ValueReset::to_rai() {
  RegAllocInstr rai;
  for (auto& x : m_args) {
    rai.write.push_back(x->ireg());
  }
  return rai;
}

void IR_ValueReset::do_codegen_x86(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  (void)gen;
  (void)allocs;
  (void)irec;
}

void IR_ValueReset::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                     const AllocationResult& allocs,
                                     emitter::IR_Record irec) {
  // same as do_codegen_x86 above: this IR exists purely for register-allocator bookkeeping
  // (to_rai() marks these as freshly written), it emits no real instructions on any arch.
  (void)gen;
  (void)allocs;
  (void)irec;
}

///////////////////////
// FloatToInt
///////////////////////

IR_FloatToInt::IR_FloatToInt(const RegVal* dest, const RegVal* src) : m_dest(dest), m_src(src) {}

std::string IR_FloatToInt::print() {
  return fmt::format("f2i {}, {}", m_dest->print(), m_src->print());
}

RegAllocInstr IR_FloatToInt::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(m_src->ireg());
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_FloatToInt::do_codegen_x86(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  gen->add_instr(
      IGen::f32_to_int32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_src, allocs, irec)), irec);
  gen->add_instr(
      IGen::movsx_r64_r32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_dest, allocs, irec)),
      irec);
}

void IR_FloatToInt::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                     const AllocationResult& allocs,
                                     emitter::IR_Record irec) {
  gen->add_instr(
      IGen::f32_to_int32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_src, allocs, irec)), irec);
  gen->add_instr(
      IGen::movsx_r64_r32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_dest, allocs, irec)),
      irec);
}

///////////////////////
// IntToFloat
///////////////////////

IR_IntToFloat::IR_IntToFloat(const RegVal* dest, const RegVal* src) : m_dest(dest), m_src(src) {}

std::string IR_IntToFloat::print() {
  return fmt::format("i2f {}, {}", m_dest->print(), m_src->print());
}

RegAllocInstr IR_IntToFloat::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(m_src->ireg());
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_IntToFloat::do_codegen_x86(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  gen->add_instr(
      IGen::int32_to_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_src, allocs, irec)), irec);
}

void IR_IntToFloat::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                     const AllocationResult& allocs,
                                     emitter::IR_Record irec) {
  gen->add_instr(
      IGen::int32_to_f32(*gen, get_reg(m_dest, allocs, irec), get_reg(m_src, allocs, irec)), irec);
}

///////////////////////
// GetStackAddr
///////////////////////

IR_GetStackAddr::IR_GetStackAddr(const RegVal* dest, int slot) : m_dest(dest), m_slot(slot) {}

std::string IR_GetStackAddr::print() {
  return fmt::format("mov {}, stack-slot-{}", m_dest->print(), m_slot);
}

RegAllocInstr IR_GetStackAddr::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_GetStackAddr::do_codegen_x86(emitter::ObjectGenerator* gen,
                                     const AllocationResult& allocs,
                                     emitter::IR_Record irec) {
  auto dest_reg = get_reg(m_dest, allocs, irec);
  int offset = GPR_SIZE * allocs.get_slot_for_var(m_slot);

  if (offset == 0) {
    gen->add_instr(IGen::mov_gpr64_gpr64(*gen, dest_reg, RSP), irec);
    gen->add_instr(IGen::sub_gpr64_gpr64(*gen, dest_reg, gRegInfo.get_offset_reg()), irec);
  } else {
    // dest = offset + RSP
    gen->add_instr(IGen::lea_reg_plus_off(*gen, dest_reg, RSP, offset), irec);
    // dest = offset + RSP - offset
    gen->add_instr(IGen::sub_gpr64_gpr64(*gen, dest_reg, gRegInfo.get_offset_reg()), irec);
  }
}

void IR_GetStackAddr::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                       const AllocationResult& allocs,
                                       emitter::IR_Record irec) {
  auto dest_reg = get_reg(m_dest, allocs, irec);
  int offset = GPR_SIZE * allocs.get_slot_for_var(m_slot);

  // NOTE: unlike the x86 version above, this doesn't special-case offset==0 with a plain
  // mov_gpr64_gpr64(dest_reg, SP) - ARM64's "MOV (register)" is an alias for ORR with the zero
  // register (XZR) in the Rm slot, which does NOT accept SP there (register field 31 means XZR
  // for this encoding, not SP, unlike the ADD/SUB-immediate family lea_reg_plus_off uses). Always
  // go through lea_reg_plus_off, which is SP-safe for any offset including 0.
  gen->add_instr(IGen::lea_reg_plus_off(*gen, dest_reg, SP, offset), irec);
  gen->add_instr(IGen::sub_gpr64_gpr64(*gen, dest_reg, gRegInfo.get_offset_reg()), irec);
}

///////////////////////
// Nop
///////////////////////

IR_Nop::IR_Nop() {}

std::string IR_Nop::print() {
  return fmt::format("nop");
}

RegAllocInstr IR_Nop::to_rai() {
  return {};
}

void IR_Nop::do_codegen_x86(emitter::ObjectGenerator* gen,
                            const AllocationResult&,
                            emitter::IR_Record irec) {
  gen->add_instr(IGen::nop(*gen), irec);
}

void IR_Nop::do_codegen_arm64(emitter::ObjectGenerator* gen,
                              const AllocationResult&,
                              emitter::IR_Record irec) {
  gen->add_instr(IGen::nop(*gen), irec);
}

///////////////////////
// Asm
///////////////////////

IR_Asm::IR_Asm(bool use_coloring) : m_use_coloring(use_coloring) {}

std::string IR_Asm::get_color_suffix_string() {
  if (m_use_coloring) {
    return "";
  } else {
    return " :no-color";
  }
}

///////////////////////
// AsmRet
///////////////////////

IR_AsmRet::IR_AsmRet(bool use_coloring) : IR_Asm(use_coloring) {}

std::string IR_AsmRet::print() {
  return fmt::format(".ret{}", get_color_suffix_string());
}

RegAllocInstr IR_AsmRet::to_rai() {
  return {};
}

void IR_AsmRet::do_codegen_x86(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  (void)allocs;
  gen->add_instr(IGen::ret(*gen), irec);
}

void IR_AsmRet::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  (void)allocs;
  // Pop the top of the stack into X30 (LR) before the real RET, simulating x86's `ret` (which
  // pops the return address off the stack itself). See the big comment on
  // CodeGenerator::do_asm_function_arm64 for why: that function pushes X30 as its very first
  // instruction specifically so this pop has a matching, balanced slot to consume - either the
  // original X30 (a no-op round trip, for a "normal" asm-func), or a value some earlier
  // `.pop`/`.push` pair deliberately replaced it with (the "fake return address" idiom used by
  // throw-dispatch, return-from-thread(-dead), and gstate.gc's enter-state).
  gen->add_instr(IGen::pop_gpr64(*gen, X30), irec);
  gen->add_instr(IGen::ret(*gen), irec);
}

///////////////////////
// AsmFNop
///////////////////////

IR_AsmFNop::IR_AsmFNop() : IR_Asm(false) {}

std::string IR_AsmFNop::print() {
  return ".nop.vf";
}

RegAllocInstr IR_AsmFNop::to_rai() {
  return {};
}

void IR_AsmFNop::do_codegen_x86(emitter::ObjectGenerator* gen,
                                const AllocationResult& allocs,
                                emitter::IR_Record irec) {
  (void)allocs;
  gen->add_instr(IGen::nop_vf(*gen), irec);
}

void IR_AsmFNop::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                  const AllocationResult& allocs,
                                  emitter::IR_Record irec) {
  (void)allocs;
  gen->add_instr(IGen::nop_vf(*gen), irec);
}

///////////////////////
// AsmFWait
///////////////////////

IR_AsmFWait::IR_AsmFWait() : IR_Asm(false) {}

std::string IR_AsmFWait::print() {
  return ".wait.vf";
}

RegAllocInstr IR_AsmFWait::to_rai() {
  return {};
}

void IR_AsmFWait::do_codegen_x86(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  (void)allocs;
  gen->add_instr(IGen::wait_vf(*gen), irec);
}

void IR_AsmFWait::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  (void)allocs;
  gen->add_instr(IGen::wait_vf(*gen), irec);
}

///////////////////////
// AsmPush
///////////////////////

IR_AsmPush::IR_AsmPush(bool use_coloring, const RegVal* src) : IR_Asm(use_coloring), m_src(src) {}

std::string IR_AsmPush::print() {
  return fmt::format(".push{} {}", get_color_suffix_string(), m_src->print());
}

RegAllocInstr IR_AsmPush::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_AsmPush::do_codegen_x86(emitter::ObjectGenerator* gen,
                                const AllocationResult& allocs,
                                emitter::IR_Record irec) {
  if (m_use_coloring) {
    gen->add_instr(IGen::push_gpr64(*gen, get_reg(m_src, allocs, irec)), irec);
  } else {
    gen->add_instr(IGen::push_gpr64(*gen, get_no_color_reg(m_src)), irec);
  }
}

void IR_AsmPush::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                  const AllocationResult& allocs,
                                  emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::push_gpr64() already has a real (if 16-byte-wasteful,
  // per its own comment - not a correctness issue) ARM64 implementation.
  if (m_use_coloring) {
    gen->add_instr(IGen::push_gpr64(*gen, get_reg(m_src, allocs, irec)), irec);
  } else {
    gen->add_instr(IGen::push_gpr64(*gen, get_no_color_reg(m_src)), irec);
  }
}

///////////////////////
// AsmPop
///////////////////////

IR_AsmPop::IR_AsmPop(bool use_coloring, const RegVal* dst) : IR_Asm(use_coloring), m_dst(dst) {}

std::string IR_AsmPop::print() {
  return fmt::format(".pop{} {}", get_color_suffix_string(), m_dst->print());
}

RegAllocInstr IR_AsmPop::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
  }
  return rai;
}

void IR_AsmPop::do_codegen_x86(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  if (m_use_coloring) {
    gen->add_instr(IGen::pop_gpr64(*gen, get_reg(m_dst, allocs, irec)), irec);
  } else {
    gen->add_instr(IGen::pop_gpr64(*gen, get_no_color_reg(m_dst)), irec);
  }
}

void IR_AsmPop::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::pop_gpr64() already has a real ARM64 implementation.
  if (m_use_coloring) {
    gen->add_instr(IGen::pop_gpr64(*gen, get_reg(m_dst, allocs, irec)), irec);
  } else {
    gen->add_instr(IGen::pop_gpr64(*gen, get_no_color_reg(m_dst)), irec);
  }
}

///////////////////////
// AsmSub
///////////////////////

IR_AsmSub::IR_AsmSub(bool use_coloring, const RegVal* dst, const RegVal* src)
    : IR_Asm(use_coloring), m_dst(dst), m_src(src) {}

std::string IR_AsmSub::print() {
  return fmt::format(".sub{} {}, {}", get_color_suffix_string(), m_dst->print(), m_src->print());
}

RegAllocInstr IR_AsmSub::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_AsmSub::do_codegen_x86(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  if (m_use_coloring) {
    gen->add_instr(
        IGen::sub_gpr64_gpr64(*gen, get_reg(m_dst, allocs, irec), get_reg(m_src, allocs, irec)),
        irec);
  } else {
    gen->add_instr(IGen::sub_gpr64_gpr64(*gen, get_no_color_reg(m_dst), get_no_color_reg(m_src)),
                   irec);
  }
}

void IR_AsmSub::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::sub_gpr64_gpr64() already dispatches to a real ARM64
  // encoding.
  if (m_use_coloring) {
    gen->add_instr(
        IGen::sub_gpr64_gpr64(*gen, get_reg(m_dst, allocs, irec), get_reg(m_src, allocs, irec)),
        irec);
  } else {
    gen->add_instr(IGen::sub_gpr64_gpr64(*gen, get_no_color_reg(m_dst), get_no_color_reg(m_src)),
                   irec);
  }
}

///////////////////////
// AsmAdd
///////////////////////

IR_AsmAdd::IR_AsmAdd(bool use_coloring, const RegVal* dst, const RegVal* src)
    : IR_Asm(use_coloring), m_dst(dst), m_src(src) {}

std::string IR_AsmAdd::print() {
  return fmt::format(".add{} {}, {}", get_color_suffix_string(), m_dst->print(), m_src->print());
}

RegAllocInstr IR_AsmAdd::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_AsmAdd::do_codegen_x86(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  if (m_use_coloring) {
    gen->add_instr(
        IGen::add_gpr64_gpr64(*gen, get_reg(m_dst, allocs, irec), get_reg(m_src, allocs, irec)),
        irec);
  } else {
    gen->add_instr(IGen::add_gpr64_gpr64(*gen, get_no_color_reg(m_dst), get_no_color_reg(m_src)),
                   irec);
  }
}

void IR_AsmAdd::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::add_gpr64_gpr64() already dispatches to a real ARM64
  // encoding.
  if (m_use_coloring) {
    gen->add_instr(
        IGen::add_gpr64_gpr64(*gen, get_reg(m_dst, allocs, irec), get_reg(m_src, allocs, irec)),
        irec);
  } else {
    gen->add_instr(IGen::add_gpr64_gpr64(*gen, get_no_color_reg(m_dst), get_no_color_reg(m_src)),
                   irec);
  }
}

///////////////////////
// AsmGetSymbolValue
///////////////////////

IR_GetSymbolValueAsm::IR_GetSymbolValueAsm(bool use_coloring,
                                           const RegVal* dest,
                                           std::string sym_name,
                                           bool sext)
    : IR_Asm(use_coloring), m_dest(dest), m_sym_name(std::move(sym_name)), m_sext(sext) {}

std::string IR_GetSymbolValueAsm::print() {
  return fmt::format(".load-sym{} {} [{}]", get_color_suffix_string(), m_dest->print(), m_sym_name);
}

RegAllocInstr IR_GetSymbolValueAsm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dest->ireg());
  }
  return rai;
}

void IR_GetSymbolValueAsm::do_codegen_x86(emitter::ObjectGenerator* gen,
                                          const AllocationResult& allocs,
                                          emitter::IR_Record irec) {
  auto dst_reg = m_use_coloring ? get_reg(m_dest, allocs, irec) : get_no_color_reg(m_dest);
  if (m_sext) {
    auto instr = gen->add_instr(IGen::load32s_gpr64_gpr64_plus_gpr64_plus_s32(
                                    *gen, dst_reg, gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(),
                                    LINK_SYM_NO_OFFSET_FLAG),
                                irec);
    gen->link_instruction_symbol_mem(instr, m_sym_name);
  } else {
    auto instr = gen->add_instr(IGen::load32u_gpr64_gpr64_plus_gpr64_plus_s32(
                                    *gen, dst_reg, gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(),
                                    LINK_SYM_NO_OFFSET_FLAG),
                                irec);
    gen->link_instruction_symbol_mem(instr, m_sym_name);
  }
}

void IR_GetSymbolValueAsm::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                            const AllocationResult& allocs,
                                            emitter::IR_Record irec) {
  // Same IGen::ARM64::load_symbol_value() as IR_GetSymbolValue::do_codegen_arm64 above, but this
  // node can run with m_use_coloring false (a hand-written asm function, no register allocator
  // participation at all for it - see get_no_color_reg()), so it can't rely on to_rai()'s
  // `clobber` mechanism for a scratch register the way the colored version does. Uses X17 instead
  // - already established as "reserved, never allocated to a GOAL virtual register" scratch by
  // IR_StaticVarLoad::do_codegen_arm64 above, so it's always safe here too, colored or not.
  auto dst_reg = m_use_coloring ? get_reg(m_dest, allocs, irec) : get_no_color_reg(m_dest);
  Register scratch(X17);
  auto instr = gen->add_instr(
      Instruction(IGen::ARM64::load_symbol_value(dst_reg, gRegInfo.get_st_reg(),
                                                 gRegInfo.get_offset_reg(), scratch,
                                                 LINK_SYM_NO_OFFSET_FLAG, m_sext)),
      irec);
  gen->link_instruction_symbol_mem(instr, m_sym_name);
}

///////////////////////
// AsmJumpReg
///////////////////////

IR_JumpReg::IR_JumpReg(bool use_coloring, const RegVal* src, bool stack_return)
    : IR_Asm(use_coloring), m_src(src), m_stack_return(stack_return) {}

std::string IR_JumpReg::print() {
  return fmt::format(".jr{}{} {}", get_color_suffix_string(),
                     m_stack_return ? " :stack-return #t" : "", m_src->print());
}

RegAllocInstr IR_JumpReg::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_JumpReg::do_codegen_x86(emitter::ObjectGenerator* gen,
                                const AllocationResult& allocs,
                                emitter::IR_Record irec) {
  auto src_reg = m_use_coloring ? get_reg(m_src, allocs, irec) : get_no_color_reg(m_src);
  gen->add_instr(IGen::jmp_r64(*gen, src_reg), irec);
}

void IR_JumpReg::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                  const AllocationResult& allocs,
                                  emitter::IR_Record irec) {
  auto src_reg = m_use_coloring ? get_reg(m_src, allocs, irec) : get_no_color_reg(m_src);
  if (m_stack_return) {
    // A tail call with an explicit stacked continuation must install it in LR.
    // Plain .jr also resumes suspended code, where SP must stay untouched. (AI-assisted)
    ASSERT(src_reg != X30);
    gen->add_instr(IGen::pop_gpr64(*gen, X30), irec);
  }
  gen->add_instr(IGen::jmp_r64(*gen, src_reg), irec);
}

///////////////////////
// AsmRegSet
///////////////////////

IR_RegSetAsm::IR_RegSetAsm(bool use_color, const RegVal* dst, const RegVal* src)
    : IR_Asm(use_color), m_dst(dst), m_src(src) {}

std::string IR_RegSetAsm::print() {
  return fmt::format(".mov{} {} {}", get_color_suffix_string(), m_dst->print(), m_src->print());
}

RegAllocInstr IR_RegSetAsm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_RegSetAsm::do_codegen_x86(emitter::ObjectGenerator* gen,
                                  const AllocationResult& allocs,
                                  emitter::IR_Record irec) {
  regset_common(gen, allocs, irec, m_dst, m_src, m_use_coloring);
}

void IR_RegSetAsm::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                    const AllocationResult& allocs,
                                    emitter::IR_Record irec) {
  regset_common(gen, allocs, irec, m_dst, m_src, m_use_coloring);
}

///////////////////////
// AsmVF3
///////////////////////

IR_VFMath3Asm::IR_VFMath3Asm(bool use_color,
                             const RegVal* dst,
                             const RegVal* src1,
                             const RegVal* src2,
                             Kind kind)
    : IR_Asm(use_color), m_dst(dst), m_src1(src1), m_src2(src2), m_kind(kind) {}

std::string IR_VFMath3Asm::print() {
  std::string function = "";
  switch (m_kind) {
    case Kind::XOR:
      function = ".xor.vf";
      break;
    case Kind::SUB:
      function = ".sub.vf";
      break;
    case Kind::ADD:
      function = ".add.vf";
      break;
    case Kind::MUL:
      function = ".mul.vf";
      break;
    case Kind::MAX:
      function = ".max.vf";
      break;
    case Kind::MIN:
      function = ".min.vf";
      break;
    case Kind::DIV:
      function = ".div.vf";
      break;
    default:
      ASSERT(false);
  }
  return fmt::format("{}{} {}, {}, {}", function, get_color_suffix_string(), m_dst->print(),
                     m_src1->print(), m_src2->print());
}

RegAllocInstr IR_VFMath3Asm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src1->ireg());
    rai.read.push_back(m_src2->ireg());
  }
  return rai;
}

void IR_VFMath3Asm::do_codegen_x86(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src1 = get_reg_asm(m_src1, allocs, irec, m_use_coloring);
  auto src2 = get_reg_asm(m_src2, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::XOR:
      gen->add_instr(IGen::xor_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::SUB:
      gen->add_instr(IGen::sub_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::ADD:
      gen->add_instr(IGen::add_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::MUL:
      gen->add_instr(IGen::mul_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::MAX:
      gen->add_instr(IGen::max_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::MIN:
      gen->add_instr(IGen::min_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::DIV:
      gen->add_instr(IGen::div_vf(*gen, dst, src1, src2), irec);
      break;
    default:
      ASSERT(false);
  }
}

void IR_VFMath3Asm::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                     const AllocationResult& allocs,
                                     emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - every IGen::*_vf() wrapper already has a real ARM64 NEON
  // encoding (IGenARM64.cpp), just never wired up to this node.
  //
  // NOTE - three of these pre-existing (upstream of this session, not introduced by this fix)
  // encodings look suspicious enough to flag explicitly rather than trust silently, and none were
  // touched here since verifying/fixing an existing encoding without real ARM64 hardware or an
  // authoritative reference on hand is a different, riskier task than wiring up this NYI:
  //  - add_vf()'s bit pattern is identical to parallel_add_byte()'s (the *integer* vector ADD)
  //    except for one bit that plausibly selects 32-bit-word lanes instead of byte lanes - i.e.
  //    this may really be computing a plain integer 32-bit lane add, not FADD, despite the "vf"
  //    (vector float) name and its own comment citing the integer ADD reference page.
  //  - max_vf()/min_vf() are commented as FAMAX/FAMIN (absolute-value max/min - a different real
  //    ARM64 instruction, possibly needing a CPU feature not present on all cores), not plain
  //    FMAX/FMIN, which is what GOAL's max.vf/min.vf almost certainly need semantically.
  // If a NaN, negative-max, or absolute-value-looking bug ever shows up in ARM64 vector float math
  // during actual execution testing, look here first before anywhere else.
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src1 = get_reg_asm(m_src1, allocs, irec, m_use_coloring);
  auto src2 = get_reg_asm(m_src2, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::XOR:
      gen->add_instr(IGen::xor_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::SUB:
      gen->add_instr(IGen::sub_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::ADD:
      gen->add_instr(IGen::add_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::MUL:
      gen->add_instr(IGen::mul_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::MAX:
      gen->add_instr(IGen::max_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::MIN:
      gen->add_instr(IGen::min_vf(*gen, dst, src1, src2), irec);
      break;
    case Kind::DIV:
      gen->add_instr(IGen::div_vf(*gen, dst, src1, src2), irec);
      break;
    default:
      ASSERT(false);
  }
}

///////////////////////
// IR_Int128Math3Asm
///////////////////////

IR_Int128Math3Asm::IR_Int128Math3Asm(bool use_color,
                                     const RegVal* dst,
                                     const RegVal* src1,
                                     const RegVal* src2,
                                     Kind kind)
    : IR_Asm(use_color), m_dst(dst), m_src1(src1), m_src2(src2), m_kind(kind) {}

std::string IR_Int128Math3Asm::print() {
  std::string function = "";
  switch (m_kind) {
    case Kind::PEXTLB:
      function = ".pextlb";
      break;
    case Kind::PEXTLH:
      function = ".pextlh";
      break;
    case Kind::PEXTLW:
      function = ".pextlw";
      break;
    case Kind::PEXTUB:
      function = ".pextub";
      break;
    case Kind::PEXTUH:
      function = ".pextuh";
      break;
    case Kind::PEXTUW:
      function = ".pextuw";
      break;
    case Kind::PCPYLD:
      function = ".pcpyld";
      break;
    case Kind::PCPYUD:
      function = ".pcpyud";
      break;
    case Kind::PSUBW:
      function = ".psubw";
      break;
    case Kind::PCEQB:
      function = ".pceqb";
      break;
    case Kind::PCEQH:
      function = ".pceqh";
      break;
    case Kind::PCEQW:
      function = ".pceqw";
      break;
    case Kind::PCGTB:
      function = ".pcgtb";
      break;
    case Kind::PCGTH:
      function = ".pcgth";
      break;
    case Kind::PCGTW:
      function = ".pcgtw";
      break;
    case Kind::POR:
      function = ".por";
      break;
    case Kind::PXOR:
      function = ".pxor";
      break;
    case Kind::PAND:
      function = ".pand";
      break;
    case Kind::PACKUSWB:
      function = ".packuswb";
      break;
    case Kind::PADDB:
      function = ".paddb";
      break;
    default:
      ASSERT(false);
  }
  return fmt::format("{}{} {}, {}, {}", function, get_color_suffix_string(), m_dst->print(),
                     m_src1->print(), m_src2->print());
}

RegAllocInstr IR_Int128Math3Asm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src1->ireg());
    rai.read.push_back(m_src2->ireg());
  }
  return rai;
}

void IR_Int128Math3Asm::do_codegen_x86(emitter::ObjectGenerator* gen,
                                       const AllocationResult& allocs,
                                       emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src1 = get_reg_asm(m_src1, allocs, irec, m_use_coloring);
  auto src2 = get_reg_asm(m_src2, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::PEXTUB:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextub_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PEXTUH:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextuh_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PEXTUW:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextuw_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PEXTLB:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextlb_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PEXTLH:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextlh_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PEXTLW:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextlw_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PCPYLD:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pcpyld_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PCPYUD:
      gen->add_instr(IGen::pcpyud(*gen, dst, src1, src2), irec);
      break;
    case Kind::PCEQB:
      gen->add_instr(IGen::parallel_compare_e_b(*gen, dst, src2, src1), irec);
      break;
    case Kind::PCEQH:
      gen->add_instr(IGen::parallel_compare_e_h(*gen, dst, src2, src1), irec);
      break;
    case Kind::PCEQW:
      gen->add_instr(IGen::parallel_compare_e_w(*gen, dst, src2, src1), irec);
      break;
    case Kind::PCGTB:
      gen->add_instr(IGen::parallel_compare_gt_b(*gen, dst, src1, src2), irec);
      break;
    case Kind::PCGTH:
      gen->add_instr(IGen::parallel_compare_gt_h(*gen, dst, src1, src2), irec);
      break;
    case Kind::PCGTW:
      gen->add_instr(IGen::parallel_compare_gt_w(*gen, dst, src1, src2), irec);
      break;
    case Kind::PSUBW:
      // psubW on mips is psubD on x86...
      gen->add_instr(IGen::vpsubd(*gen, dst, src1, src2), irec);
      break;
    case Kind::POR:
      gen->add_instr(IGen::parallel_bitwise_or(*gen, dst, src2, src1), irec);
      break;
    case Kind::PXOR:
      gen->add_instr(IGen::parallel_bitwise_xor(*gen, dst, src2, src1), irec);
      break;
    case Kind::PAND:
      gen->add_instr(IGen::parallel_bitwise_and(*gen, dst, src2, src1), irec);
      break;
    case Kind::PACKUSWB:
      gen->add_instr(IGen::vpackuswb(*gen, dst, src1, src2), irec);
      break;
    case Kind::PADDB:
      gen->add_instr(IGen::parallel_add_byte(*gen, dst, src1, src2), irec);
      break;
    default:
      ASSERT(false);
  }
}

void IR_Int128Math3Asm::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                         const AllocationResult& allocs,
                                         emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above, argument order (including the "_swapped" ones' src2/src1
  // order) and all - every IGen::* wrapper used here already has a real ARM64 NEON encoding
  // (IGenARM64.cpp), just never wired up to this node.
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src1 = get_reg_asm(m_src1, allocs, irec, m_use_coloring);
  auto src2 = get_reg_asm(m_src2, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::PEXTUB:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextub_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PEXTUH:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextuh_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PEXTUW:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextuw_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PEXTLB:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextlb_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PEXTLH:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextlh_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PEXTLW:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextlw_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PCPYLD:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pcpyld_swapped(*gen, dst, src2, src1), irec);
      break;
    case Kind::PCPYUD:
      gen->add_instr(IGen::pcpyud(*gen, dst, src1, src2), irec);
      break;
    case Kind::PCEQB:
      gen->add_instr(IGen::parallel_compare_e_b(*gen, dst, src2, src1), irec);
      break;
    case Kind::PCEQH:
      gen->add_instr(IGen::parallel_compare_e_h(*gen, dst, src2, src1), irec);
      break;
    case Kind::PCEQW:
      gen->add_instr(IGen::parallel_compare_e_w(*gen, dst, src2, src1), irec);
      break;
    case Kind::PCGTB:
      gen->add_instr(IGen::parallel_compare_gt_b(*gen, dst, src1, src2), irec);
      break;
    case Kind::PCGTH:
      gen->add_instr(IGen::parallel_compare_gt_h(*gen, dst, src1, src2), irec);
      break;
    case Kind::PCGTW:
      gen->add_instr(IGen::parallel_compare_gt_w(*gen, dst, src1, src2), irec);
      break;
    case Kind::PSUBW:
      // psubW on mips is psubD on x86...
      gen->add_instr(IGen::vpsubd(*gen, dst, src1, src2), irec);
      break;
    case Kind::POR:
      gen->add_instr(IGen::parallel_bitwise_or(*gen, dst, src2, src1), irec);
      break;
    case Kind::PXOR:
      gen->add_instr(IGen::parallel_bitwise_xor(*gen, dst, src2, src1), irec);
      break;
    case Kind::PAND:
      gen->add_instr(IGen::parallel_bitwise_and(*gen, dst, src2, src1), irec);
      break;
    case Kind::PACKUSWB:
      gen->add_instr(IGen::vpackuswb(*gen, dst, src1, src2), irec);
      break;
    case Kind::PADDB:
      gen->add_instr(IGen::parallel_add_byte(*gen, dst, src1, src2), irec);
      break;
    default:
      ASSERT(false);
  }
}

///////////////////////
// AsmVF2
///////////////////////

IR_VFMath2Asm::IR_VFMath2Asm(bool use_color, const RegVal* dst, const RegVal* src, Kind kind)
    : IR_Asm(use_color), m_dst(dst), m_src(src), m_kind(kind) {}

std::string IR_VFMath2Asm::print() {
  std::string function;
  switch (m_kind) {
    case Kind::ITOF:
      function = ".itof.vf";
      break;
    case Kind::FTOI:
      function = ".ftoi.vf";
      break;
    default:
      ASSERT(false);
  }

  return fmt::format("{}{} {}, {}", function, get_color_suffix_string(), m_dst->print(),
                     m_src->print());
}

RegAllocInstr IR_VFMath2Asm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_VFMath2Asm::do_codegen_x86(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::ITOF:
      gen->add_instr(IGen::itof_vf(*gen, dst, src), irec);
      break;
    case Kind::FTOI:
      gen->add_instr(IGen::ftoi_vf(*gen, dst, src), irec);
      break;
    default:
      ASSERT(false);
  }
}

void IR_VFMath2Asm::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                     const AllocationResult& allocs,
                                     emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::itof_vf()/ftoi_vf() already dispatch to real ARM64
  // SCVTF/FCVTZS (vector) encodings. This is the last remaining IR_*::do_codegen_arm64 NYI in this
  // file - see the development notes.
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::ITOF:
      gen->add_instr(IGen::itof_vf(*gen, dst, src), irec);
      break;
    case Kind::FTOI:
      gen->add_instr(IGen::ftoi_vf(*gen, dst, src), irec);
      break;
    default:
      ASSERT(false);
  }
}

///////////////////////
// AsmInt128-2
///////////////////////

IR_Int128Math2Asm::IR_Int128Math2Asm(bool use_color,
                                     const RegVal* dst,
                                     const RegVal* src,
                                     Kind kind,
                                     std::optional<int64_t> imm)
    : IR_Asm(use_color), m_dst(dst), m_src(src), m_kind(kind), m_imm(std::move(imm)) {}

std::string IR_Int128Math2Asm::print() {
  std::string function;
  bool use_imm = false;
  switch (m_kind) {
    case Kind::PW_SLL:
      use_imm = true;
      function = ".pw.sll";
      break;
    case Kind::PW_SRL:
      use_imm = true;
      function = ".pw.srl";
      break;
    case Kind::PW_SRA:
      use_imm = true;
      function = ".pw.sra";
      break;
    case Kind::PH_SLL:
      use_imm = true;
      function = ".ph.sll";
      break;
    case Kind::PH_SRL:
      use_imm = true;
      function = ".ph.srl";
      break;
    case Kind::VPSRLDQ:
      use_imm = true;
      function = ".VPSRLDQ";
      break;
    case Kind::VPSLLDQ:
      use_imm = true;
      function = ".VPSLLDQ";
      break;
    case Kind::VPSHUFLW:
      use_imm = true;
      function = ".VPSHUFLW";
      break;
    case Kind::VPSHUFHW:
      use_imm = true;
      function = ".VPSHUFHW";
      break;
    default:
      ASSERT(false);
  }

  if (use_imm) {
    ASSERT(m_imm.has_value());
    return fmt::format("{}{} {}, {}, {}", function, get_color_suffix_string(), m_dst->print(),
                       m_src->print(), *m_imm);
  } else {
    return fmt::format("{}{} {}, {}", function, get_color_suffix_string(), m_dst->print(),
                       m_src->print());
  }
}

RegAllocInstr IR_Int128Math2Asm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_Int128Math2Asm::do_codegen_x86(emitter::ObjectGenerator* gen,
                                       const AllocationResult& allocs,
                                       emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::PW_SLL:
      // you are technically allowed to put values > 32 in here.
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::pw_sll(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::PW_SRL:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::pw_srl(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::PH_SLL:
      // you are technically allowed to put values > 32 in here.
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::ph_sll(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::PH_SRL:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::ph_srl(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::PW_SRA:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::pw_sra(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::VPSRLDQ:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::vpsrldq(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::VPSLLDQ:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::vpslldq(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::VPSHUFLW:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::vpshuflw(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::VPSHUFHW:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::vpshufhw(*gen, dst, src, *m_imm), irec);
      break;
    default:
      ASSERT(false);
  }
}

void IR_Int128Math2Asm::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                         const AllocationResult& allocs,
                                         emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above. 7 of 9 IGen::* wrappers here already have real ARM64
  // encodings (pw_sll/srl/sra, ph_sll/srl - real SHL/USHR/SSHR vector-immediate shifts;
  // vpsrldq/vpslldq - real EXT byte-rotate); vpshuflw()/vpshufhw() (16-bit half-shuffle within one
  // 64-bit lane) remain real "not yet implemented" stubs - if GOAL code actually exercises
  // VPSHUFLW/VPSHUFHW on ARM64, that ASSERT_MSG will fire with a clear name to pick up from.
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::PW_SLL:
      // you are technically allowed to put values > 32 in here.
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::pw_sll(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::PW_SRL:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::pw_srl(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::PH_SLL:
      // you are technically allowed to put values > 32 in here.
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::ph_sll(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::PH_SRL:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::ph_srl(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::PW_SRA:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::pw_sra(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::VPSRLDQ:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::vpsrldq(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::VPSLLDQ:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::vpslldq(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::VPSHUFLW:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::vpshuflw(*gen, dst, src, *m_imm), irec);
      break;
    case Kind::VPSHUFHW:
      ASSERT(m_imm.has_value());
      ASSERT(*m_imm >= 0);
      ASSERT(*m_imm <= 255);
      gen->add_instr(IGen::vpshufhw(*gen, dst, src, *m_imm), irec);
      break;
    default:
      ASSERT(false);
  }
}

// ---- Blend VF

IR_BlendVF::IR_BlendVF(bool use_color,
                       const RegVal* dst,
                       const RegVal* src1,
                       const RegVal* src2,
                       u8 mask)
    : IR_Asm(use_color), m_dst(dst), m_src1(src1), m_src2(src2), m_mask(mask) {}

std::string IR_BlendVF::print() {
  return fmt::format(".blend.vf{} {}, {}, {}, {}", get_color_suffix_string(), m_dst->print(),
                     m_src1->print(), m_src2->print(), m_mask);
}

RegAllocInstr IR_BlendVF::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src1->ireg());
    rai.read.push_back(m_src2->ireg());
  }
  return rai;
}

void IR_BlendVF::do_codegen_x86(emitter::ObjectGenerator* gen,
                                const AllocationResult& allocs,
                                emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src1 = get_reg_asm(m_src1, allocs, irec, m_use_coloring);
  auto src2 = get_reg_asm(m_src2, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::blend_vf(*gen, dst, src1, src2, m_mask), irec);
}

void IR_BlendVF::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                  const AllocationResult& allocs,
                                  emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::blend_vf() now has a real ARM64 implementation (see
  // its own comment in IGenARM64.cpp for how it avoids needing a new SIMD encoding derivation).
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src1 = get_reg_asm(m_src1, allocs, irec, m_use_coloring);
  auto src2 = get_reg_asm(m_src2, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::blend_vf(*gen, dst, src1, src2, m_mask), irec);
}

// ----- Splat VF

IR_SplatVF::IR_SplatVF(bool use_color,
                       const RegVal* dst,
                       const RegVal* src,
                       const emitter::Register::VF_ELEMENT element)
    : IR_Asm(use_color), m_dst(dst), m_src(src), m_element(element) {}

std::string IR_SplatVF::print() {
  return fmt::format(".splat.vf{} {}, {}, {}", get_color_suffix_string(), m_dst->print(),
                     m_src->print(), fmt::underlying(m_element));
}

RegAllocInstr IR_SplatVF::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_SplatVF::do_codegen_x86(emitter::ObjectGenerator* gen,
                                const AllocationResult& allocs,
                                emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::splat_vf(*gen, dst, src, m_element), irec);
}

void IR_SplatVF::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                  const AllocationResult& allocs,
                                  emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::splat_vf() now has a real ARM64 DUP-based
  // implementation (IGenARM64.cpp) - see its own comment for an important caveat: not yet verified
  // against real hardware.
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::splat_vf(*gen, dst, src, m_element), irec);
}

// ---- Swizzle VF

IR_SwizzleVF::IR_SwizzleVF(bool use_color,
                           const RegVal* dst,
                           const RegVal* src,
                           const u8 controlBytes)
    : IR_Asm(use_color), m_dst(dst), m_src(src), m_controlBytes(controlBytes) {}

std::string IR_SwizzleVF::print() {
  return fmt::format(".swizzle.vf{} {}, {}, {}", get_color_suffix_string(), m_dst->print(),
                     m_src->print(), m_controlBytes);
}

RegAllocInstr IR_SwizzleVF::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_SwizzleVF::do_codegen_x86(emitter::ObjectGenerator* gen,
                                  const AllocationResult& allocs,
                                  emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::swizzle_vf(*gen, dst, src, m_controlBytes), irec);
}

void IR_SwizzleVF::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                    const AllocationResult& allocs,
                                    emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::swizzle_vf() now has a real, fully general ARM64
  // implementation (see its own comment in IGenARM64.cpp for the TBL-based approach and its
  // hardware-verification caveat).
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::swizzle_vf(*gen, dst, src, m_controlBytes), irec);
}

// ---- Square Root VF

IR_SqrtVF::IR_SqrtVF(bool use_color, const RegVal* dst, const RegVal* src)
    : IR_Asm(use_color), m_dst(dst), m_src(src) {}

std::string IR_SqrtVF::print() {
  return fmt::format(".sqrt.vf{} {}, {}", get_color_suffix_string(), m_dst->print(),
                     m_src->print());
}

RegAllocInstr IR_SqrtVF::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_SqrtVF::do_codegen_x86(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::sqrt_vf(*gen, dst, src), irec);
}

void IR_SqrtVF::do_codegen_arm64(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  // Identical to do_codegen_x86 above - IGen::sqrt_vf() already dispatches to a real ARM64 FSQRT
  // (vector) encoding.
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::sqrt_vf(*gen, dst, src), irec);
}
