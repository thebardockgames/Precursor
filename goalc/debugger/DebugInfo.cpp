#include "DebugInfo.h"

#include <utility>

#include "fmt/format.h"

DebugInfo::DebugInfo(std::string obj_name) : m_obj_name(std::move(obj_name)) {}

std::string FunctionDebugInfo::disassemble_debug_info(bool* had_failure,
                                                      const goos::Reader* reader,
                                                      bool omit_ir) {
  std::string result = fmt::format("[{}]\n", name);
  {
    // TEMP DEBUG: dump raw bytes so they can be fed to a real AArch64 disassembler
    // (llvm-mc --disassemble --triple=aarch64), since disassemble_x86_function below
    // is x86-only and produces garbage for ARM64 code.
    std::string hex;
    for (auto b : generated_code) {
      hex += fmt::format("0x{:02x} ", b);
    }
    fmt::print("RAW_BYTES[{}] ({} bytes): {}\n", name, generated_code.size(), hex);
  }
  result += disassemble_x86_function(generated_code.data(), generated_code.size(), reader, 0x10000,
                                     0x10000, instructions, code_sources, ir_strings, had_failure,
                                     true, omit_ir);

  return result;
}

std::string DebugInfo::disassemble_all_functions(bool* had_failure,
                                                 const goos::Reader* reader,
                                                 bool omit_ir) {
  std::string result;
  for (auto& kv : m_functions) {
    result += kv.second.disassemble_debug_info(had_failure, reader, omit_ir) + "\n\n";
  }
  return result;
}

std::string DebugInfo::disassemble_function_by_name(const std::string& name,
                                                    bool* had_failure,
                                                    const goos::Reader* reader) {
  std::string result;
  for (auto& kv : m_functions) {
    if (kv.second.name == name) {
      result += kv.second.disassemble_debug_info(had_failure, reader, false) + "\n\n";
    }
  }
  return result;
}
