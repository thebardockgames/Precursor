#pragma once

#include <cstring>

#include "common/common_types.h"

#ifdef __aarch64__
inline void emit_arm64_word(u8*& cursor, u32 word) {
  std::memcpy(cursor, &word, sizeof(word));
  cursor += sizeof(word);
}

inline void emit_arm64_mov64(u8*& cursor, u32 register_id, u64 value) {
  for (u32 halfword = 0; halfword < 4; ++halfword) {
    const u32 immediate = (value >> (halfword * 16)) & 0xffff;
    const u32 opcode = halfword == 0 ? 0xd2800000u : 0xf2800000u;
    emit_arm64_word(cursor, opcode | (halfword << 21) | (immediate << 5) | register_id);
  }
}

// GOAL's nothing/zero-func stubs are called directly, without a C dispatcher. (AI-assisted)
inline void emit_arm64_return_stub(u8* memory, bool return_zero) {
  u8* cursor = memory;
  if (return_zero) {
    emit_arm64_word(cursor, 0xd2800008);  // mov x8, #0 (GOAL return register)
  }
  emit_arm64_word(cursor, 0xd65f03c0);  // ret
  __builtin___clear_cache(reinterpret_cast<char*>(memory), reinterpret_cast<char*>(cursor));
}

inline void emit_arm64_c_call_trampoline(u8* memory,
                                         void* function,
                                         void* dispatcher,
                                         bool arg3_is_pp) {
  u8* cursor = memory;
  emit_arm64_mov64(cursor, 9, reinterpret_cast<u64>(function));
  emit_arm64_word(cursor, 0xf81f0fe9);
  emit_arm64_mov64(cursor, 9, reinterpret_cast<u64>(dispatcher));
  if (arg3_is_pp) {
    emit_arm64_word(cursor, 0xaa1403e3);
  }
  emit_arm64_word(cursor, 0xd61f0120);
  __builtin___clear_cache(reinterpret_cast<char*>(memory), reinterpret_cast<char*>(cursor));
}

inline void emit_arm64_mips2c_trampoline(u8* memory,
                                         void* function,
                                         void* dispatcher,
                                         u32 stack_size) {
  u8* cursor = memory;
  emit_arm64_mov64(cursor, 9, reinterpret_cast<u64>(function));
  emit_arm64_mov64(cursor, 8, stack_size);
  emit_arm64_mov64(cursor, 16, reinterpret_cast<u64>(dispatcher));
  emit_arm64_word(cursor, 0xd61f0200);
  __builtin___clear_cache(reinterpret_cast<char*>(memory), reinterpret_cast<char*>(cursor));
}
#endif
