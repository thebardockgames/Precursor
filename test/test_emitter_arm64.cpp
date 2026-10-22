#include "emitter_util.h"

#include "common/util/FileUtil.h"

#include "goalc/compiler/CodeGenerator.h"
#include "goalc/compiler/Compiler.h"
#include "goalc/emitter/CodeTester.h"
#include "goalc/emitter/IGen.h"
#include "goalc/emitter/IGenARM64.h"
#include "goalc/regalloc/Allocator.h"
#include "gtest/gtest.h"

using namespace emitter;

TEST(ARM64Allocator, distinct_reads_after_control_flow_gap) {
  // A read can become live after a gap in instruction order without being a
  // new definition. It must not share the other operand's spill register.
  // This is the shape of Jak 2 nav-mesh's byte store crash. (AI-assisted)
  for (const auto arch : {InstructionSet::X86, InstructionSet::ARM64}) {
    Compiler compiler(GameVersion::Jak2, arch);
    for (const int spill : {0, 1, 2, 3}) {
      AllocationInput input;
      input.max_vars = 2;
      input.instructions.resize(7);
      const IRegister pointer(RegClass::GPR_64, 0);
      const IRegister value(RegClass::GPR_64, 1);
      input.instructions[0].write = {pointer};
      input.instructions[1].write = {value};
      input.instructions[2].jumps = {5};
      input.instructions[3].read = {value};
      input.instructions[4].jumps = {6};
      input.instructions[4].fallthrough = false;
      input.instructions[5].read = {pointer, value};
      input.instructions[6].fallthrough = false;
      for (int var = 0; var < 2; ++var) {
        if (spill & (1 << var)) {
          input.force_on_stack_regs.insert(var);
        }
      }
      const auto result = allocate_registers(input);
      EXPECT_TRUE(result.ok);
      EXPECT_NE(result.ass_as_ranges.at(0).get(5).reg,
                result.ass_as_ranges.at(1).get(5).reg)
          << "architecture=" << int(arch) << " spilled=" << spill;
    }
  }
  Compiler restore_x86(GameVersion::Jak2, InstructionSet::X86);
}

TEST(ARM64Allocator, dying_input_can_share_write_only_result) {
  // Preserve legitimate register reuse for a move, including spill reload/store. (AI-assisted)
  for (const auto arch : {InstructionSet::X86, InstructionSet::ARM64}) {
    Compiler compiler(GameVersion::Jak2, arch);
    for (const bool spill : {false, true}) {
      AllocationInput input;
      input.max_vars = 2;
      input.instructions.resize(3);
      const IRegister source(RegClass::GPR_64, 0);
      const IRegister destination(RegClass::GPR_64, 1);
      input.instructions[0].write = {source};
      input.instructions[1].read = {source};
      input.instructions[1].write = {destination};
      input.instructions[1].is_move = true;
      input.instructions[2].read = {destination};
      input.instructions[2].fallthrough = false;
      if (spill) {
        input.force_on_stack_regs = {0, 1};
      }
      const auto result = allocate_registers(input);
      EXPECT_TRUE(result.ok);
      EXPECT_EQ(result.ass_as_ranges.at(0).get(1).reg,
                result.ass_as_ranges.at(1).get(1).reg);
    }
  }
  Compiler restore_x86(GameVersion::Jak2, InstructionSet::X86);
}

TEST(ARM64CompilerTarget, compile_time_architecture) {
  // Cross-compilation must select by output target, including after switching
  // architectures in the same process. (AI-assisted)
  for (const auto arch : {InstructionSet::X86, InstructionSet::ARM64, InstructionSet::X86}) {
    Compiler compiler(GameVersion::Jak2, arch);
    const auto expected = arch == InstructionSet::ARM64 ? "arm64" : "x86-64";
    goos::Object target;
    ASSERT_TRUE(compiler.get_goos().get_global_variable_by_name("TARGET_ARCH", &target));
    EXPECT_EQ(target, compiler.get_goos().intern(expected));
    const auto source = compiler.get_goos().reader.read_from_string(
        std::string("(#cond ((eq? TARGET_ARCH '") + expected +
        ") (defglobalconstant target-budget 1.5)) (#t (invalid-target-architecture)))");
    EXPECT_NO_THROW(compiler.compile_object_file("target-architecture", source, false));
  }
}

TEST(ARM64CompilerTarget, crate_backup_stack_budget) {
  // Read the production policy, not a duplicate of its architecture branch. (AI-assisted)
  const auto source = file_util::read_text_file(file_util::get_file_path(
      {"goal_src", "jak2", "engine", "common_objs", "crates.gc"}));
  const auto begin = source.find("(#cond");
  const auto end = source.find("(defskelgroup", begin);
  ASSERT_NE(begin, std::string::npos);
  ASSERT_NE(end, std::string::npos);
  const auto init = source.find("(defmethod crate-init!");
  ASSERT_NE(init, std::string::npos);
  const auto reserve = source.find(
      "(stack-size-set! (-> this main-thread) CRATE_BACKUP_STACK_SIZE)", init);
  ASSERT_NE(reserve, std::string::npos);
  EXPECT_LT(reserve, source.find("(new 'process", init));
  for (const auto arch : {InstructionSet::ARM64, InstructionSet::X86}) {
    Compiler compiler(GameVersion::Jak2, arch);
    const auto fixture = source.substr(begin, end - begin) +
        "\n(defun crate-stack-budget () CRATE_BACKUP_STACK_SIZE)";
    auto compiled = compiler.compile_object_file(
        "crate-stack-budget", compiler.get_goos().reader.read_from_string(fixture), true);
    const std::string suffix = arch == InstructionSet::ARM64 ? ", 512" : ", 128";
    int constants = 0;
    for (const auto& function : compiled->functions()) {
      if (function->name() != "crate-stack-budget") continue;
      for (const auto& instruction : function->code()) {
        if (dynamic_cast<IR_LoadConstant64*>(instruction.get())) {
          ++constants;
          const auto printed = instruction->print();
          ASSERT_GE(printed.size(), suffix.size());
          EXPECT_EQ(printed.substr(printed.size() - suffix.size()), suffix);
        }
      }
    }
    EXPECT_EQ(constants, 1);
  }
}

TEST(ARM64Allocator, export_spilled_store_after_branch) {
  // Exercise compiler-generated reloads, address arithmetic and byte stores on
  // the device too, not just the allocator's assignment table. (AI-assisted)
  Compiler compiler(GameVersion::Jak2, InstructionSet::ARM64);
  const auto source = compiler.get_goos().reader.read_from_string(R"(
    (defun spilled-store-after-branch ((dest (pointer uint8)) (value int) (skip int))
      (when (!= skip 0)
        (return 77))
      (set! (-> dest 60) value)
      99)
  )");
  const std::string name = "spilled-store-after-branch";
  auto compiled = compiler.compile_object_file(name, source, true);
  for (const auto& function : compiled->functions()) {
    for (const auto& reg : function->reg_vals()) {
      reg->force_on_stack();
    }
  }
  compiler.color_object_file(compiled);
  auto& debug = compiler.get_debugger().get_debug_info_for_object(name);
  CodeGenerator generator(compiled, &debug, GameVersion::Jak2, InstructionSet::ARM64);
  generator.run(&compiler.type_system());
  const auto& code = debug.function_by_name(name).generated_code;
  ASSERT_FALSE(code.empty());
  const auto path = file_util::get_file_path({"out", "arm64-runtime-generated", name + ".bin"});
  file_util::create_dir_if_needed_for_file(path);
  file_util::write_binary_file(path, code.data(), code.size());
}

TEST(ARM64Collision, export_sphere_box_query) {
  // Same broad-phase operations as drawable-sphere-box-intersect? in Jak 2.
  // Pointer-only inputs allow execution without game assets. (AI-assisted)
  Compiler compiler(GameVersion::Jak2, InstructionSet::ARM64);
  const auto source = compiler.get_goos().reader.read_from_string(R"(
    (defun collision-sphere-box ((sphere (pointer float)) (bounds (pointer uint128)))
      (local-vars (lo uint128) (hi uint128) (outside-lo uint128)
                  (outside-hi uint128) (outside uint128) (packed uint128))
      (rlet ((center :class vf) (lower :class vf) (upper :class vf))
        (.lvf center sphere)
        (.add.w.vf.xyz upper center center)
        (.sub.w.vf.xyz lower center center)
        (.ftoi.vf lower lower)
        (.ftoi.vf upper upper)
        (.mov lo lower)
        (.mov hi upper)
        (.pcgtw outside-lo (-> bounds 0) hi)
        (.pcgtw outside-hi lo (-> bounds 1))
        (.por outside outside-lo outside-hi)
        (.ppach packed (the-as uint128 0) outside)
        (if (zero? (shl (the-as int packed) 16)) 1 0)))
  )");
  const std::string name = "collision-sphere-box";
  auto compiled = compiler.compile_object_file(name, source, true);
  compiler.color_object_file(compiled);
  auto& debug = compiler.get_debugger().get_debug_info_for_object(name);
  CodeGenerator generator(compiled, &debug, GameVersion::Jak2, InstructionSet::ARM64);
  generator.run(&compiler.type_system());
  const auto& code = debug.function_by_name(name).generated_code;
  ASSERT_FALSE(code.empty());
  const auto path = file_util::get_file_path({"out", "arm64-runtime-generated", name + ".bin"});
  file_util::create_dir_if_needed_for_file(path);
  file_util::write_binary_file(path, code.data(), code.size());
}

TEST(ARM64Collision, export_matrix_inverse) {
  // Exercise the actual high-register-pressure routine used by instanced collision.
  // Supply vf0 beside the input matrix so the exported code needs no relocations. (AI-assisted)
  Compiler compiler(GameVersion::Jak2, InstructionSet::ARM64);
  auto source = file_util::read_text_file(
      file_util::get_file_path({"goal_src", "jak2", "engine", "math", "matrix.gc"}));
  const auto begin = source.find("(defun matrix-4x4-inverse!");
  const auto end = source.find("(defun matrix-translate!", begin);
  ASSERT_NE(begin, std::string::npos);
  ASSERT_NE(end, std::string::npos);
  source = source.substr(begin, end - begin);
  for (size_t pos; (pos = source.find("(nop!)")) != std::string::npos;) {
    source.erase(pos, 6);
  }
  const auto init = source.find("(init-vf0-vector)");
  ASSERT_NE(init, std::string::npos);
  source.replace(init, 17, "(.lvf vf0 (&+ (the-as pointer arg1) 64))");
  source =
      "(deftype vector (structure) ((quad uint128)))\n"
      "(deftype matrix (structure) ((quad uint128 4) (trans vector :inline :offset 48)))\n" +
      source;
  const std::string name = "matrix-4x4-inverse!";
  auto compiled =
      compiler.compile_object_file(name, compiler.get_goos().reader.read_from_string(source), true);
  compiler.color_object_file(compiled);
  auto& debug = compiler.get_debugger().get_debug_info_for_object(name);
  CodeGenerator generator(compiled, &debug, GameVersion::Jak2, InstructionSet::ARM64);
  generator.run(&compiler.type_system());
  const auto& code = debug.function_by_name(name).generated_code;
  ASSERT_FALSE(code.empty());
  const auto path =
      file_util::get_file_path({"out", "arm64-runtime-generated", "collision-matrix-inverse.bin"});
  file_util::create_dir_if_needed_for_file(path);
  file_util::write_binary_file(path, code.data(), code.size());
  for (const auto& function : compiled->functions()) {
    if (function->name() != name)
      continue;
    for (size_t index = 0; index < function->code().size(); ++index) {
      const auto rai = function->code()[index]->to_rai();
      auto operands = rai.read;
      operands.insert(operands.end(), rai.write.begin(), rai.write.end());
      for (const auto& operand : operands) {
        const auto& assignment = function->alloc_result().ass_as_ranges.at(operand.id).get(index);
        if (assignment.reg.is_arm64_vector_tag()) {
          EXPECT_LT(assignment.reg.id(), 30) << "IR " << index << " variable " << operand.id;
        }
      }
    }
  }
}

TEST(ARM64EmitterEncoding, and_gpr64_gpr64) {
  for (int destination = 0; destination < 31; ++destination) {
    for (int source = 0; source < 31; ++source) {
      const auto instruction = IGen::ARM64::and_gpr64_gpr64(destination, source);
      ASSERT_EQ(instruction.count, 1);
      EXPECT_EQ(instruction.encodings[0],
                0x8a000000u | (source << 16) | (destination << 5) | destination);
    }
  }
}

TEST(ARM64EmitterEncoding, or_gpr64_gpr64) {
  for (int destination = 0; destination < 31; ++destination) {
    for (int source = 0; source < 31; ++source) {
      const auto instruction = IGen::ARM64::or_gpr64_gpr64(destination, source);
      ASSERT_EQ(instruction.count, 1);
      EXPECT_EQ(instruction.encodings[0],
                0xaa000000u | (source << 16) | (destination << 5) | destination);
    }
  }
}

TEST(ARM64EmitterEncoding, xor_gpr64_gpr64) {
  for (int destination = 0; destination < 31; ++destination) {
    for (int source = 0; source < 31; ++source) {
      const auto instruction = IGen::ARM64::xor_gpr64_gpr64(destination, source);
      ASSERT_EQ(instruction.count, 1);
      EXPECT_EQ(instruction.encodings[0],
                0xca000000u | (source << 16) | (destination << 5) | destination);
    }
  }
}

TEST(ARM64EmitterEncoding, not_gpr64) {
  for (int operand = 0; operand < 31; ++operand) {
    const auto instruction = IGen::ARM64::not_gpr64(operand);
    ASSERT_EQ(instruction.count, 1);
    EXPECT_EQ(instruction.encodings[0], 0xaa2003e0u | (operand << 16) | operand);
  }
}

TEST(ARM64EmitterEncoding, load_symbol_value_to_sp) {
  // .load-sym sp *kernel-sp* must leave SP untouched until the value is loaded. (AI-assisted)
  for (const bool sign_extend : {false, true}) {
    const auto instruction =
        IGen::ARM64::load_symbol_value(SP, X21, X22, X17, 0x12345678, sign_extend);
    ASSERT_EQ(instruction.count, 7);
    EXPECT_EQ(instruction.offset_of_disp(), 8);
    EXPECT_EQ(instruction.get_disp_size(), 4);
    EXPECT_EQ(instruction.encodings[0], 0x8b1602b0u);  // add x16, x21, x22
    EXPECT_EQ(instruction.encodings[1], 0x14000002u);  // b over literal
    EXPECT_EQ(instruction.encodings[2], 0x12345678u);
    EXPECT_EQ(instruction.encodings[3], 0x98fffff1u);  // ldrsw x17, preceding literal
    EXPECT_EQ(instruction.encodings[4], 0x8b110210u);  // add x16, x16, x17
    EXPECT_EQ(instruction.encodings[5], sign_extend ? 0xb8800210u : 0xb8400210u);
    EXPECT_EQ(instruction.encodings[6], 0x9100021fu);  // mov sp, x16
  }
}

TEST(ARM64EmitterEncoding, vector_float_math) {
  // Opcode references independently assembled by Android NDK clang. (AI-assisted)
  const std::array<InstructionARM64 (*)(Register, Register, Register), 6> emitters = {
      IGen::ARM64::add_vf, IGen::ARM64::sub_vf, IGen::ARM64::mul_vf,
      IGen::ARM64::min_vf, IGen::ARM64::max_vf, IGen::ARM64::div_vf};
  const std::array<u32, 6> opcodes = {
      0x4e20d400, 0x4ea0d400, 0x6e20dc00, 0x4ea0f400, 0x4e20f400, 0x6e20fc00};
  for (size_t operation = 0; operation < emitters.size(); ++operation) {
    for (int destination = 0; destination < 32; ++destination) {
      for (int source = 0; source < 32; ++source) {
        const int other = 31 - source;
        const auto instruction = emitters[operation](
            Register(destination, true), Register(source, true), Register(other, true));
        ASSERT_EQ(instruction.count, 1);
        EXPECT_EQ(instruction.encodings[0],
                  opcodes[operation] | (other << 16) | (source << 5) | destination);
      }
    }
  }
  const std::array<InstructionARM64 (*)(Register, Register), 3> unary = {
      IGen::ARM64::sqrt_vf, IGen::ARM64::itof_vf, IGen::ARM64::ftoi_vf};
  const std::array<u32, 3> unary_opcodes = {0x6ea1f880, 0x4e21d880, 0x4ea1b880};
  for (size_t operation = 0; operation < unary.size(); ++operation) {
    const auto instruction = unary[operation](Register(V0, true), Register(V4, true));
    ASSERT_EQ(instruction.count, 1);
    EXPECT_EQ(instruction.encodings[0], unary_opcodes[operation]);
  }
  const std::array<Register::VF_ELEMENT, 4> lanes = {
      Register::VF_ELEMENT::X, Register::VF_ELEMENT::Y,
      Register::VF_ELEMENT::Z, Register::VF_ELEMENT::W};
  for (size_t lane = 0; lane < lanes.size(); ++lane) {
    const auto instruction =
        IGen::ARM64::splat_vf(Register(V0, true), Register(V4, true), lanes[lane]);
    ASSERT_EQ(instruction.count, 1);
    EXPECT_EQ(instruction.encodings[0], 0x4e040480u | (lane << 19));
  }
  for (int destination = 0; destination < 32; ++destination) {
    for (int source = 0; source < 32; ++source) {
      const auto instruction =
          IGen::ARM64::mov_vf_vf(Register(destination, true), Register(source, true));
      ASSERT_EQ(instruction.count, 1);
      EXPECT_EQ(instruction.encodings[0],
                0x4ea01c00u | (source << 16) | (source << 5) | destination);
    }
  }

  // Export actual emitter output for native finite-value checks, including negative values.
  // The wrapper uses only caller-saved registers and independently checked LDR/STR/RET opcodes.
  std::vector<u32> code = {0x3dc00000, 0x3dc00021};
  for (size_t operation = 0; operation < emitters.size(); ++operation) {
    code.push_back(emitters[operation](Register(V2, true), Register(V0, true),
                                       Register(V1, true)).encodings[0]);
    code.push_back(0x3d800042u | (operation << 10));
  }
  // V0 deliberately contains other bits: a MOV must not accidentally OR them in.
  code.push_back(IGen::ARM64::mov_vf_vf(Register(V2, true), Register(V1, true)).encodings[0]);
  code.push_back(0x3d801842);  // str q2, [x2, #96]
  code.push_back(0xd65f03c0);
  const auto path = file_util::get_file_path({"out", "arm64-runtime-generated", "vector-math.bin"});
  file_util::create_dir_if_needed_for_file(path);
  file_util::write_binary_file(path, code.data(), code.size() * sizeof(u32));
}

TEST(ARM64EmitterEncoding, vector_layout) {
  // Exercise every blend mask and shuffle control, including aliased destinations. (AI-assisted)
  for (const int source_base : {0, 14}) {
    std::vector<u32> code = {0xadbf3fee};  // stp q14, q15, [sp, #-32]!
    const auto append = [&](const InstructionARM64& instruction) {
      for (int index = 0; index < instruction.count; ++index) {
        code.push_back(instruction.encodings[index]);
      }
    };
    u32 output_index = 0;
    for (int mask = 0; mask < 16; ++mask) {
      for (int destination = 0; destination < 3; ++destination) {
        const int dst = destination < 2 ? source_base + destination : 2;
        code.push_back(0x3dc00000u | source_base);
        code.push_back(0x3dc00020u | (source_base + 1));
        append(IGen::ARM64::blend_vf(Register(dst, true), Register(source_base, true),
                                     Register(source_base + 1, true), mask));
        code.push_back(0x3d800040u | (output_index++ << 10) | dst);
      }
    }
    for (int control = 0; control < 256; ++control) {
      for (int destination = 0; destination < 2; ++destination) {
        const int dst = source_base + destination;
        code.push_back(0x3dc00000u | source_base);
        const auto shuffle =
            IGen::ARM64::swizzle_vf(Register(dst, true), Register(source_base, true), control);
        ASSERT_EQ(shuffle.count, 7);
        EXPECT_EQ(shuffle.encodings[6], 0x4e1e0000u | (source_base << 5) | dst);
        append(shuffle);
        code.push_back(0x3d800040u | (output_index++ << 10) | dst);
      }
    }
    code.push_back(0xacc13fee);  // ldp q14, q15, [sp], #32
    code.push_back(0xd65f03c0);
    const auto path = file_util::get_file_path(
        {"out", "arm64-runtime-generated",
         source_base == 0 ? "vector-layout.bin" : "vector-layout-spill.bin"});
    file_util::create_dir_if_needed_for_file(path);
    file_util::write_binary_file(path, code.data(), code.size() * sizeof(u32));
  }
}

TEST(ARM64EmitterEncoding, packed_interleave) {
  // PS2 PEXT operations interleave halves (ZIP), not even/odd lanes (UZP). (AI-assisted)
  const std::array<InstructionARM64 (*)(Register, Register, Register), 8> emitters = {
      IGen::ARM64::pextlb_swapped, IGen::ARM64::pextub_swapped, IGen::ARM64::pextlh_swapped,
      IGen::ARM64::pextuh_swapped, IGen::ARM64::pextlw_swapped, IGen::ARM64::pextuw_swapped,
      IGen::ARM64::pcpyld_swapped, IGen::ARM64::pcpyud};
  std::vector<u32> code;
  for (size_t operation = 0; operation < emitters.size(); ++operation) {
    const u32 opcode = 0x4e003800u | ((operation / 2) << 22) | ((operation & 1) << 14);
    for (int destination = 0; destination < 32; ++destination) {
      for (int source = 0; source < 32; ++source) {
        const int other = 31 - source;
        const auto instruction = emitters[operation](Register(destination, true),
            Register(source, true), Register(other, true));
        ASSERT_EQ(instruction.count, 1);
        EXPECT_EQ(instruction.encodings[0],
                  opcode | (other << 16) | (source << 5) | destination);
      }
    }
    for (int destination = 0; destination < 3; ++destination) {
      code.push_back(0x3dc00000);
      code.push_back(0x3dc00021);
      code.push_back(emitters[operation](Register(destination, true), Register(V0, true),
                                         Register(V1, true)).encodings[0]);
      code.push_back(0x3d800040u | ((operation * 3 + destination) << 10) | destination);
    }
  }
  code.push_back(0xd65f03c0);
  const auto path = file_util::get_file_path({"out", "arm64-runtime-generated", "packed-interleave.bin"});
  file_util::create_dir_if_needed_for_file(path);
  file_util::write_binary_file(path, code.data(), code.size() * sizeof(u32));
}

TEST(ARM64EmitterEncoding, scalar_float_math) {
  const std::array<InstructionARM64 (*)(Register, Register), 6> emitters = {
      IGen::ARM64::add_f32_f32, IGen::ARM64::sub_f32_f32, IGen::ARM64::mul_f32_f32,
      IGen::ARM64::div_f32_f32, IGen::ARM64::min_f32_f32, IGen::ARM64::max_f32_f32};
  const std::array<u32, 6> opcodes = {
      0x1e202800, 0x1e203800, 0x1e200800, 0x1e201800, 0x1e205800, 0x1e204800};
  std::vector<u32> code = {0xbd400000, 0xbd400021};  // ldr s0, [x0]; ldr s1, [x1]
  for (size_t operation = 0; operation < emitters.size(); ++operation) {
    for (int destination = 0; destination < 32; ++destination) {
      for (int source = 0; source < 32; ++source) {
        const auto instruction = emitters[operation](Register(destination, true), Register(source, true));
        ASSERT_EQ(instruction.count, 1);
        EXPECT_EQ(instruction.encodings[0],
                  opcodes[operation] | (source << 16) | (destination << 5) | destination);
      }
    }
    code.push_back(0x1e204002);  // fmov s2, s0
    code.push_back(emitters[operation](Register(V2, true), Register(V1, true)).encodings[0]);
    code.push_back(0xbd000042u | (operation << 10));
  }
  for (int destination = 0; destination < 32; ++destination) {
    for (int source = 0; source < 32; ++source) {
      const auto instruction = IGen::ARM64::sqrt_f32(Register(destination, true), Register(source, true));
      ASSERT_EQ(instruction.count, 1);
      EXPECT_EQ(instruction.encodings[0], 0x1e21c000u | (source << 5) | destination);
    }
  }
  code.push_back(IGen::ARM64::sqrt_f32(Register(V2, true), Register(V1, true)).encodings[0]);
  code.push_back(0xbd001842);  // str s2, [x2, #24]
  code.push_back(0xd65f03c0);
  const auto path = file_util::get_file_path({"out", "arm64-runtime-generated", "scalar-math.bin"});
  file_util::create_dir_if_needed_for_file(path);
  file_util::write_binary_file(path, code.data(), code.size() * sizeof(u32));
}

TEST(ARM64EmitterReturn, early_return_register_assignment) {
  // No manual gRegInfo save/restore needed: Compiler's constructor always resets it from scratch
  // based on the InstructionSet it's given, so the next test's own Compiler construction fixes
  // it back up before that test uses it - and referencing the gRegInfo global directly from this
  // test binary (across the compiler.dll boundary) doesn't link reliably on Windows anyway
  // (CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS doesn't export plain data symbols the same way it does
  // functions).
  Compiler compiler(GameVersion::Jak1, InstructionSet::ARM64);
  const auto source = compiler.get_goos().reader.read_from_string(R"(
    (defun early-return-first ((choice int) (callback (function int int)))
      (when (callback choice) (return-from #f 37))
      91)
    (defun early-return-second ((choice int) (callback (function int int)))
      (when (callback choice) (return-from #f 43))
      97)
    ;; Match the loader's loop, optional reload, and fallback call. (AI-assisted)
    (defun early-return-loop ((count int) (reload symbol)
                             (callback (function int int)))
      (dotimes (index count)
        (when (= (callback index) 37)
          (when reload (callback count))
          (return-from #f (+ index 100))))
      (callback -1))
    (deftype early-return-directory (basic)
      ((count int) (reload symbol) (callback (function int int)))
      (:methods (lookup (_type_) int)))
    (defmethod lookup ((this early-return-directory))
      (dotimes (index (-> this count))
        (when (= ((-> this callback) index) 37)
          (when (-> this reload) ((-> this callback) (-> this count)))
          (return-from #f (+ index 100))))
      ((-> this callback) -1))
  )");
  auto compiled = compiler.compile_object_file("early-return-register-assignment", source, true);
  compiler.color_object_file(compiled);
  int checked_functions = 0;
  for (const auto& function : compiled->functions()) {
    if (function->name().find("early-return-") == std::string::npos ||
        function->name().find("autogenerated-") == 0) {
      continue;
    }
    ++checked_functions;
    const auto& instructions = function->code();
    int return_id = -1;
    int return_index = -1;
    int merged_value_id = -1;
    for (size_t index = 0; index < instructions.size(); ++index) {
      if (auto* return_instruction = dynamic_cast<IR_Return*>(instructions[index].get())) {
        return_id = return_instruction->to_rai().write.at(0).id;
        merged_value_id = return_instruction->to_rai().read.at(0).id;
        return_index = index;
      }
    }
    ASSERT_GE(return_id, 0);
    int merged_writes = 0;
    for (size_t index = 0; index < instructions.size(); ++index) {
      const auto rai = instructions[index]->to_rai();
      // Every exit must execute the ABI move, including any reload of a spilled value.
      // Checking just its physical register misses early exits that bypass it. (AI-assisted)
      for (const auto target : rai.jumps) {
        EXPECT_LE(target, return_index) << function->name() << " IR " << index;
      }
      if (rai.writes(merged_value_id)) {
        ++merged_writes;
      }
      if (instructions[index]->to_rai().writes(return_id)) {
        const auto& assignment = function->alloc_result().ass_as_ranges.at(return_id).get(index);
        EXPECT_EQ(assignment.reg, Register(X8)) << function->name() << " IR " << index;
        EXPECT_FALSE(assignment.spilled) << function->name() << " IR " << index;
      }
    }
    EXPECT_EQ(merged_writes, 2);
  }
  EXPECT_EQ(checked_functions, 4);
}

TEST(ARM64EmitterReturn, stacked_continuation) {
  // Export actual compiler output for the device regression, not handwritten substitutes.
  // The plain jump is also exported to reproduce the old failure safely. (AI-assisted)
  Compiler compiler(GameVersion::Jak1, InstructionSet::ARM64);
  const auto source = compiler.get_goos().reader.read_from_string(R"(
    (defun tail-jump-plain ((target uint) (continuation uint))
      (declare (asm-func none))
      (rlet ((discard :reg rax))
        (.pop discard)
        (.push continuation)
        (.jr target)))
    (defun tail-jump-stacked ((target uint) (continuation uint))
      (declare (asm-func none))
      (rlet ((discard :reg rax))
        (.pop discard)
        (.push continuation)
        (.jr :stack-return #t target)))
  )");
  const std::string object_name = "stacked-continuation";
  auto compiled = compiler.compile_object_file(object_name, source, true);
  compiler.color_object_file(compiled);
  auto& debug = compiler.get_debugger().get_debug_info_for_object(object_name);
  CodeGenerator generator(compiled, &debug, GameVersion::Jak1, InstructionSet::ARM64);
  generator.run(&compiler.type_system());
  for (const std::string name : {"tail-jump-plain", "tail-jump-stacked"}) {
    const auto& code = debug.function_by_name(name).generated_code;
    int stack_return_jumps = 0;
    for (size_t offset = 0; offset + 8 <= code.size(); offset += 4) {
      u32 pop, branch;
      std::memcpy(&pop, code.data() + offset, sizeof(pop));
      std::memcpy(&branch, code.data() + offset + 4, sizeof(branch));
      if (pop == 0xf84107feu && (branch & 0xfffffc1fu) == 0xd61f0000u) {
        ++stack_return_jumps;
      }
    }
    EXPECT_EQ(stack_return_jumps, name == "tail-jump-stacked" ? 1 : 0);
    auto path = file_util::get_file_path({"out", "arm64-runtime-generated", name + ".bin"});
    file_util::create_dir_if_needed_for_file(path);
    file_util::write_binary_file(path, code.data(), code.size());
  }
}

TEST(ARM64EmitterReturn, wide_value_across_call) {
  Compiler compiler(GameVersion::Jak1, InstructionSet::ARM64);
  const auto source = compiler.get_goos().reader.read_from_string(R"(
    (defun wide-value-across-call ((value uint128) (callback (function int int)))
      (callback 17)
      value)
  )");
  const std::string object_name = "wide-value-across-call";
  auto compiled = compiler.compile_object_file(object_name, source, true);
  int calls_checked = 0;
  for (const auto& function : compiled->functions()) {
    if (function->name() != object_name) {
      continue;
    }
    for (const auto& instruction : function->code()) {
      if (auto* call = dynamic_cast<IR_FunctionCall*>(instruction.get())) {
        ++calls_checked;
        const auto clobbers = call->to_rai().clobber;
        EXPECT_EQ(clobbers.size(), 24);
        std::array<bool, 8> vector_clobbers = {};
        for (const auto reg : clobbers) {
          EXPECT_GE(reg.id(), 0);
          EXPECT_LT(reg.id(), 32);
          if (reg.is_arm64_vector_tag()) {
            ASSERT_LT(reg.id(), 8);
            vector_clobbers[reg.id()] = true;
          }
        }
        for (const bool clobbered : vector_clobbers) {
          EXPECT_TRUE(clobbered);
        }
      }
    }
  }
  EXPECT_EQ(calls_checked, 1);
  compiler.color_object_file(compiled);
  auto& debug = compiler.get_debugger().get_debug_info_for_object(object_name);
  CodeGenerator generator(compiled, &debug, GameVersion::Jak1, InstructionSet::ARM64);
  generator.run(&compiler.type_system());
  const auto& code = debug.function_by_name(object_name).generated_code;
  ASSERT_FALSE(code.empty());
  const auto path = file_util::get_file_path({"out", "arm64-runtime-generated", object_name + ".bin"});
  file_util::create_dir_if_needed_for_file(path);
  file_util::write_binary_file(path, code.data(), code.size());
}

TEST(ARM64EmitterIntegerMath, add_gpr64_imm8s) {
#ifndef __aarch64__
  GTEST_SKIP() << "ARM64 machine code requires an ARM64 host";
#endif
  CodeTester tester(InstructionSet::ARM64);
  tester.init_code_buffer(256);

  std::vector<s64> vals = {0, 1, -1, INT32_MIN, INT32_MAX, INT64_MIN, INT64_MAX};
  std::vector<s64> imms = {0, 1, -1, INT8_MIN, INT8_MAX};

  // test the ones that aren't sp
  for (int i = 0; i < 16; i++) {
    if (i == SP) {
      continue;
    }

    for (auto val : vals) {
      for (auto imm : imms) {
        auto expected = val + imm;

        tester.clear();
        tester.emit_push_all_gprs(true);

        // move initial value to register
        tester.emit(IGen::mov_gpr64_gpr64(tester.generator(), i, X0));
        // do the add
        tester.emit(IGen::add_gpr64_imm8s(tester.generator(), i, imm));
        // move for return
        tester.emit(IGen::mov_gpr64_gpr64(tester.generator(), X0, i));

        tester.emit_pop_all_gprs(true);
        tester.emit_return();

        execute_ret_tester(tester, val, expected);
      }
    }
  }
  tester.clear();
}
