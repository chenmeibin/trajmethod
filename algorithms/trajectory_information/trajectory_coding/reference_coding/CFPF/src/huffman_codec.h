#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

struct HuffmanFileStats {
  std::string name;
  std::uint64_t original_bytes = 0;
  std::uint64_t encoded_bits = 0;
};

void huffman_encode_bytes(const std::vector<std::uint8_t>& data,
                          std::ostream& output,
                          std::uint64_t* encoded_bits = nullptr);

std::vector<std::uint8_t> huffman_decode_bytes(std::istream& input,
                                                std::uint64_t output_size,
                                                std::uint64_t encoded_bits);

std::vector<HuffmanFileStats> encode_cfpf_directory(
    const std::string& input_dir, const std::string& output_file);

std::vector<HuffmanFileStats> decode_cfpf_archive(
    const std::string& input_file, const std::string& output_dir);
