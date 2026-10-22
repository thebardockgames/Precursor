/*!
 * @file CodeGenerator.cpp
 * Generate object files from a FileEnv using an emitter::ObjectGenerator.
 * Populates a DebugInfo.
 * Currently owns the logic for emitting the function prologues/epilogues and stack spill ops.
 */

#include "CodeGenerator.h"

#include <stdexcept>
#include <unordered_set>

#include "IR.h"

#include "goalc/debugger/DebugInfo.h"
#include "goalc/emitter/IGen.h"

#include "fmt/format.h"

using namespace emitter;

CodeGenerator::CodeGenerator(FileEnv* env,
                             DebugInfo* debug_info,
                             GameVersion version,
                             InstructionSet instruction_set)
    : m_gen(version, instruction_set), m_fe(env), m_debug_info(debug_info) {}

/*!
 * Generate an object file.
 */
std::vector<u8> CodeGenerator::run(const TypeSystem* ts) {
  std::unordered_set<std::string> function_names;

  // first, add each function to the ObjectGenerator (but don't add any data)
  for (auto& f : m_fe->functions()) {
    if (function_names.find(f->name()) == function_names.end()) {
      function_names.insert(f->name());
    } else {
      printf("Failed to codegen, there are two functions with internal names [%s]\n",
             f->name().c_str());
      throw std::runtime_error("Failed to codegen.");
    }
    auto rec =
        m_gen.add_function_to_seg(f->segment, &m_debug_info->add_function(f->name(), m_fe->name()));
    for (auto& x : f->code_source()) {
      rec.debug->code_sources.push_back(x.heap_obj);
    }
    for (auto& x : f->code()) {
      rec.debug->ir_strings.push_back(x->print());
    }
  }

  // next, add all static objects.
  for (auto& static_obj : m_fe->statics()) {
    static_obj->generate(&m_gen);
  }

  // next, add instructions to functions
  for (size_t i = 0; i < m_fe->functions().size(); i++) {
    do_function(m_fe->functions().at(i).get(), i);
  }

  // generate a v3 object.
  return m_gen.generate_data_v3(ts).to_vector();
}

void CodeGenerator::do_function(FunctionEnv* env, int f_idx) {
  if (env->is_asm_func) {
    if (m_gen.instr_set() == InstructionSet::X86) {
      do_asm_function_x86(env, f_idx, env->asm_func_saved_regs);
    } else if (m_gen.instr_set() == InstructionSet::ARM64) {
      do_asm_function_arm64(env, f_idx, env->asm_func_saved_regs);
    } else {
      throw std::runtime_error("CodeGenerator::do_function, instruction set not supported");
    }
  } else {
    if (m_gen.instr_set() == InstructionSet::X86) {
      do_goal_function_x86(env, f_idx);
    } else if (m_gen.instr_set() == InstructionSet::ARM64) {
      do_goal_function_arm64(env, f_idx);
    } else {
      throw std::runtime_error("CodeGenerator::do_function, instruction set not supported");
    }
  }
}

/*!
 * Add instructions to the function, specified by index.
 * Generates prologues / epilogues.
 */
void CodeGenerator::do_goal_function_x86(FunctionEnv* env, int f_idx) {
  bool use_new_xmms = true;
  auto* debug = &m_debug_info->function_by_name(env->name());

  auto f_rec = m_gen.get_existing_function_record(f_idx);
  // todo, extra alignment settings

  auto& ri = emitter::gRegInfo;
  const auto& allocs = env->alloc_result();

  // compute how much stack we will use
  int stack_offset = 0;

  // count how many xmm's we have to backup
  int n_xmm_backups = 0;
  for (auto& saved_reg : allocs.used_saved_regs) {
    if (saved_reg.is_xmm(m_gen.instr_set())) {
      n_xmm_backups++;
    }
  }

  // only for new xmms. if n == 0, we don't use this at all.
  int xmm_backup_stack_offset = 8 + XMM_SIZE * n_xmm_backups;

  if (use_new_xmms) {
    if (n_xmm_backups > 0) {
      // offset the stack
      stack_offset += xmm_backup_stack_offset;
      m_gen.add_instr_no_ir(f_rec, IGen::sub_gpr64_imm(m_gen, RSP, xmm_backup_stack_offset),
                            InstructionInfo::Kind::PROLOGUE);
      // back up xmms
      int i = 0;
      for (auto& saved_reg : allocs.used_saved_regs) {
        if (saved_reg.is_xmm(m_gen.instr_set())) {
          int offset = i * XMM_SIZE;
          m_gen.add_instr_no_ir(f_rec,
                                IGen::store128_xmm128_reg_offset(m_gen, RSP, saved_reg, offset),
                                InstructionInfo::Kind::PROLOGUE);
          i++;
        }
      }
    }
  } else {
    // back up xmms (currently not aligned)
    for (auto& saved_reg : allocs.used_saved_regs) {
      if (saved_reg.is_xmm(m_gen.instr_set())) {
        m_gen.add_instr_no_ir(f_rec, IGen::sub_gpr64_imm8s(m_gen, RSP, XMM_SIZE),
                              InstructionInfo::Kind::PROLOGUE);
        m_gen.add_instr_no_ir(f_rec, IGen::store128_gpr64_simd128(m_gen, RSP, saved_reg),
                              InstructionInfo::Kind::PROLOGUE);
        stack_offset += XMM_SIZE;
      }
    }
  }

  // back up gprs
  for (auto& saved_reg : allocs.used_saved_regs) {
    if (saved_reg.is_gpr(m_gen.instr_set())) {
      m_gen.add_instr_no_ir(f_rec, IGen::push_gpr64(m_gen, saved_reg),
                            InstructionInfo::Kind::PROLOGUE);
      stack_offset += GPR_SIZE;
    }
  }

  // do we include an extra push to get 8 more bytes to keep the stack aligned?
  bool bonus_push = false;

  // the offset to add directly to rsp for stack variables or spills (no push/pop)
  int manually_added_stack_offset =
      GPR_SIZE * (allocs.stack_slots_for_spills + allocs.stack_slots_for_vars);
  stack_offset += manually_added_stack_offset;

  // do we need to align or manually offset?
  if (manually_added_stack_offset || allocs.needs_aligned_stack_for_spills ||
      env->needs_aligned_stack()) {
    if (!(stack_offset & 15)) {
      if (manually_added_stack_offset) {
        // if we're already adding to rsp, just add 8 more.
        manually_added_stack_offset += 8;
      } else {
        // otherwise to an extra push, and remember so we can do an extra pop later on.
        bonus_push = true;
        m_gen.add_instr_no_ir(f_rec, IGen::push_gpr64(m_gen, ri.get_saved_gpr(0)),
                              InstructionInfo::Kind::PROLOGUE);
      }
      stack_offset += 8;
    }

    ASSERT(stack_offset & 15);

    // do manual stack offset.
    if (manually_added_stack_offset) {
      m_gen.add_instr_no_ir(f_rec, IGen::sub_gpr64_imm(m_gen, RSP, manually_added_stack_offset),
                            InstructionInfo::Kind::PROLOGUE);
    }
  }
  debug->stack_usage = stack_offset;

  // emit each IR into x86 instructions.
  for (int ir_idx = 0; ir_idx < int(env->code().size()); ir_idx++) {
    auto& ir = env->code().at(ir_idx);
    // start of IR
    auto i_rec = m_gen.add_ir(f_rec);

    // load anything off the stack that was spilled and is needed.
    auto& bonus = allocs.stack_ops.at(ir_idx);
    for (auto& op : bonus.ops) {
      if (op.load) {
        if (op.reg.is_gpr(m_gen.instr_set()) && op.reg_class == RegClass::GPR_64) {
          // todo, s8 or 0 offset if possible?
          m_gen.add_instr(IGen::load64_gpr64_plus_s32(
                              m_gen, op.reg, allocs.get_slot_for_spill(op.slot) * GPR_SIZE, RSP),
                          i_rec);
        } else if (op.reg.is_xmm(m_gen.instr_set()) && op.reg_class == RegClass::FLOAT) {
          // load xmm32 off of the stack
          m_gen.add_instr(IGen::load_reg_offset_xmm32(
                              m_gen, op.reg, RSP, allocs.get_slot_for_spill(op.slot) * GPR_SIZE),
                          i_rec);
        } else if (op.reg.is_xmm(m_gen.instr_set()) &&
                   (op.reg_class == RegClass::VECTOR_FLOAT || op.reg_class == RegClass::INT_128)) {
          m_gen.add_instr(IGen::load128_xmm128_reg_offset(
                              m_gen, op.reg, RSP, allocs.get_slot_for_spill(op.slot) * GPR_SIZE),
                          i_rec);
        } else {
          ASSERT(false);
        }
      }
    }

    // do the actual op
    ir->do_codegen_x86(&m_gen, allocs, i_rec);

    // store things back on the stack if needed.
    for (auto& op : bonus.ops) {
      if (op.store) {
        if (op.reg.is_gpr(m_gen.instr_set()) && op.reg_class == RegClass::GPR_64) {
          // todo, s8 or 0 offset if possible?
          m_gen.add_instr(IGen::store64_gpr64_plus_s32(
                              m_gen, RSP, allocs.get_slot_for_spill(op.slot) * GPR_SIZE, op.reg),
                          i_rec);
        } else if (op.reg.is_xmm(m_gen.instr_set()) && op.reg_class == RegClass::FLOAT) {
          // store xmm32 on the stack
          m_gen.add_instr(IGen::store_reg_offset_xmm32(
                              m_gen, RSP, op.reg, allocs.get_slot_for_spill(op.slot) * GPR_SIZE),
                          i_rec);
        } else if (op.reg.is_xmm(m_gen.instr_set()) &&
                   (op.reg_class == RegClass::VECTOR_FLOAT || op.reg_class == RegClass::INT_128)) {
          m_gen.add_instr(IGen::store128_xmm128_reg_offset(
                              m_gen, RSP, op.reg, allocs.get_slot_for_spill(op.slot) * GPR_SIZE),
                          i_rec);
        } else {
          ASSERT(false);
        }
      }
    }
  }  // end IR loop

  // EPILOGUE
  if (manually_added_stack_offset || allocs.needs_aligned_stack_for_spills ||
      env->needs_aligned_stack()) {
    if (manually_added_stack_offset) {
      m_gen.add_instr_no_ir(f_rec, IGen::add_gpr64_imm(m_gen, RSP, manually_added_stack_offset),
                            InstructionInfo::Kind::EPILOGUE);
    }

    if (bonus_push) {
      ASSERT(!manually_added_stack_offset);
      m_gen.add_instr_no_ir(f_rec, IGen::pop_gpr64(m_gen, ri.get_saved_gpr(0)),
                            InstructionInfo::Kind::EPILOGUE);
    }
  }

  for (int i = int(allocs.used_saved_regs.size()); i-- > 0;) {
    auto& saved_reg = allocs.used_saved_regs.at(i);
    if (saved_reg.is_gpr(m_gen.instr_set())) {
      m_gen.add_instr_no_ir(f_rec, IGen::pop_gpr64(m_gen, saved_reg),
                            InstructionInfo::Kind::EPILOGUE);
    }
  }

  if (use_new_xmms) {
    if (n_xmm_backups > 0) {
      int j = n_xmm_backups;
      for (int i = int(allocs.used_saved_regs.size()); i-- > 0;) {
        auto& saved_reg = allocs.used_saved_regs.at(i);
        if (saved_reg.is_xmm(m_gen.instr_set())) {
          j--;
          int offset = j * XMM_SIZE;
          m_gen.add_instr_no_ir(f_rec,
                                IGen::load128_xmm128_reg_offset(m_gen, saved_reg, RSP, offset),
                                InstructionInfo::Kind::EPILOGUE);
        }
      }
      ASSERT(j == 0);
      m_gen.add_instr_no_ir(f_rec, IGen::add_gpr64_imm(m_gen, RSP, xmm_backup_stack_offset),
                            InstructionInfo::Kind::EPILOGUE);
    }
  } else {
    for (int i = int(allocs.used_saved_regs.size()); i-- > 0;) {
      auto& saved_reg = allocs.used_saved_regs.at(i);
      if (saved_reg.is_xmm(m_gen.instr_set())) {
        m_gen.add_instr_no_ir(f_rec, IGen::load128_simd128_gpr64(m_gen, saved_reg, RSP),
                              InstructionInfo::Kind::EPILOGUE);
        m_gen.add_instr_no_ir(f_rec, IGen::add_gpr64_imm8s(m_gen, RSP, XMM_SIZE),
                              InstructionInfo::Kind::EPILOGUE);
      }
    }
  }

  m_gen.add_instr_no_ir(f_rec, IGen::ret(m_gen), InstructionInfo::Kind::EPILOGUE);
}

/*!
 * ARM64 counterpart to do_goal_function_x86(). Structurally the same idea (prologue that backs
 * up saved regs and reserves spill/local stack space, one arm64 instruction sequence emitted per
 * IR with spill loads/stores wrapped around it, then the epilogue undoing the prologue) - but
 * deliberately not a line-for-line port, because ARM64's stack has a hard, always-on 16-byte
 * alignment requirement (unlike this project's x86-64 target, which only cares about it at call
 * boundaries / for xmm natural alignment, hence do_goal_function_x86's "bonus_push" fixup). Here,
 * every single adjustment below is already a multiple of 16 by construction, so there's nothing
 * to detect-and-fix after the fact:
 * - push_gpr64/pop_gpr64 always move SP by 16, not 8 (see their own comments in IGenARM64.cpp -
 *   there's no push-pair/STP support here yet, so this is intentionally not the most compact
 *   encoding).
 * - the vector-register backup block is sized as XMM_SIZE(16) * count, with none of
 *   do_goal_function_x86's extra "+8" byte (which would have broken alignment here).
 * - the spill/local-var area is rounded up to the next 16 if the slot count is odd.
 */
void CodeGenerator::do_goal_function_arm64(FunctionEnv* env, int f_idx) {
  auto* debug = &m_debug_info->function_by_name(env->name());
  auto f_rec = m_gen.get_existing_function_record(f_idx);
  const auto& allocs = env->alloc_result();

  // Unconditionally save/restore X30 (LR) around the whole body - unlike x86's `call`, ARM64's
  // `blr` puts the return address in a register, not on the stack. IR_FunctionCall's to_rai()
  // only clobbers registers gRegInfo actually models (temp/saved GPRs+XMMs); X30 isn't one of
  // those (it's marked "don't use it" in Register.h, invisible to the allocator), so a function
  // making 2+ calls would silently clobber its own incoming return address with the last call's
  // return address - its own final `ret` then jumps back into its own body instead of to its real
  // caller, an infinite loop indistinguishable from a hang. Confirmed via a live lldb attach on a
  // real hung ARM64 device: PC was landing exactly on gcommon.gc's top-level `ret`, and LR at that
  // point matched the return address of its own last `method-set!` call, not its real caller - see
  // the development notes' "second distinct hang bug" for the full trace. Every function pays one
  // extra push+pop (16 bytes) even if it's a leaf with no calls at all - correctness first; a
  // leaf-function-detection optimization can come later if this ever matters for code size/speed.
  m_gen.add_instr_no_ir(f_rec, IGen::push_gpr64(m_gen, X30), InstructionInfo::Kind::PROLOGUE);

  int stack_offset = 0;

  int n_xmm_backups = 0;
  for (auto& saved_reg : allocs.used_saved_regs) {
    if (saved_reg.is_xmm(m_gen.instr_set())) {
      n_xmm_backups++;
    }
  }

  int xmm_backup_stack_offset = XMM_SIZE * n_xmm_backups;
  if (n_xmm_backups > 0) {
    stack_offset += xmm_backup_stack_offset;
    m_gen.add_instr_no_ir(f_rec, IGen::sub_gpr64_imm(m_gen, SP, xmm_backup_stack_offset),
                          InstructionInfo::Kind::PROLOGUE);
    int i = 0;
    for (auto& saved_reg : allocs.used_saved_regs) {
      if (saved_reg.is_xmm(m_gen.instr_set())) {
        int offset = i * XMM_SIZE;
        m_gen.add_instr_no_ir(f_rec,
                              IGen::store128_xmm128_reg_offset(m_gen, SP, saved_reg, offset),
                              InstructionInfo::Kind::PROLOGUE);
        i++;
      }
    }
  }

  // back up gprs. Each push_gpr64 moves SP by 16 (see comment above), not GPR_SIZE(8).
  for (auto& saved_reg : allocs.used_saved_regs) {
    if (saved_reg.is_gpr(m_gen.instr_set())) {
      m_gen.add_instr_no_ir(f_rec, IGen::push_gpr64(m_gen, saved_reg),
                            InstructionInfo::Kind::PROLOGUE);
      stack_offset += 16;
    }
  }

  // stack for spills/local vars, rounded up to keep SP 16-aligned (everything above already is).
  int manually_added_stack_offset =
      GPR_SIZE * (allocs.stack_slots_for_spills + allocs.stack_slots_for_vars);
  if (manually_added_stack_offset & 15) {
    manually_added_stack_offset = (manually_added_stack_offset + 15) & ~15;
  }
  stack_offset += manually_added_stack_offset;

  if (manually_added_stack_offset > 0) {
    m_gen.add_instr_no_ir(f_rec, IGen::sub_gpr64_imm(m_gen, SP, manually_added_stack_offset),
                          InstructionInfo::Kind::PROLOGUE);
  }
  debug->stack_usage = stack_offset;

  // emit each IR into arm64 instructions.
  for (int ir_idx = 0; ir_idx < int(env->code().size()); ir_idx++) {
    auto& ir = env->code().at(ir_idx);
    auto i_rec = m_gen.add_ir(f_rec);

    // load anything off the stack that was spilled and is needed.
    auto& bonus = allocs.stack_ops.at(ir_idx);
    for (auto& op : bonus.ops) {
      if (op.load) {
        if (op.reg.is_gpr(m_gen.instr_set()) && op.reg_class == RegClass::GPR_64) {
          m_gen.add_instr(IGen::load64_gpr64_plus_s32(
                              m_gen, op.reg, allocs.get_slot_for_spill(op.slot) * GPR_SIZE, SP),
                          i_rec);
        } else if (op.reg.is_xmm(m_gen.instr_set()) && op.reg_class == RegClass::FLOAT) {
          m_gen.add_instr(IGen::load_reg_offset_xmm32(
                              m_gen, op.reg, SP, allocs.get_slot_for_spill(op.slot) * GPR_SIZE),
                          i_rec);
        } else if (op.reg.is_xmm(m_gen.instr_set()) &&
                   (op.reg_class == RegClass::VECTOR_FLOAT || op.reg_class == RegClass::INT_128)) {
          m_gen.add_instr(IGen::load128_xmm128_reg_offset(
                              m_gen, op.reg, SP, allocs.get_slot_for_spill(op.slot) * GPR_SIZE),
                          i_rec);
        } else {
          ASSERT(false);
        }
      }
    }

    // do the actual op
    ir->do_codegen_arm64(&m_gen, allocs, i_rec);

    // store things back on the stack if needed.
    for (auto& op : bonus.ops) {
      if (op.store) {
        if (op.reg.is_gpr(m_gen.instr_set()) && op.reg_class == RegClass::GPR_64) {
          m_gen.add_instr(IGen::store64_gpr64_plus_s32(
                              m_gen, SP, allocs.get_slot_for_spill(op.slot) * GPR_SIZE, op.reg),
                          i_rec);
        } else if (op.reg.is_xmm(m_gen.instr_set()) && op.reg_class == RegClass::FLOAT) {
          m_gen.add_instr(IGen::store_reg_offset_xmm32(
                              m_gen, SP, op.reg, allocs.get_slot_for_spill(op.slot) * GPR_SIZE),
                          i_rec);
        } else if (op.reg.is_xmm(m_gen.instr_set()) &&
                   (op.reg_class == RegClass::VECTOR_FLOAT || op.reg_class == RegClass::INT_128)) {
          m_gen.add_instr(IGen::store128_xmm128_reg_offset(
                              m_gen, SP, op.reg, allocs.get_slot_for_spill(op.slot) * GPR_SIZE),
                          i_rec);
        } else {
          ASSERT(false);
        }
      }
    }
  }  // end IR loop

  // EPILOGUE
  if (manually_added_stack_offset > 0) {
    m_gen.add_instr_no_ir(f_rec, IGen::add_gpr64_imm(m_gen, SP, manually_added_stack_offset),
                          InstructionInfo::Kind::EPILOGUE);
  }

  for (int i = int(allocs.used_saved_regs.size()); i-- > 0;) {
    auto& saved_reg = allocs.used_saved_regs.at(i);
    if (saved_reg.is_gpr(m_gen.instr_set())) {
      m_gen.add_instr_no_ir(f_rec, IGen::pop_gpr64(m_gen, saved_reg),
                            InstructionInfo::Kind::EPILOGUE);
    }
  }

  if (n_xmm_backups > 0) {
    int j = n_xmm_backups;
    for (int i = int(allocs.used_saved_regs.size()); i-- > 0;) {
      auto& saved_reg = allocs.used_saved_regs.at(i);
      if (saved_reg.is_xmm(m_gen.instr_set())) {
        j--;
        int offset = j * XMM_SIZE;
        m_gen.add_instr_no_ir(f_rec, IGen::load128_xmm128_reg_offset(m_gen, saved_reg, SP, offset),
                              InstructionInfo::Kind::EPILOGUE);
      }
    }
    ASSERT(j == 0);
    m_gen.add_instr_no_ir(f_rec, IGen::add_gpr64_imm(m_gen, SP, xmm_backup_stack_offset),
                          InstructionInfo::Kind::EPILOGUE);
  }

  // restore X30 (LR) - see the matching push in the prologue above for why this is needed.
  m_gen.add_instr_no_ir(f_rec, IGen::pop_gpr64(m_gen, X30), InstructionInfo::Kind::EPILOGUE);

  m_gen.add_instr_no_ir(f_rec, IGen::ret(m_gen), InstructionInfo::Kind::EPILOGUE);
}

void CodeGenerator::do_asm_function_x86(FunctionEnv* env, int f_idx, bool allow_saved_regs) {
  auto f_rec = m_gen.get_existing_function_record(f_idx);
  const auto& allocs = env->alloc_result();

  if (!allow_saved_regs && !allocs.used_saved_regs.empty()) {
    std::string err = fmt::format(
        "ASM Function {}'s coloring using the following callee-saved registers: ", env->name());
    for (auto& x : allocs.used_saved_regs) {
      err += x.print();
      err += " ";
    }
    err.pop_back();
    err.push_back('.');
    throw std::runtime_error(err);
  }

  if (allocs.stack_slots_for_spills) {
    throw std::runtime_error("ASM Function has used the stack for spills.");
  }

  if (allocs.stack_slots_for_vars) {
    throw std::runtime_error("ASM Function has variables on the stack.");
  }

  // emit each IR into x86 instructions.
  for (int ir_idx = 0; ir_idx < int(env->code().size()); ir_idx++) {
    auto& ir = env->code().at(ir_idx);
    // start of IR
    auto i_rec = m_gen.add_ir(f_rec);

    // Make sure we aren't automatically accessing the stack.
    if (!allocs.stack_ops.at(ir_idx).ops.empty()) {
      throw std::runtime_error("ASM Function used a bonus op.");
    }

    // do the actual op
    ir->do_codegen_x86(&m_gen, allocs, i_rec);
  }
}

void CodeGenerator::do_asm_function_arm64(FunctionEnv* env, int f_idx, bool allow_saved_regs) {
  // Mostly identical to do_asm_function_x86 above - an asm function's whole point is that the
  // compiler emits no automatic stack management at all (that's do_goal_function_*'s job), just
  // each IR's do_codegen_* back to back exactly as written. But `.ret`/`.push`/`.pop` ARE written
  // assuming x86's calling convention, where `call` implicitly pushes the return address onto the
  // stack and `ret` implicitly pops it back off - several hand-written asm-funcs (gkernel.gc's
  // throw-dispatch/return-from-thread/return-from-thread-dead, gstate.gc's enter-state) rely on
  // this: they `.pop`/discard the real return address, compute a *different* target (e.g. a
  // catch-frame's saved `ra`, or a fixed trampoline like return-from-thread-dead), `.push` that
  // instead, and `.ret` - expecting to "return" to the computed address rather than the real
  // caller. On ARM64 there is no implicit stack push on call - the return address lives in the
  // LR (X30) register, and a bare `RET` (see IGen::ret()) reads X30 directly, never touching the
  // stack at all. Confirmed via a live crash trace: `throw`'s while loop correctly found and
  // matched the pushed catch-frame (verified via TEMP DEBUG prints - both `eq?` checks were #t),
  // but throw-dispatch's `.ret` silently returned to throw's own caller instead of the intended
  // unwind target, since the pushed "fake return address" was simply never consulted - see
  // the development notes.
  //
  // Fix: push X30 here, as the very first instruction, on ARM64 only - simulating what x86's
  // `call` instruction already does for free. IR_AsmRet::do_codegen_arm64 is the other half: it
  // pops the top of the stack back into X30 before the real `RET`, simulating x86's `ret`. Together
  // these make every existing `.push`/`.pop`/`.ret` sequence behave identically on both
  // architectures with zero GOAL source changes: a "normal" asm-func (no fake-return trickery)
  // just pushes X30 and immediately pops the same value back, a no-op round trip that leaves
  // behavior unchanged from before this fix; one using the fake-return idiom now correctly
  // replaces that slot's value and jumps there. A `.jr`-terminated asm-func (reset-and-call,
  // set-to-run-bootstrap) never reaches a `.ret` to pop this back, but that's harmless: those
  // exist specifically to permanently transfer control elsewhere (into a fresh thread stack) and
  // are never expected to return normally to their own caller, so the orphaned 16 bytes on
  // whatever stack was active at entry are simply abandoned, exactly as they would be if this
  // function never returned at all.
  auto f_rec = m_gen.get_existing_function_record(f_idx);
  const auto& allocs = env->alloc_result();

  if (!allow_saved_regs && !allocs.used_saved_regs.empty()) {
    std::string err = fmt::format(
        "ASM Function {}'s coloring using the following callee-saved registers: ", env->name());
    for (auto& x : allocs.used_saved_regs) {
      err += x.print();
      err += " ";
    }
    err.pop_back();
    err.push_back('.');
    throw std::runtime_error(err);
  }

  if (allocs.stack_slots_for_spills) {
    throw std::runtime_error("ASM Function has used the stack for spills.");
  }

  if (allocs.stack_slots_for_vars) {
    throw std::runtime_error("ASM Function has variables on the stack.");
  }

  // See the big comment on this function above: simulate x86's implicit call-pushes-return-
  // address behavior, so any `.push`/`.pop`/`.ret` sequence in the body below sees the same stack
  // layout it would on x86.
  m_gen.add_instr_no_ir(f_rec, IGen::push_gpr64(m_gen, X30), InstructionInfo::Kind::PROLOGUE);

  // emit each IR into arm64 instructions.
  for (int ir_idx = 0; ir_idx < int(env->code().size()); ir_idx++) {
    auto& ir = env->code().at(ir_idx);
    // start of IR
    auto i_rec = m_gen.add_ir(f_rec);

    // Make sure we aren't automatically accessing the stack.
    if (!allocs.stack_ops.at(ir_idx).ops.empty()) {
      throw std::runtime_error("ASM Function used a bonus op.");
    }

    // do the actual op
    ir->do_codegen_arm64(&m_gen, allocs, i_rec);
  }
}
