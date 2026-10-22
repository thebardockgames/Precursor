#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <regex>
#include <stdexcept>
#include "decompiler/extractor/extractor_util.h"
#include "third-party/zstd/lib/common/xxhash.h"

// Read only the ISO directory and executable before copying/extracting gigabytes. (AI-assisted)
inline ISOMetadata probe_game_iso(FILE* fp, const std::string& expected) {
  if (expected != "jak1" && expected != "jak2" && expected != "jak3")
    throw std::runtime_error("Selecciona primero el juego que quieres instalar");
  const auto layout = find_files_in_iso(fp);
  const IsoFile::Entry* executable = nullptr;
  for (const auto& entry : layout.root.children) {
    if (!entry.is_dir && std::regex_match(entry.name, std::regex("[A-Z]{4}_[0-9]{3}\\.[0-9]{2}"))) {
      if (executable) throw std::runtime_error("La ISO contiene varios ejecutables de arranque");
      executable = &entry;
    }
  }
  if (!executable) throw std::runtime_error("La ISO no contiene un juego Jak reconocido");
  const auto& name = executable->name;
  const auto serial = name.substr(0, 4) + "-" + name.substr(5, 3) + name.substr(9, 2);
  const auto& database = extractor_iso_database();
  const auto editions = database.find(serial);
  if (editions == database.end()) throw std::runtime_error("Esta edición de la ISO no está soportada: " + serial);
  if (executable->size < 4 || executable->size > 64 * 1024 * 1024)
    throw std::runtime_error("El ejecutable de la ISO no es válido");
  if (fseeko(fp, executable->offset_in_file, SEEK_SET)) throw std::runtime_error("No se pudo leer la ISO");
  std::unique_ptr<XXH64_state_t, decltype(&XXH64_freeState)> hash(XXH64_createState(), &XXH64_freeState);
  if (!hash) throw std::bad_alloc();
  XXH64_reset(hash.get(), 0);
  std::array<unsigned char, 65536> bytes{};
  size_t remaining = executable->size;
  while (remaining) {
    const auto count = std::min(remaining, bytes.size());
    if (fread(bytes.data(), 1, count, fp) != count) throw std::runtime_error("La ISO está incompleta");
    XXH64_update(hash.get(), bytes.data(), count);
    remaining -= count;
  }
  const auto edition = editions->second.find(XXH64_digest(hash.get()));
  if (edition == editions->second.end()) throw std::runtime_error("El ejecutable está modificado o su edición no está soportada");
  const auto& info = edition->second;
  auto title = [](const std::string& game) {
    return game == "jak1" ? "Jak 1" : game == "jak2" ? "Jak II" : game == "jak3" ? "Jak 3" : "otro juego";
  };
  if (info.game_name != expected)
    throw std::runtime_error(std::string("Esta ISO es de ") + title(info.game_name) +
                             ", pero seleccionaste " + title(expected) +
                             ". Selecciona la tarjeta correcta o elige otra ISO.");
  return info;
}
