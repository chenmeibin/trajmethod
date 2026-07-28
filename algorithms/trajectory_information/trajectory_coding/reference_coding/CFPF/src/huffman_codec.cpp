#include "huffman_codec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <direct.h>
#include <fstream>
#include <limits>
#include <memory>
#include <queue>
#include <stdexcept>
#include <utility>

namespace {

constexpr std::array<char, 8> kMagic = {'C', 'F', 'P', 'F', 'H', 'U', 'F', '1'};
constexpr std::array<const char*, 4> kCfpfFiles = {
    "trajectory_groups.csv", "cluster_centers.csv", "fused_points.csv",
    "cfpf_summary.csv"};

std::string join_path(const std::string& directory, const std::string& name) {
  if (directory.empty()) {
    return name;
  }
  const char last = directory.back();
  return last == '/' || last == '\\' ? directory + name
                                      : directory + "\\" + name;
}

void create_directory_tree(const std::string& path) {
  std::string current;
  for (std::size_t i = 0; i < path.size(); ++i) {
    current.push_back(path[i]);
    const bool boundary = path[i] == '/' || path[i] == '\\' ||
                          i + 1 == path.size();
    if (!boundary) {
      continue;
    }
    while (!current.empty() &&
           (current.back() == '/' || current.back() == '\\')) {
      current.pop_back();
    }
    if (!current.empty() && current.back() != ':') {
      _mkdir(current.c_str());
    }
    if (i + 1 < path.size()) {
      current.push_back('\\');
    }
  }
}

void write_u16(std::ostream& output, std::uint16_t value) {
  output.put(static_cast<char>(value & 0xffU));
  output.put(static_cast<char>((value >> 8U) & 0xffU));
}

void write_u64(std::ostream& output, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    output.put(static_cast<char>((value >> (i * 8)) & 0xffU));
  }
}

std::uint16_t read_u16(std::istream& input) {
  const int low = input.get();
  const int high = input.get();
  if (low == EOF || high == EOF) {
    throw std::runtime_error("truncated Huffman archive header");
  }
  return static_cast<std::uint16_t>(low | (high << 8));
}

std::uint64_t read_u64(std::istream& input) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    const int byte = input.get();
    if (byte == EOF) {
      throw std::runtime_error("truncated Huffman archive header");
    }
    value |= static_cast<std::uint64_t>(byte) << (i * 8);
  }
  return value;
}

std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open CFPF file: " + path);
  }
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0) {
    throw std::runtime_error("failed to determine file size: " + path);
  }
  input.seekg(0);
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
  if (!data.empty()) {
    input.read(reinterpret_cast<char*>(data.data()), size);
  }
  if (!input && !data.empty()) {
    throw std::runtime_error("failed to read CFPF file: " + path);
  }
  return data;
}

struct TreeNode {
  std::uint64_t frequency = 0;
  int symbol = -1;
  int minimum_symbol = 0;
  TreeNode* left = nullptr;
  TreeNode* right = nullptr;
};

std::array<std::uint8_t, 256> build_code_lengths(
    const std::vector<std::uint8_t>& data) {
  std::array<std::uint64_t, 256> frequencies{};
  for (std::uint8_t value : data) {
    ++frequencies[value];
  }
  std::vector<std::unique_ptr<TreeNode>> nodes;
  auto compare = [](const TreeNode* lhs, const TreeNode* rhs) {
    if (lhs->frequency != rhs->frequency) {
      return lhs->frequency > rhs->frequency;
    }
    return lhs->minimum_symbol > rhs->minimum_symbol;
  };
  std::priority_queue<TreeNode*, std::vector<TreeNode*>, decltype(compare)>
      queue(compare);
  for (int symbol = 0; symbol < 256; ++symbol) {
    if (frequencies[symbol] == 0) {
      continue;
    }
    nodes.push_back(std::make_unique<TreeNode>(TreeNode{
        frequencies[symbol], symbol, symbol, nullptr, nullptr}));
    queue.push(nodes.back().get());
  }
  std::array<std::uint8_t, 256> lengths{};
  if (queue.empty()) {
    return lengths;
  }
  if (queue.size() == 1) {
    lengths[queue.top()->symbol] = 1;
    return lengths;
  }
  while (queue.size() > 1) {
    TreeNode* left = queue.top();
    queue.pop();
    TreeNode* right = queue.top();
    queue.pop();
    nodes.push_back(std::make_unique<TreeNode>(TreeNode{
        left->frequency + right->frequency, -1,
        std::min(left->minimum_symbol, right->minimum_symbol), left, right}));
    queue.push(nodes.back().get());
  }
  const auto visit = [&](const auto& self, const TreeNode* node,
                         std::uint16_t depth) -> void {
    if (node->symbol >= 0) {
      if (depth > std::numeric_limits<std::uint8_t>::max()) {
        throw std::runtime_error("Huffman code exceeds supported length");
      }
      lengths[node->symbol] = static_cast<std::uint8_t>(depth);
      return;
    }
    self(self, node->left, depth + 1);
    self(self, node->right, depth + 1);
  };
  visit(visit, queue.top(), 0);
  return lengths;
}

void increment_binary(std::string& value) {
  for (std::size_t i = value.size(); i > 0; --i) {
    if (value[i - 1] == '0') {
      value[i - 1] = '1';
      return;
    }
    value[i - 1] = '0';
  }
  throw std::runtime_error("invalid canonical Huffman lengths");
}

std::array<std::string, 256> canonical_codes(
    const std::array<std::uint8_t, 256>& lengths) {
  std::vector<std::pair<int, int>> symbols;
  for (int symbol = 0; symbol < 256; ++symbol) {
    if (lengths[symbol] != 0) {
      symbols.emplace_back(lengths[symbol], symbol);
    }
  }
  std::sort(symbols.begin(), symbols.end());
  std::array<std::string, 256> codes;
  if (symbols.empty()) {
    return codes;
  }
  std::string code(static_cast<std::size_t>(symbols.front().first), '0');
  codes[symbols.front().second] = code;
  for (std::size_t i = 1; i < symbols.size(); ++i) {
    increment_binary(code);
    if (symbols[i].first < static_cast<int>(code.size())) {
      throw std::runtime_error("invalid canonical Huffman code order");
    }
    code.append(static_cast<std::size_t>(symbols[i].first) - code.size(), '0');
    codes[symbols[i].second] = code;
  }
  return codes;
}

class BitWriter {
 public:
  explicit BitWriter(std::ostream& output) : output_(output) {}
  void write(bool bit) {
    byte_ = static_cast<std::uint8_t>((byte_ << 1U) | (bit ? 1U : 0U));
    if (++used_ == 8) {
      output_.put(static_cast<char>(byte_));
      byte_ = 0;
      used_ = 0;
    }
  }
  void finish() {
    if (used_ != 0) {
      output_.put(static_cast<char>(byte_ << (8 - used_)));
      byte_ = 0;
      used_ = 0;
    }
  }

 private:
  std::ostream& output_;
  std::uint8_t byte_ = 0;
  int used_ = 0;
};

class BitReader {
 public:
  explicit BitReader(std::istream& input) : input_(input) {}
  bool read() {
    if (remaining_ == 0) {
      const int value = input_.get();
      if (value == EOF) {
        throw std::runtime_error("truncated Huffman payload");
      }
      byte_ = static_cast<std::uint8_t>(value);
      remaining_ = 8;
    }
    const bool bit = (byte_ & 0x80U) != 0;
    byte_ <<= 1U;
    --remaining_;
    return bit;
  }

 private:
  std::istream& input_;
  std::uint8_t byte_ = 0;
  int remaining_ = 0;
};

}  // namespace

void huffman_encode_bytes(const std::vector<std::uint8_t>& data,
                          std::ostream& output,
                          std::uint64_t* encoded_bits) {
  const auto lengths = build_code_lengths(data);
  const auto codes = canonical_codes(lengths);
  std::uint64_t bit_count = 0;
  for (std::uint8_t length : lengths) {
    output.put(static_cast<char>(length));
  }
  BitWriter writer(output);
  for (std::uint8_t value : data) {
    for (char bit : codes[value]) {
      writer.write(bit == '1');
      ++bit_count;
    }
  }
  writer.finish();
  if (encoded_bits != nullptr) {
    *encoded_bits = bit_count;
  }
}

std::vector<std::uint8_t> huffman_decode_bytes(std::istream& input,
                                                std::uint64_t output_size,
                                                std::uint64_t encoded_bits) {
  std::array<std::uint8_t, 256> lengths{};
  for (std::uint8_t& length : lengths) {
    const int value = input.get();
    if (value == EOF) {
      throw std::runtime_error("truncated Huffman codebook");
    }
    length = static_cast<std::uint8_t>(value);
  }
  const auto codes = canonical_codes(lengths);
  struct DecodeNode {
    int children[2] = {-1, -1};
    int symbol = -1;
  };
  std::vector<DecodeNode> tree(1);
  for (int symbol = 0; symbol < 256; ++symbol) {
    if (codes[symbol].empty()) {
      continue;
    }
    int node = 0;
    for (char bit : codes[symbol]) {
      const int branch = bit == '1' ? 1 : 0;
      if (tree[node].children[branch] < 0) {
        tree[node].children[branch] = static_cast<int>(tree.size());
        tree.push_back(DecodeNode{});
      }
      node = tree[node].children[branch];
    }
    tree[node].symbol = symbol;
  }
  std::vector<std::uint8_t> output;
  output.reserve(static_cast<std::size_t>(output_size));
  BitReader reader(input);
  std::uint64_t consumed = 0;
  int node = 0;
  while (output.size() < output_size) {
    if (consumed >= encoded_bits) {
      throw std::runtime_error("Huffman payload ended before expected output");
    }
    node = tree[node].children[reader.read() ? 1 : 0];
    ++consumed;
    if (node < 0) {
      throw std::runtime_error("invalid Huffman payload");
    }
    if (tree[node].symbol >= 0) {
      output.push_back(static_cast<std::uint8_t>(tree[node].symbol));
      node = 0;
    }
  }
  if (consumed != encoded_bits) {
    throw std::runtime_error("Huffman payload has unexpected trailing bits");
  }
  return output;
}

std::vector<HuffmanFileStats> encode_cfpf_directory(
    const std::string& input_dir, const std::string& output_file) {
  std::ofstream output(output_file, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to create Huffman archive: " + output_file);
  }
  output.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
  write_u16(output, static_cast<std::uint16_t>(kCfpfFiles.size()));
  std::vector<HuffmanFileStats> stats;
  for (const char* file_name : kCfpfFiles) {
    const std::vector<std::uint8_t> data =
        read_file(join_path(input_dir, file_name));
    const std::string name(file_name);
    write_u16(output, static_cast<std::uint16_t>(name.size()));
    output.write(name.data(), static_cast<std::streamsize>(name.size()));
    write_u64(output, data.size());
    const auto lengths = build_code_lengths(data);
    std::uint64_t bit_count = 0;
    for (std::uint8_t value : data) {
      bit_count += lengths[value];
    }
    write_u64(output, bit_count);
    std::uint64_t written_bits = 0;
    huffman_encode_bytes(data, output, &written_bits);
    if (written_bits != bit_count) {
      throw std::runtime_error("internal Huffman bit-count mismatch");
    }
    stats.push_back(HuffmanFileStats{name, data.size(), bit_count});
  }
  if (!output) {
    throw std::runtime_error("failed while writing Huffman archive");
  }
  return stats;
}

std::vector<HuffmanFileStats> decode_cfpf_archive(
    const std::string& input_file, const std::string& output_dir) {
  std::ifstream input(input_file, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open Huffman archive: " + input_file);
  }
  std::array<char, 8> magic{};
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  if (magic != kMagic) {
    throw std::runtime_error("not a CFPF Huffman archive");
  }
  const std::uint16_t file_count = read_u16(input);
  create_directory_tree(output_dir);
  std::vector<HuffmanFileStats> stats;
  for (std::uint16_t i = 0; i < file_count; ++i) {
    const std::uint16_t name_size = read_u16(input);
    std::string name(name_size, '\0');
    input.read(&name[0], name_size);
    if (!input || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
      throw std::runtime_error("invalid file name in Huffman archive");
    }
    const std::uint64_t original_size = read_u64(input);
    const std::uint64_t encoded_bits = read_u64(input);
    const std::vector<std::uint8_t> data =
        huffman_decode_bytes(input, original_size, encoded_bits);
    std::ofstream output(join_path(output_dir, name), std::ios::binary);
    if (!output) {
      throw std::runtime_error("failed to create decoded file: " + name);
    }
    if (!data.empty()) {
      output.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
    }
    stats.push_back(HuffmanFileStats{name, original_size, encoded_bits});
  }
  return stats;
}
