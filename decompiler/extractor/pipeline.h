#pragma once

#include "decompiler/extractor/extractor_util.h"
#include "goalc/emitter/InstructionSet.h"

// Shared by the desktop CLI and Android's isolated installation service. (AI-assisted)
IsoFile extract_files(fs::path input_file_path, fs::path extracted_iso_path);
std::tuple<std::optional<ISOMetadata>, ExtractorErrorCode> validate(
    const std::string& game_name,
    const fs::path& extracted_iso_path,
    uint64_t expected_hash,
    int expected_num_files);
ExtractorErrorCode decompile(const fs::path& in_folder,
                            const std::string& data_subfolder,
                            const std::string& config_override);
ExtractorErrorCode compile(const fs::path& iso_data_path,
                          const std::string& data_subfolder,
                          emitter::InstructionSet instruction_set = emitter::InstructionSet::X86);
