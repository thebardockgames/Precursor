// Standalone asset-free ISO parser regression test, runnable on Android. (AI-assisted)
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <vector>
#include "common/util/read_iso_file.h"
#include "common/repl/repl_wrapper.h"
#include "third-party/zstd/lib/common/xxhash.h"

namespace {
constexpr size_t sector = 2048;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void put32(std::vector<u8>& data, size_t at, u32 value) {
  for (int i = 0; i < 4; i++) data.at(at + i) = value >> (8 * i);
}
size_t record(std::vector<u8>& data, size_t at, const std::string& name, bool dir, u32 extent, u32 size) {
  size_t length = (33 + name.size() + 1) & ~size_t(1);
  data.at(at) = length;
  put32(data, at + 2, extent);
  put32(data, at + 10, size);
  data.at(at + 25) = dir ? 2 : 0;
  data.at(at + 32) = name.size();
  memcpy(data.data() + at + 33, name.data(), name.size());
  return length;
}
std::vector<u8> fixture(size_t length = 1024 * 1024 + 17) {
  std::vector<u8> data(22 * sector + length);
  data[16 * sector] = 1;
  memcpy(data.data() + 16 * sector + 1, "CD001", 5);
  data[16 * sector + 6] = 1;
  data[16 * sector + 129] = 8;
  record(data, 16 * sector + 156, std::string(1, 0), true, 20, sector);
  size_t at = 20 * sector;
  at += record(data, at, std::string(1, 0), true, 20, sector);
  at += record(data, at, std::string(1, 1), true, 20, sector);
  record(data, at, "PAYLOAD.BIN;1", false, 22, length);
  for (size_t i = 0; i < length; i++) data[22 * sector + i] = (i * 31 + 7) & 255;
  return data;
}
constexpr size_t entry = 20 * sector + 68;
void with_iso(const fs::path& path, const std::vector<u8>& data, const std::function<void(FILE*)>& run) {
  FILE* fp = file_util::open_file(path, "w+b");
  require(fp != nullptr, "open fixture");
  try {
    require(fwrite(data.data(), 1, data.size(), fp) == data.size(), "write fixture");
    fflush(fp);
    run(fp);
    fclose(fp);
  } catch (...) { fclose(fp); throw; }
}
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const fs::path root(argv[1]);
  fs::create_directories(root);
  int failed = 0, passed = 0;
  auto test = [&](const char* name, const std::function<void()>& body) {
    try { body(); printf("PASS %s\n", name); passed++; }
    catch (const std::exception& error) { printf("FAIL %s: %s\n", name, error.what()); failed++; }
  };
  auto reject = [&](std::vector<u8> data) {
    bool rejected = false;
    with_iso(root / "fixture.iso", data, [&](FILE* fp) {
      try { (void)find_files_in_iso(fp); }
      catch (const std::runtime_error&) { rejected = true; }
    });
    require(rejected, "malformed ISO accepted");
  };
  test("runtime without developer profiles", [&] {
    file_util::setup_project_path(root);
    require(REPL::find_repl_username() == "unknown", "missing optional profiles");
  });
  test("streamed extraction and hash across 1 MiB boundary", [&] {
    const auto data = fixture();
    with_iso(root / "fixture.iso", data, [&](FILE* fp) {
      auto iso = unpack_iso_files(fp, root / "extracted", false, true);
      require(iso.files_extracted == 1, "file count");
      auto bytes = file_util::read_binary_file(root / "extracted/PAYLOAD.BIN");
      require(bytes.size() == data.size() - 22 * sector, "output size");
      require(!memcmp(bytes.data(), data.data() + 22 * sector, bytes.size()), "output data");
      require(iso.hashes.at(0) == XXH64(bytes.data(), bytes.size(), 0), "streamed hash");
    });
  });
  test("zero length file", [&] {
    with_iso(root / "fixture.iso", fixture(0), [&](FILE* fp) {
      auto iso = unpack_iso_files(fp, root / "empty", false, true);
      require(iso.files_extracted == 1 && iso.hashes.at(0) == XXH64("", 0, 0), "empty file");
    });
  });
  test("invalid signature", [&] { auto data = fixture(); data[16 * sector + 1] = 'X'; reject(data); });
  test("truncated descriptor", [&] { reject(std::vector<u8>(20)); });
  test("truncated file extent", [&] { auto data = fixture(); data.resize(22 * sector); reject(data); });
  test("directory cycle", [&] { auto data = fixture(); record(data, entry, "LOOP", true, 20, sector); reject(data); });
  test("parent path traversal", [&] { auto data = fixture(); record(data, entry, "../BAD;1", false, 22, 1); reject(data); });
  test("absolute path", [&] { auto data = fixture(); record(data, entry, "/BAD;1", false, 22, 1); reject(data); });
  test("backslash path", [&] { auto data = fixture(); record(data, entry, "A\\B;1", false, 22, 1); reject(data); });
  test("oversized name", [&] { auto data = fixture(); data[entry + 32] = 250; reject(data); });
  test("short record", [&] { auto data = fixture(); data[entry] = 12; reject(data); });
  test("unsupported multi extent", [&] { auto data = fixture(); data[entry + 25] = 128; reject(data); });
  test("oversized directory", [&] { auto data = fixture(); put32(data, 16 * sector + 166, 0xffffffff); reject(data); });
  test("duplicate file", [&] {
    auto data = fixture();
    record(data, entry + data[entry], "PAYLOAD.BIN;1", false, 22, 1);
    reject(data);
  });
  printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
