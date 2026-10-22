#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <unistd.h>

#include "game/kernel/common/arm64_trampoline.h"
#include "game/kernel/common/cache.h"
#include "game/kernel/common/kprint.h"
#include <sys/mman.h>

extern "C" {
void _arg_call_arm64();
void _stack_call_arm64();
void _mips2c_call_arm64();
u64 _call_goal_asm_arm64(u64, u64, u64, void*, u64, u64);
u64 _call_goal8_asm_arm64(void*, const u64*, u64, u64, u64, u64);
u64 _call_goal_on_stack_asm_arm64(void*, u64, u64, void*, u64, u64);
u64 probe_goal_call(void*, const u64*, u128*, u64, u64, u64);
void probe_goal_return();
u64 probe_c_call(void*, const u64*, u128*);
void probe_goal_scratch_return();
u64 probe_stack_return(void*);
void probe_wide_return(void*, const u128*, u128*);
volatile u64 g_mips2c_debug_last_exec = 0;
volatile u64 g_mips2c_debug_last_ret = 0;
volatile u64 g_mips2c_debug_call_count = 0;
}

namespace {
constexpr u64 kResult = 0xfedcba9876543210;
constexpr u64 kProcess = 0x123456789abcdef0;
constexpr u64 kSymbols = 0x1020304050607080;
constexpr std::array<u64, 8> kArguments = {0x100000001, 0x200000002, 0x300000003, 0x400000004,
                                           0x500000005, 0x600000006, 0x700000007, 0x800000008};
std::array<u64, 8> received_arguments;
bool context_valid = false;

bool test_code_cache() {
  const size_t page_size = sysconf(_SC_PAGESIZE);
  auto* code = static_cast<u32*>(mmap(nullptr, page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (code == MAP_FAILED) {
    std::perror("mmap code cache");
    return false;
  }
  bool passed = true;
  for (u32 value = 1; value <= 256; ++value) {
    // Reuse an executable allocation without mprotect potentially flushing it for us.
    // MOV W0, #value; RET. The kernel calls the same flush helper after linking.
    code[0] = 0x52800000u | (value << 5);
    code[1] = 0xd65f03c0;
    CacheFlush(code, 8);
    const auto result = reinterpret_cast<u32 (*)()>(code)();
    if (result != value) {
      std::printf("code cache result=%u expected=%u\n", result, value);
      passed = false;
      break;
    }
  }
  munmap(code, page_size);
  return passed;
}

u64 native_direct(u64 arg0, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6, u64 arg7) {
  received_arguments = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7};
  return kResult;
}

u64 native_array(const u64* arguments) {
  std::memcpy(received_arguments.data(), arguments, sizeof(received_arguments));
  return kResult;
}

u64 native_mips2c(void* context) {
  auto* registers = static_cast<u128*>(context);
  for (size_t index = 0; index < kArguments.size(); ++index) {
    received_arguments[index] = registers[index + 4].du64[0];
  }
  context_valid = registers[22].du64[0] == kProcess && registers[23].du64[0] == kSymbols &&
                  registers[29].du64[0] == reinterpret_cast<u64>(context);
  registers[2].du64[0] = kResult;
  return 0xdeadbeef;
}

bool test_generated_code(const char* filename, std::string_view test_case) {
  // Exhaustive layout fixtures are larger than a page. Keep a bounded, page-aligned mapping.
  const size_t page_size = 16 * sysconf(_SC_PAGESIZE);
  auto* code = static_cast<u8*>(
      mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (code == MAP_FAILED) {
    std::perror("mmap");
    return false;
  }
  auto* file = std::fopen(filename, "rb");
  if (!file) {
    std::perror(filename);
    munmap(code, page_size);
    return false;
  }
  const size_t size = std::fread(code, 1, page_size, file);
  const bool valid = size > 0 && size < page_size && !std::ferror(file);
  std::fclose(file);
  __builtin___clear_cache(reinterpret_cast<char*>(code), reinterpret_cast<char*>(code + size));
  bool passed = false;
  if (valid && mprotect(code, page_size, PROT_READ | PROT_EXEC) == 0) {
    if (test_case == "stack-return") {
      const u64 result = probe_stack_return(code);
      std::printf("continuation result=%llu expected=99\n", static_cast<unsigned long long>(result));
      passed = result == 99;
    } else if (test_case == "spilled-store") {
      // Small GOAL offsets into a canary-filled buffer make the old 0/1-as-pointer
      // failure observable without intentionally crashing the test process. (AI-assisted)
      passed = true;
      for (const u64 skip : {0, 1}) {
        for (const u64 value : {0, 1, 0x7f, 0xff}) {
          std::array<u8, 512> memory;
          memory.fill(0xa5);
          const u64 result =
              _call_goal_asm_arm64(16, value, skip, code, 0, reinterpret_cast<u64>(memory.data()));
          passed &= result == (skip ? 77 : 99);
          for (size_t byte = 0; byte < memory.size(); ++byte) {
            const u8 expected = !skip && byte == 76 ? value : 0xa5;
            passed &= memory[byte] == expected;
          }
        }
      }
    } else if (test_case == "collision-sphere-box") {
      // Test all faces, touching, inside, negative coordinates and irrelevant W.
      // Matches Jak 2's integer broad phase, not exact sphere/triangle contact. (AI-assisted)
      struct alignas(16) Inputs {
        std::array<float, 4> sphere;
        std::array<s32, 4> lower;
        std::array<s32, 4> upper;
      } inputs;
      passed = true;
      unsigned checks = 0;
      for (const s32 origin : {-100000, 0, 100000}) {
        for (int axis = 0; axis < 3; ++axis) {
          for (const float position : {-12.f, -11.f, -10.f, 0.f, 10.f, 11.f, 12.f}) {
            inputs.sphere = {float(origin), float(origin), float(origin), 1.f};
            inputs.sphere[axis] += position;
            inputs.lower = {origin - 10, origin - 10, origin - 10, 12345};
            inputs.upper = {origin + 10, origin + 10, origin + 10, -12345};
            const u64 expected = position >= -11.f && position <= 11.f;
            const u64 result =
                _call_goal_asm_arm64(0, 16, 0, code, 0, reinterpret_cast<u64>(&inputs));
            if (result != expected) {
              std::printf("sphere-box origin=%d axis=%d position=%g result=%llu expected=%llu\n",
                          origin, axis, position, (unsigned long long)result,
                          (unsigned long long)expected);
              passed = false;
            }
            ++checks;
          }
        }
      }
      std::printf("sphere-box checks=%u\n", checks);
    } else if (test_case == "collision-matrix-inverse") {
      // Real Jak 2 GOAL inversion under spill pressure; compare to a double-precision
      // scalar inverse of a scaled rotation, including ignored input W lanes. (AI-assisted)
      alignas(16) std::array<float, 36> memory;
      passed = true;
      unsigned checks = 0;
      for (float angle : {0.f, 0.3f, 1.f}) {
        for (float scale : {0.5f, 2.f}) {
          for (float origin : {0.f, 2077187.75f}) {
            for (bool in_place : {false, true}) {
              memory.fill(12345.f);
              const float c = std::cos(angle), s = std::sin(angle);
              const std::array<float, 20> input = {
                  scale * c, 0,        scale * s, 1,     0,      scale, 0, 0, -scale * s, 0,
                  scale * c, 3.96875f, origin,    32832, 392631, 0,     0, 0, 0,          1};
              std::memcpy(memory.data() + 16, input.data(), sizeof(input));
              const double xx = input[0], zz = input[2];
              const double norm = xx * xx + zz * zz;
              std::array<double, 16> expected = {xx / norm, 0, -zz / norm, 0, 0, 1. / scale, 0, 0,
                                                 zz / norm, 0, xx / norm,  0, 0, 0,          0, 1};
              for (int row = 0; row < 3; ++row) {
                expected[12 + row] = -(expected[row] * input[12] + expected[4 + row] * input[13] +
                                       expected[8 + row] * input[14]);
              }
              const u64 output_offset = in_place ? 64 : 0;
              _call_goal_asm_arm64(output_offset, 64, 0, code, 0,
                                   reinterpret_cast<u64>(memory.data()));
              for (int lane = 0; lane < 16; ++lane) {
                const float result = memory[output_offset / 4 + lane];
                if (!std::isfinite(result) ||
                    std::abs(result - expected[lane]) > 1e-4 + 3e-6 * std::abs(expected[lane])) {
                  std::printf(
                      "inverse angle=%g scale=%g origin=%g in-place=%d lane=%d result=%g "
                      "expected=%g\n",
                      angle, scale, origin, in_place, lane, result, expected[lane]);
                  passed = false;
                }
              }
              if (!in_place)
                passed &= std::memcmp(memory.data() + 16, input.data(), sizeof(input)) == 0;
              ++checks;
            }
          }
        }
      }
      std::printf("matrix-inverse checks=%u\n", checks);
    } else if (test_case == "wide-return") {
      alignas(16) u128 input, output;
      input.du64[0] = 0x123456789abcdef0;
      input.du64[1] = 0xfedcba9876543210;
      output.du64[0] = output.du64[1] = 0;
      probe_wide_return(code, &input, &output);
      passed = std::memcmp(&input, &output, sizeof(input)) == 0;
      std::printf("wide return=%016llx/%016llx\n", static_cast<unsigned long long>(output.du64[1]),
                  static_cast<unsigned long long>(output.du64[0]));
    } else if (test_case == "vector-transform" || test_case == "matrix-multiply") {
      alignas(16) const std::array<float, 16> left = {1.5f, -2.f, 3.f, 1.f, 5.f, 2.f,  -1.f, 2.f,
                                                      -4.f, 0.5f, 2.f, 1.f, 3.f, -1.f, 4.f,  0.5f};
      alignas(16) const std::array<float, 16> right = {2.f, 1.f,  -3.f, 0.f, -1.f, 4.f, 2.f,  0.f,
                                                       3.f, -2.f, 1.f,  0.f, 5.f,  7.f, -4.f, 1.f};
      alignas(16) std::array<float, 16> output = {};
      _call_goal_asm_arm64(reinterpret_cast<u64>(output.data()), reinterpret_cast<u64>(left.data()),
                           reinterpret_cast<u64>(right.data()), code, 0, 0);
      const int columns = test_case == "vector-transform" ? 1 : 4;
      passed = true;
      for (int column = 0; column < columns; ++column) {
        for (int lane = 0; lane < 4; ++lane) {
          float expected = 0;
          for (int inner = 0; inner < 4; ++inner) {
            expected += left[4 * column + inner] * right[4 * inner + lane];
          }
          if (output[4 * column + lane] != expected) {
            std::printf("transform column=%d lane=%d result=%g expected=%g\n", column, lane,
                        output[4 * column + lane], expected);
            passed = false;
          }
        }
      }
    } else if (test_case == "packed-interleave") {
      std::array<u8, 16> left, right;
      for (size_t index = 0; index < 16; ++index) {
        left[index] = index;
        right[index] = 0x80 + index;
      }
      std::array<u8, 8 * 3 * 16> output = {};
      reinterpret_cast<void (*)(const u8*, const u8*, u8*)>(code)(left.data(), right.data(),
                                                                  output.data());
      passed = true;
      for (int operation = 0; operation < 8; ++operation) {
        const int element_size = 1 << (operation / 2);
        const int start = (operation & 1) ? 8 : 0;
        for (int destination = 0; destination < 3; ++destination) {
          for (int byte = 0; byte < 16; ++byte) {
            const int element = byte / element_size;
            const int source_byte = start + (element / 2) * element_size + byte % element_size;
            const u8 expected = (element & 1) ? right[source_byte] : left[source_byte];
            if (output[(operation * 3 + destination) * 16 + byte] != expected) {
              std::printf("interleave operation=%d destination=%d byte=%d failed\n", operation,
                          destination, byte);
              passed = false;
              break;
            }
          }
        }
      }
    } else if (test_case == "vector-layout") {
      const std::array<u32, 4> left = {0x12345678, 0x87654321, 0xabcdef01, 0x10fedcba};
      const std::array<u32, 4> right = {0x80ff00ee, 0x11223344, 0xaabbccdd, 0x55667788};
      std::array<u32, (16 * 3 + 256 * 2) * 4> output = {};
      reinterpret_cast<void (*)(const u32*, const u32*, u32*)>(code)(left.data(), right.data(),
                                                                     output.data());
      size_t index = 0;
      passed = true;
      for (int mask = 0; mask < 16; ++mask) {
        for (int destination = 0; destination < 3; ++destination) {
          for (int lane = 0; lane < 4; ++lane) {
            const u32 expected = (mask & (1 << lane)) ? right[lane] : left[lane];
            if (output[index++] != expected) {
              std::printf("blend mask=%d destination=%d lane=%d failed\n", mask, destination, lane);
              passed = false;
            }
          }
        }
      }
      for (int control = 0; control < 256; ++control) {
        for (int destination = 0; destination < 2; ++destination) {
          for (int lane = 0; lane < 4; ++lane) {
            if (output[index++] != left[(control >> (2 * lane)) & 3]) {
              std::printf("shuffle control=%d destination=%d lane=%d failed\n", control, destination, lane);
              passed = false;
            }
          }
        }
      }
    } else if (test_case == "scalar-math") {
      const float left = -4.f, right = 16.f;
      const std::array<float, 7> expected = {12.f, -20.f, -64.f, -0.25f, -4.f, 16.f, 4.f};
      std::array<float, 7> output = {};
      reinterpret_cast<void (*)(const float*, const float*, float*)>(code)(
          &left, &right, output.data());
      passed = true;
      for (size_t index = 0; index < output.size(); ++index) {
        if (output[index] != expected[index]) {
          std::printf("scalar operation %zu result=%g expected=%g\n", index, output[index], expected[index]);
          passed = false;
        }
      }
    } else {
      // Run the host emitter's six binary vector operations on real hardware. (AI-assisted)
      const std::array<float, 4> left = {-4.f, 1.5f, 16.f, 25.f};
      const std::array<float, 4> right = {2.f, -3.f, 4.f, 5.f};
      const std::array<float, 28> expected = {
          -2.f, -1.5f, 20.f, 30.f, -6.f, 4.5f, 12.f, 20.f,
          -8.f, -4.5f, 64.f, 125.f, -4.f, -3.f, 4.f, 5.f,
          2.f, 1.5f, 16.f, 25.f, -2.f, -0.5f, 4.f, 5.f,
          2.f, -3.f, 4.f, 5.f};
      std::array<float, 28> output = {};
      reinterpret_cast<void (*)(const float*, const float*, float*)>(code)(
          left.data(), right.data(), output.data());
      passed = true;
      for (size_t index = 0; index < output.size(); ++index) {
        if (output[index] != expected[index]) {
          std::printf("vector lane %zu result=%g expected=%g\n", index, output[index], expected[index]);
          passed = false;
        }
      }
    }
  }
  munmap(code, page_size);
  return passed;
}

bool test_trampoline(std::string_view test_case) {
  const size_t page_size = sysconf(_SC_PAGESIZE);
  auto* code = static_cast<u8*>(
      mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (code == MAP_FAILED) {
    std::perror("mmap");
    return false;
  }
  const bool is_mips2c = test_case == "mips2c";
  const bool is_stub = test_case == "nothing" || test_case == "zero";
  const bool is_array = test_case == "array";
  const bool is_pp = test_case == "pp";
  const std::array<u32, 4> stack_sizes = {0, 16, 128, 1024};
  bool passed = true;
  for (u32 stack_size : stack_sizes) {
    if (mprotect(code, page_size, PROT_READ | PROT_WRITE) != 0) {
      std::perror("mprotect writable");
      passed = false;
      break;
    }
    if (is_stub) {
      emit_arm64_return_stub(code, test_case == "zero");
    } else if (is_mips2c) {
      emit_arm64_mips2c_trampoline(code, reinterpret_cast<void*>(native_mips2c),
                                   reinterpret_cast<void*>(_mips2c_call_arm64), stack_size);
    } else {
      emit_arm64_c_call_trampoline(
          code,
          is_array ? reinterpret_cast<void*>(native_array) : reinterpret_cast<void*>(native_direct),
          is_array ? reinterpret_cast<void*>(_stack_call_arm64)
                   : reinterpret_cast<void*>(_arg_call_arm64),
          is_pp);
    }
    if (mprotect(code, page_size, PROT_READ | PROT_EXEC) != 0) {
      std::perror("mprotect executable");
      passed = false;
      break;
    }
    alignas(16) std::array<u128, 8> vectors;
    for (size_t index = 0; index < vectors.size(); ++index) {
      vectors[index].du64[0] = 0x1111222233330000 + index;
      vectors[index].du64[1] = 0xaaaabbbbcccc0000 + index;
    }
    const auto expected_vectors = vectors;
    auto expected_arguments = kArguments;
    if (is_pp) {
      expected_arguments[3] = kProcess;
    }
    const u64 result =
        probe_goal_call(code, kArguments.data(), vectors.data(), kProcess, kSymbols, 0);
    const u64 expected_result = test_case == "zero" ? 0 : (is_stub ? kArguments[0] : kResult);
    const bool return_valid = result == expected_result;
    const bool arguments_valid = is_stub || received_arguments == expected_arguments;
    const bool vectors_valid =
        std::memcmp(vectors.data(), expected_vectors.data(), sizeof(vectors)) == 0;
    std::printf("stack=%u return=%d arguments=%d vectors=%d context=%d\n", stack_size, return_valid,
                arguments_valid, vectors_valid, !is_mips2c || context_valid);
    passed &= return_valid && arguments_valid && vectors_valid && (!is_mips2c || context_valid);
  }
  munmap(code, page_size);
  return passed;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) {
    std::fprintf(
        stderr,
        "Usage: arm64-runtime-tests "
        "direct|array|pp|mips2c|goal|goal8|stack|nothing|zero|format|cache\n"
        "       arm64-runtime-tests goal-vectors|goal8-vectors|stack-vectors\n"
        "       arm64-runtime-tests collision-sphere-box|collision-matrix-inverse code.bin\n"
        "       arm64-runtime-tests "
        "stack-return|vector-math|wide-return|vector-layout|scalar-math|packed-interleave|spilled-"
        "store code.bin\n"
        "       arm64-runtime-tests vector-transform|matrix-multiply code.bin (manual game-code "
        "exports)\n");
    return 2;
  }
  const std::string_view test_case = argv[1];
  bool passed;
  if (test_case == "cache") {
    passed = test_code_cache();
  } else if (test_case == "format") {
    format_struct options;
    options.reset();
    passed = true;
    for (const auto option : options.data) {
      passed &= option == -1;
    }
    options.data[0] = 'x';
    options.data[1] = 0;
    passed &= std::strcmp(options.c_str(), "x") == 0;
  } else if ((test_case == "stack-return" || test_case == "vector-math" ||
              test_case == "wide-return" || test_case == "vector-layout" ||
              test_case == "scalar-math" || test_case == "vector-transform" ||
              test_case == "matrix-multiply" || test_case == "packed-interleave" ||
              test_case == "spilled-store" || test_case == "collision-sphere-box" ||
              test_case == "collision-matrix-inverse") &&
             argc == 3) {
    passed = test_generated_code(argv[2], test_case);
  } else if (test_case == "goal") {
    passed =
        _call_goal_asm_arm64(1, 2, 3, reinterpret_cast<void*>(probe_goal_return), 0, 0) == 0x5678;
  } else if (test_case == "goal8") {
    passed = _call_goal8_asm_arm64(reinterpret_cast<void*>(probe_goal_return), kArguments.data(), 0,
                                   0, 0, 0) == 0x5678;
  } else if (test_case == "stack") {
    alignas(16) std::array<u8, 16384> stack;
    passed =
        _call_goal_on_stack_asm_arm64(stack.data() + stack.size(), 0, 0,
                                      reinterpret_cast<void*>(probe_goal_return), 0, 0) == 0x5678;
  } else if (test_case == "goal-vectors" || test_case == "goal8-vectors" ||
             test_case == "stack-vectors") {
    // Protect the C ABI even for old code/stubs that overwrite saved vector registers.
    // Preserve native callers' values across all three entry points. (AI-assisted)
    alignas(16) std::array<u8, 16384> stack;
    std::array<u128, 8> vectors;
    for (size_t index = 0; index < vectors.size(); ++index) {
      vectors[index].du64[0] = 0x1234567800000000ull + index;
      vectors[index].du64[1] = 0xfedcba9800000000ull + index;
    }
    const auto expected = vectors;
    const auto target = reinterpret_cast<u64>(probe_goal_scratch_return);
    std::array<u64, 6> arguments = {1, 2, 3, target, 0, 0};
    void* bridge = reinterpret_cast<void*>(_call_goal_asm_arm64);
    if (test_case == "goal8-vectors") {
      bridge = reinterpret_cast<void*>(_call_goal8_asm_arm64);
      arguments = {target, reinterpret_cast<u64>(kArguments.data()), 0, 0, 0, 0};
    } else if (test_case == "stack-vectors") {
      bridge = reinterpret_cast<void*>(_call_goal_on_stack_asm_arm64);
      arguments[0] = reinterpret_cast<u64>(stack.data() + stack.size());
    }
    passed = probe_c_call(bridge, arguments.data(), vectors.data()) == 0x5678;
    for (size_t index = 0; index < vectors.size(); ++index) {
      if (std::memcmp(&vectors[index], &expected[index], sizeof(u128))) {
        std::printf("C-to-GOAL clobbered V%zu: %016llx/%016llx\n", index + 8,
                    (unsigned long long)vectors[index].du64[1],
                    (unsigned long long)vectors[index].du64[0]);
        passed = false;
      }
    }
  } else if (test_case == "direct" || test_case == "array" || test_case == "pp" ||
             test_case == "mips2c" || test_case == "nothing" || test_case == "zero") {
    passed = test_trampoline(test_case);
  } else {
    std::fprintf(stderr, "Unknown test: %s\n", argv[1]);
    return 2;
  }
  std::printf("%s: %s\n", argv[1], passed ? "PASS" : "FAIL");
  return passed ? 0 : 1;
}
