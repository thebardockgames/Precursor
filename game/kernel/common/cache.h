#pragma once

// Publish freshly written machine code before executing it on ARM64. (AI-assisted)
inline void CacheFlush(void* memory, int size) {
#ifdef __aarch64__
  if (size > 0) {
    auto* begin = static_cast<char*>(memory);
    __builtin___clear_cache(begin, begin + size);
  }
#else
  (void)memory;
  (void)size;
#endif
}
