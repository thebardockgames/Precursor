#include "read_iso_file.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>

#include "common/common_types.h"
#include "common/log/log.h"
#include "common/util/FileUtil.h"
#include "third-party/zstd/lib/common/xxhash.h"

IsoFile::IsoFile() {
  root.is_dir = true;
}

std::string IsoFile::print() const {
  std::string result;
  root.print(&result, "");
  return result;
}

void IsoFile::Entry::print(std::string* result, const std::string& prefix) const {
  if (is_dir) {
    for (const auto& child : children) child.print(result, prefix + "/" + name);
  } else {
    result->append(prefix + "/" + name + "\n");
  }
}

namespace {
constexpr u64 SECTOR_SIZE = 0x800;

// ISOs now come directly from Android's document picker. Bound every directory/extent,
// reject paths/cycles, and stream large files rather than allocating their entire size. (AI-assisted)
void seek(FILE* fp, u64 offset) {
#ifdef _WIN32
  const auto result = _fseeki64(fp, offset, SEEK_SET);
#else
  const auto result = fseeko(fp, offset, SEEK_SET);
#endif
  if (result) throw std::runtime_error("Failed to seek ISO");
}

u64 file_size(FILE* fp) {
#ifdef _WIN32
  if (_fseeki64(fp, 0, SEEK_END)) throw std::runtime_error("Failed to seek ISO");
  const auto size = _ftelli64(fp);
#else
  if (fseeko(fp, 0, SEEK_END)) throw std::runtime_error("Failed to seek ISO");
  const auto size = ftello(fp);
#endif
  if (size < 0) throw std::runtime_error("Failed to measure ISO");
  return size;
}

u32 read_u32(const u8* bytes) {
  return u32(bytes[0]) | (u32(bytes[1]) << 8) | (u32(bytes[2]) << 16) | (u32(bytes[3]) << 24);
}

void check_extent(u64 offset, u64 size, u64 total) {
  if (offset > total || size > total - offset) throw std::runtime_error("Truncated ISO extent");
}

void read_at(FILE* fp, u64 offset, void* output, size_t size, u64 total) {
  check_extent(offset, size, total);
  seek(fp, offset);
  if (size && fread(output, 1, size, fp) != size) throw std::runtime_error("Failed to read ISO");
}

void check_name(const std::string& name) {
  if (name.empty() || name == "." || name == ".." ||
      name.find_first_of("/\\:") != std::string::npos ||
      std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
    throw std::runtime_error("Invalid filename in ISO");
  }
}

void add_from_dir(FILE* fp,
                  u32 sector,
                  u32 size,
                  IsoFile::Entry* parent,
                  u64 total,
                  std::set<u32>& directories,
                  size_t& entries,
                  int depth) {
  if (depth > 32 || size > 16 * 1024 * 1024 || size < 34 || !directories.insert(sector).second)
    throw std::runtime_error("Invalid or recursive ISO directory");
  std::vector<u8> data(size);
  read_at(fp, u64(sector) * SECTOR_SIZE, data.data(), data.size(), total);
  std::set<std::string> names;
  size_t offset = 0;
  while (offset < data.size()) {
    const u8 record_size = data[offset];
    if (!record_size) {
      offset = (offset / SECTOR_SIZE + 1) * SECTOR_SIZE;
      continue;
    }
    if (record_size < 34 || record_size > data.size() - offset ||
        (offset % SECTOR_SIZE) + record_size > SECTOR_SIZE)
      throw std::runtime_error("Invalid ISO directory record");
    const auto* record = data.data() + offset;
    const u8 name_len = record[32];
    if (!name_len || 33u + name_len > record_size)
      throw std::runtime_error("Invalid ISO filename length");
    if (!(name_len == 1 && record[33] <= 1)) {
      if (++entries > 100000) throw std::runtime_error("Too many ISO entries");
      if (record[25] & 0x80) throw std::runtime_error("Multi-extent ISO files are unsupported");
      IsoFile::Entry entry;
      entry.is_dir = (record[25] & 2) != 0;
      entry.name.assign(reinterpret_cast<const char*>(record + 33), name_len);
      if (!entry.is_dir && entry.name.size() >= 2 &&
          entry.name.substr(entry.name.size() - 2) == ";1") entry.name.resize(entry.name.size() - 2);
      if (entry.name == "WATER_AN.CGO") entry.name = "WATER-AN.CGO";
      check_name(entry.name);
      if (!names.insert(entry.name).second) throw std::runtime_error("Duplicate filename in ISO");
      const u32 extent = read_u32(record + 2);
      const u32 length = read_u32(record + 10);
      check_extent(u64(extent) * SECTOR_SIZE, length, total);
      if (entry.is_dir) {
        add_from_dir(fp, extent, length, &entry, total, directories, entries, depth + 1);
      } else {
        entry.offset_in_file = u64(extent) * SECTOR_SIZE;
        entry.size = length;
      }
      parent->children.push_back(std::move(entry));
    }
    offset += record_size;
  }
}

using File = std::unique_ptr<FILE, decltype(&fclose)>;

void unpack_entry(FILE* fp,
                  IsoFile& iso,
                  const IsoFile::Entry& entry,
                  const fs::path& dest,
                  bool print_progress) {
  // Revalidate even when callers provide a layout directly.
  if (!entry.name.empty()) check_name(entry.name);
  else if (!entry.is_dir) throw std::runtime_error("Empty ISO filename");
  const auto path = dest / entry.name;
  if (entry.is_dir) {
    fs::create_directories(path);
    for (const auto& child : entry.children) unpack_entry(fp, iso, child, path, print_progress);
    return;
  }
  if (print_progress) lg::info("Extracting {}, size 0x{:x}...", entry.name, entry.size);
  File output(file_util::open_file(path, "wb"), &fclose);
  if (!output) throw std::runtime_error("Failed to create extracted ISO file");
  std::unique_ptr<XXH64_state_t, decltype(&XXH64_freeState)> hash(XXH64_createState(), &XXH64_freeState);
  if (!hash) throw std::bad_alloc();
  XXH64_reset(hash.get(), 0);
  std::vector<u8> buffer(1024 * 1024);
  seek(fp, entry.offset_in_file);
  size_t remaining = entry.size;
  while (remaining) {
    const auto count = std::min(remaining, buffer.size());
    if (fread(buffer.data(), 1, count, fp) != count) throw std::runtime_error("Truncated ISO file");
    if (fwrite(buffer.data(), 1, count, output.get()) != count)
      throw std::runtime_error("Failed to write ISO file (check free space)");
    if (iso.shouldHash) XXH64_update(hash.get(), buffer.data(), count);
    remaining -= count;
  }
  if (fflush(output.get())) throw std::runtime_error("Failed to flush extracted ISO file");
  iso.files_extracted++;
  if (iso.shouldHash) iso.hashes.push_back(XXH64_digest(hash.get()));
}
}  // namespace

IsoFile find_files_in_iso(FILE* fp) {
  const auto total = file_size(fp);
  std::array<u8, SECTOR_SIZE> descriptor{};
  read_at(fp, 16 * SECTOR_SIZE, descriptor.data(), descriptor.size(), total);
  if (descriptor[0] != 1 || memcmp(descriptor.data() + 1, "CD001", 5) || descriptor[6] != 1)
    throw std::runtime_error("Not an ISO 9660 primary volume");
  if (descriptor[128] != 0 || descriptor[129] != 8)
    throw std::runtime_error("Unsupported ISO sector size");
  const auto* root = descriptor.data() + 156;
  if (root[0] < 34 || !(root[25] & 2)) throw std::runtime_error("Invalid ISO root directory");
  IsoFile result;
  std::set<u32> directories;
  size_t entries = 0;
  add_from_dir(fp, read_u32(root + 2), read_u32(root + 10), &result.root, total, directories, entries, 0);
  return result;
}

void unpack_iso_files(FILE* fp, IsoFile& layout, const fs::path& dest, bool print_progress) {
  unpack_entry(fp, layout, layout.root, dest, print_progress);
}

IsoFile unpack_iso_files(FILE* fp, const fs::path& dest, bool print_progress, const bool hashFiles) {
  auto file = find_files_in_iso(fp);
  file.shouldHash = hashFiles;
  unpack_iso_files(fp, file, dest, print_progress);
  return file;
}
