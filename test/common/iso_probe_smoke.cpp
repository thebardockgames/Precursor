// Read-only regression: real ISO identification and all six wrong-game pairings. (AI-assisted)
#include "scripts/android-port/apk/app/jni/iso_probe.h"
#include <cstdio>

int main(int argc, char** argv) {
  if (argc != 4) return 2;
  int passed = 0;
  for (int actual = 1; actual <= 3; actual++) {
    for (int selected = 1; selected <= 3; selected++) {
      std::unique_ptr<FILE, decltype(&fclose)> fp(fopen(argv[actual], "rb"), &fclose);
      if (!fp) { perror(argv[actual]); return 1; }
      const auto expected = "jak" + std::to_string(selected);
      try {
        auto info = probe_game_iso(fp.get(), expected);
        if (actual != selected || info.game_name != expected) return 1;
        printf("PASS ISO Jak %d accepted as %s\n", actual, expected.c_str());
      } catch (const std::exception& error) {
        if (actual == selected || std::string(error.what()).find("pero seleccionaste") == std::string::npos) {
          printf("FAIL %s\n", error.what()); return 1;
        }
        printf("PASS ISO Jak %d rejected as %s: %s\n", actual, expected.c_str(), error.what());
      }
      passed++;
    }
  }
  printf("%d checks passed; no extraction performed\n", passed);
  return 0;
}
