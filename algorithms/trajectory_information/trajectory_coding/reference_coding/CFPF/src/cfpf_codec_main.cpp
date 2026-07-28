#include "compact_cfpf_codec.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::uint64_t file_size(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw std::runtime_error("failed to inspect file: " + path);
  }
  return static_cast<std::uint64_t>(input.tellg());
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 4) {
      std::cout << "Usage:\n"
                << "  cfpf_codec encode-compact CFPF_OUTPUT_DIR ARCHIVE.cfpfc\n"
                << "  cfpf_codec decode-compact ARCHIVE.cfpfc OUTPUT_DIR\n";
      return argc == 1 ? 0 : 1;
    }
    const std::string command = argv[1];
    if (command == "encode-compact") {
      const CompactCfpfStats stats =
          encode_compact_cfpf_directory(argv[2], argv[3]);
      const std::uint64_t archive = file_size(argv[3]);
      std::cout << "Semantic bytes: " << stats.semantic_bytes << '\n'
                << "Huffman payload bytes: "
                << (stats.encoded_bits + 7) / 8 << '\n'
                << "Archive bytes: " << archive << '\n'
                << "Centers: " << stats.centers << '\n'
                << "Trajectories: " << stats.trajectories << '\n'
                << "Points: " << stats.points << '\n';
    } else if (command == "decode-compact") {
      const CompactCfpfStats stats =
          decode_compact_cfpf_archive(argv[2], argv[3]);
      std::cout << "Semantic bytes: " << stats.semantic_bytes << '\n'
                << "Centers: " << stats.centers << '\n'
                << "Trajectories: " << stats.trajectories << '\n'
                << "Points: " << stats.points << '\n'
                << "Decoded directory: " << argv[3] << '\n';
    } else {
      throw std::runtime_error("unknown codec command");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "cfpf_codec error: " << error.what() << '\n';
    return 1;
  }
}
