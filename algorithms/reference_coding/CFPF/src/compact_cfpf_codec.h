#pragma once

#include <cstdint>
#include <string>

struct CompactCfpfStats {
  std::uint64_t centers = 0;
  std::uint64_t trajectories = 0;
  std::uint64_t points = 0;
  std::uint64_t semantic_bytes = 0;
  std::uint64_t encoded_bits = 0;
};

CompactCfpfStats encode_compact_cfpf_directory(
    const std::string& input_dir, const std::string& output_file);

CompactCfpfStats decode_compact_cfpf_archive(
    const std::string& input_file, const std::string& output_dir);
