#include "compact_cfpf_codec.h"

#include "huffman_codec.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <direct.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::array<char, 8> kMagic = {'C', 'F', 'P', 'F', 'C', 'M', '0', '1'};

struct Center {
  double lon = 0.0;
  double lat = 0.0;
};

struct Point {
  std::int64_t time = 0;
  bool cluster_ref = false;
  std::uint64_t cluster_id = 0;
  double lon = 0.0;
  double lat = 0.0;
};

struct Trajectory {
  std::string id;
  std::vector<Point> points;
};

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

std::vector<std::string> split_csv(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t begin = 0;
  while (true) {
    const std::size_t comma = line.find(',', begin);
    if (comma == std::string::npos) {
      fields.push_back(line.substr(begin));
      break;
    }
    fields.push_back(line.substr(begin, comma - begin));
    begin = comma + 1;
  }
  return fields;
}

void write_u64(std::ostream& output, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    output.put(static_cast<char>((value >> (8 * i)) & 0xffU));
  }
}

std::uint64_t read_u64(std::istream& input) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    const int byte = input.get();
    if (byte == EOF) {
      throw std::runtime_error("truncated compact CFPF header");
    }
    value |= static_cast<std::uint64_t>(byte) << (8 * i);
  }
  return value;
}

void append_varint(std::vector<std::uint8_t>& output, std::uint64_t value) {
  while (value >= 0x80U) {
    output.push_back(static_cast<std::uint8_t>((value & 0x7fU) | 0x80U));
    value >>= 7U;
  }
  output.push_back(static_cast<std::uint8_t>(value));
}

std::uint64_t zigzag(std::int64_t value) {
  return value >= 0 ? static_cast<std::uint64_t>(value) * 2U
                    : static_cast<std::uint64_t>(-(value + 1)) * 2U + 1U;
}

std::int64_t unzigzag(std::uint64_t value) {
  if ((value & 1U) == 0) {
    if (value / 2U >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      throw std::runtime_error("compact CFPF integer overflow");
    }
    return static_cast<std::int64_t>(value / 2U);
  }
  return -static_cast<std::int64_t>(value / 2U) - 1;
}

std::int64_t timestamp_delta(std::int64_t current, std::int64_t previous) {
  if ((previous > 0 &&
       current < std::numeric_limits<std::int64_t>::min() + previous) ||
      (previous < 0 &&
       current > std::numeric_limits<std::int64_t>::max() + previous)) {
    throw std::runtime_error("compact CFPF timestamp delta overflow");
  }
  return current - previous;
}

void append_double(std::vector<std::uint8_t>& output, double value) {
  std::uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
  std::memcpy(&bits, &value, sizeof(bits));
  for (int i = 0; i < 8; ++i) {
    output.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xffU));
  }
}

class PayloadReader {
 public:
  explicit PayloadReader(const std::vector<std::uint8_t>& data) : data_(data) {}

  std::uint64_t varint() {
    std::uint64_t value = 0;
    for (int shift = 0; shift <= 63; shift += 7) {
      const std::uint8_t byte = one();
      if (shift == 63 && (byte & 0xfeU) != 0) {
        throw std::runtime_error("compact CFPF varint overflow");
      }
      value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
      if ((byte & 0x80U) == 0) {
        return value;
      }
    }
    throw std::runtime_error("invalid compact CFPF varint");
  }

  std::uint8_t one() {
    if (position_ >= data_.size()) {
      throw std::runtime_error("truncated compact CFPF payload");
    }
    return data_[position_++];
  }

  std::string string(std::size_t size) {
    if (size > data_.size() - position_) {
      throw std::runtime_error("truncated compact CFPF string");
    }
    std::string value(reinterpret_cast<const char*>(data_.data() + position_),
                      size);
    position_ += size;
    return value;
  }

  double number() {
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) {
      bits |= static_cast<std::uint64_t>(one()) << (8 * i);
    }
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  bool finished() const { return position_ == data_.size(); }

 private:
  const std::vector<std::uint8_t>& data_;
  std::size_t position_ = 0;
};

std::vector<Center> read_centers(const std::string& input_dir) {
  std::ifstream input(join_path(input_dir, "cluster_centers.csv"));
  if (!input) {
    throw std::runtime_error("cluster_centers.csv not found");
  }
  std::vector<Center> centers;
  std::string line;
  std::getline(input, line);
  while (std::getline(input, line)) {
    const std::vector<std::string> fields = split_csv(line);
    if (fields.size() != 3 || std::stoull(fields[0]) != centers.size()) {
      throw std::runtime_error("compact codec requires contiguous cluster IDs");
    }
    centers.push_back(Center{std::stod(fields[1]), std::stod(fields[2])});
  }
  return centers;
}

std::vector<Trajectory> read_trajectories(const std::string& input_dir,
                                          std::size_t center_count) {
  std::ifstream input(join_path(input_dir, "fused_points.csv"));
  if (!input) {
    throw std::runtime_error("fused_points.csv not found");
  }
  std::vector<Trajectory> trajectories;
  std::string line;
  std::getline(input, line);
  while (std::getline(input, line)) {
    const std::vector<std::string> fields = split_csv(line);
    if (fields.size() != 6) {
      throw std::runtime_error("invalid fused point row");
    }
    if (trajectories.empty() || trajectories.back().id != fields[0]) {
      trajectories.push_back(Trajectory{fields[0], {}});
    }
    Point point;
    point.time = std::stoll(fields[1]);
    if (fields[2] == "cluster_ref") {
      point.cluster_ref = true;
      point.cluster_id = std::stoull(fields[3]);
      if (point.cluster_id >= center_count) {
        throw std::runtime_error("fused point references an invalid cluster");
      }
    } else if (fields[2] == "literal") {
      point.lon = std::stod(fields[4]);
      point.lat = std::stod(fields[5]);
    } else {
      throw std::runtime_error("unknown fused point record type");
    }
    trajectories.back().points.push_back(point);
  }
  return trajectories;
}

std::vector<std::uint8_t> serialize(const std::vector<Center>& centers,
                                    const std::vector<Trajectory>& trajectories,
                                    CompactCfpfStats& stats) {
  std::vector<std::uint8_t> payload;
  append_varint(payload, centers.size());
  for (const Center& center : centers) {
    append_double(payload, center.lon);
    append_double(payload, center.lat);
  }
  append_varint(payload, trajectories.size());
  for (const Trajectory& trajectory : trajectories) {
    append_varint(payload, trajectory.id.size());
    payload.insert(payload.end(), trajectory.id.begin(), trajectory.id.end());
    append_varint(payload, trajectory.points.size());
    std::int64_t previous_time = 0;
    for (const Point& point : trajectory.points) {
      append_varint(payload, zigzag(timestamp_delta(point.time, previous_time)));
      previous_time = point.time;
      payload.push_back(point.cluster_ref ? 1U : 0U);
      if (point.cluster_ref) {
        append_varint(payload, point.cluster_id);
      } else {
        append_double(payload, point.lon);
        append_double(payload, point.lat);
      }
      ++stats.points;
    }
  }
  stats.centers = centers.size();
  stats.trajectories = trajectories.size();
  stats.semantic_bytes = payload.size();
  return payload;
}

void deserialize(const std::vector<std::uint8_t>& payload,
                 std::vector<Center>& centers,
                 std::vector<Trajectory>& trajectories,
                 CompactCfpfStats& stats) {
  PayloadReader reader(payload);
  const std::uint64_t center_count = reader.varint();
  centers.reserve(static_cast<std::size_t>(center_count));
  for (std::uint64_t i = 0; i < center_count; ++i) {
    centers.push_back(Center{reader.number(), reader.number()});
  }
  const std::uint64_t trajectory_count = reader.varint();
  trajectories.reserve(static_cast<std::size_t>(trajectory_count));
  for (std::uint64_t i = 0; i < trajectory_count; ++i) {
    const std::uint64_t id_size = reader.varint();
    Trajectory trajectory;
    trajectory.id = reader.string(static_cast<std::size_t>(id_size));
    const std::uint64_t point_count = reader.varint();
    trajectory.points.reserve(static_cast<std::size_t>(point_count));
    std::int64_t previous_time = 0;
    for (std::uint64_t j = 0; j < point_count; ++j) {
      const std::int64_t delta = unzigzag(reader.varint());
      if ((delta > 0 && previous_time >
                            std::numeric_limits<std::int64_t>::max() - delta) ||
          (delta < 0 && previous_time <
                            std::numeric_limits<std::int64_t>::min() - delta)) {
        throw std::runtime_error("compact CFPF timestamp overflow");
      }
      Point point;
      point.time = previous_time + delta;
      previous_time = point.time;
      const std::uint8_t type = reader.one();
      if (type == 1U) {
        point.cluster_ref = true;
        point.cluster_id = reader.varint();
        if (point.cluster_id >= center_count) {
          throw std::runtime_error("compact CFPF cluster reference is invalid");
        }
      } else if (type == 0U) {
        point.lon = reader.number();
        point.lat = reader.number();
      } else {
        throw std::runtime_error("invalid compact CFPF point type");
      }
      trajectory.points.push_back(point);
      ++stats.points;
    }
    trajectories.push_back(std::move(trajectory));
  }
  if (!reader.finished()) {
    throw std::runtime_error("compact CFPF payload has trailing bytes");
  }
  stats.centers = centers.size();
  stats.trajectories = trajectories.size();
  stats.semantic_bytes = payload.size();
}

#if 0  // Removed experimental multi-stream and byte-plane codec variants.
std::array<std::vector<std::uint8_t>, kFieldCount> serialize_fields(
    const std::vector<Center>& centers,
    const std::vector<Trajectory>& trajectories, CompactCfpfStats& stats) {
  std::array<std::vector<std::uint8_t>, kFieldCount> fields;
  auto& control = fields[0];
  auto& center_coordinates = fields[1];
  auto& timestamps = fields[2];
  auto& point_types = fields[3];
  auto& cluster_ids = fields[4];
  auto& literal_coordinates = fields[5];

  append_varint(control, centers.size());
  for (const Center& center : centers) {
    append_double(center_coordinates, center.lon);
    append_double(center_coordinates, center.lat);
  }
  append_varint(control, trajectories.size());
  for (const Trajectory& trajectory : trajectories) {
    append_varint(control, trajectory.id.size());
    control.insert(control.end(), trajectory.id.begin(), trajectory.id.end());
    append_varint(control, trajectory.points.size());
    std::int64_t previous_time = 0;
    for (const Point& point : trajectory.points) {
      append_varint(timestamps,
                    zigzag(timestamp_delta(point.time, previous_time)));
      previous_time = point.time;
      point_types.push_back(point.cluster_ref ? 1U : 0U);
      if (point.cluster_ref) {
        append_varint(cluster_ids, point.cluster_id);
      } else {
        append_double(literal_coordinates, point.lon);
        append_double(literal_coordinates, point.lat);
      }
      ++stats.points;
    }
  }
  stats.centers = centers.size();
  stats.trajectories = trajectories.size();
  for (std::size_t i = 0; i < fields.size(); ++i) {
    stats.field_bytes[i] = fields[i].size();
    stats.semantic_bytes += fields[i].size();
  }
  return fields;
}

void deserialize_fields(
    const std::array<std::vector<std::uint8_t>, kFieldCount>& fields,
    std::vector<Center>& centers, std::vector<Trajectory>& trajectories,
    CompactCfpfStats& stats) {
  PayloadReader control(fields[0]);
  PayloadReader center_coordinates(fields[1]);
  PayloadReader timestamps(fields[2]);
  PayloadReader point_types(fields[3]);
  PayloadReader cluster_ids(fields[4]);
  PayloadReader literal_coordinates(fields[5]);

  const std::uint64_t center_count = control.varint();
  centers.reserve(static_cast<std::size_t>(center_count));
  for (std::uint64_t i = 0; i < center_count; ++i) {
    centers.push_back(
        Center{center_coordinates.number(), center_coordinates.number()});
  }
  const std::uint64_t trajectory_count = control.varint();
  trajectories.reserve(static_cast<std::size_t>(trajectory_count));
  for (std::uint64_t i = 0; i < trajectory_count; ++i) {
    Trajectory trajectory;
    trajectory.id =
        control.string(static_cast<std::size_t>(control.varint()));
    const std::uint64_t point_count = control.varint();
    trajectory.points.reserve(static_cast<std::size_t>(point_count));
    std::int64_t previous_time = 0;
    for (std::uint64_t j = 0; j < point_count; ++j) {
      const std::int64_t delta = unzigzag(timestamps.varint());
      if ((delta > 0 && previous_time >
                            std::numeric_limits<std::int64_t>::max() - delta) ||
          (delta < 0 && previous_time <
                            std::numeric_limits<std::int64_t>::min() - delta)) {
        throw std::runtime_error("field CFPF timestamp overflow");
      }
      Point point;
      point.time = previous_time + delta;
      previous_time = point.time;
      const std::uint8_t type = point_types.one();
      if (type == 1U) {
        point.cluster_ref = true;
        point.cluster_id = cluster_ids.varint();
        if (point.cluster_id >= center_count) {
          throw std::runtime_error("field CFPF cluster reference is invalid");
        }
      } else if (type == 0U) {
        point.lon = literal_coordinates.number();
        point.lat = literal_coordinates.number();
      } else {
        throw std::runtime_error("invalid field CFPF point type");
      }
      trajectory.points.push_back(point);
      ++stats.points;
    }
    trajectories.push_back(std::move(trajectory));
  }
  const std::array<PayloadReader*, kFieldCount> readers = {
      &control,          &center_coordinates, &timestamps,
      &point_types,      &cluster_ids,        &literal_coordinates};
  for (std::size_t i = 0; i < readers.size(); ++i) {
    if (!readers[i]->finished()) {
      throw std::runtime_error("field CFPF stream has trailing bytes");
    }
    stats.field_bytes[i] = fields[i].size();
    stats.semantic_bytes += fields[i].size();
  }
  stats.centers = centers.size();
  stats.trajectories = trajectories.size();
}

void append_coordinate_planes(
    std::array<std::vector<std::uint8_t>, kPlaneFieldCount>& fields,
    double lon, double lat) {
  std::uint64_t lon_bits = 0;
  std::uint64_t lat_bits = 0;
  std::memcpy(&lon_bits, &lon, sizeof(lon_bits));
  std::memcpy(&lat_bits, &lat, sizeof(lat_bits));
  for (std::size_t byte = 0; byte < 8; ++byte) {
    fields[4 + byte].push_back(
        static_cast<std::uint8_t>((lon_bits >> (8 * byte)) & 0xffU));
    fields[12 + byte].push_back(
        static_cast<std::uint8_t>((lat_bits >> (8 * byte)) & 0xffU));
  }
}

std::array<std::vector<std::uint8_t>, kPlaneFieldCount> serialize_planes(
    const std::vector<Center>& centers,
    const std::vector<Trajectory>& trajectories, PlaneCfpfStats& stats) {
  std::array<std::vector<std::uint8_t>, kPlaneFieldCount> fields;
  auto& control = fields[0];
  auto& timestamps = fields[1];
  auto& point_types = fields[2];
  auto& cluster_ids = fields[3];
  append_varint(control, centers.size());
  for (const Center& center : centers) {
    append_coordinate_planes(fields, center.lon, center.lat);
  }
  append_varint(control, trajectories.size());
  for (const Trajectory& trajectory : trajectories) {
    append_varint(control, trajectory.id.size());
    control.insert(control.end(), trajectory.id.begin(), trajectory.id.end());
    append_varint(control, trajectory.points.size());
    std::int64_t previous_time = 0;
    for (const Point& point : trajectory.points) {
      append_varint(timestamps,
                    zigzag(timestamp_delta(point.time, previous_time)));
      previous_time = point.time;
      point_types.push_back(point.cluster_ref ? 1U : 0U);
      if (point.cluster_ref) {
        append_varint(cluster_ids, point.cluster_id);
      } else {
        append_coordinate_planes(fields, point.lon, point.lat);
      }
      ++stats.points;
    }
  }
  stats.centers = centers.size();
  stats.trajectories = trajectories.size();
  for (std::size_t i = 0; i < fields.size(); ++i) {
    stats.field_bytes[i] = fields[i].size();
    stats.semantic_bytes += fields[i].size();
  }
  return fields;
}

class PlaneCoordinateReader {
 public:
  explicit PlaneCoordinateReader(
      const std::array<std::vector<std::uint8_t>, kPlaneFieldCount>& fields)
      : fields_(fields) {
    const std::size_t expected = fields_[4].size();
    for (std::size_t field = 5; field < fields_.size(); ++field) {
      if (fields_[field].size() != expected) {
        throw std::runtime_error("coordinate plane lengths do not match");
      }
    }
  }

  std::pair<double, double> coordinate() {
    if (position_ >= fields_[4].size()) {
      throw std::runtime_error("truncated coordinate planes");
    }
    std::uint64_t lon_bits = 0;
    std::uint64_t lat_bits = 0;
    for (std::size_t byte = 0; byte < 8; ++byte) {
      lon_bits |= static_cast<std::uint64_t>(fields_[4 + byte][position_])
                  << (8 * byte);
      lat_bits |= static_cast<std::uint64_t>(fields_[12 + byte][position_])
                  << (8 * byte);
    }
    ++position_;
    double lon = 0.0;
    double lat = 0.0;
    std::memcpy(&lon, &lon_bits, sizeof(lon));
    std::memcpy(&lat, &lat_bits, sizeof(lat));
    return {lon, lat};
  }

  bool finished() const { return position_ == fields_[4].size(); }

 private:
  const std::array<std::vector<std::uint8_t>, kPlaneFieldCount>& fields_;
  std::size_t position_ = 0;
};

void deserialize_planes(
    const std::array<std::vector<std::uint8_t>, kPlaneFieldCount>& fields,
    std::vector<Center>& centers, std::vector<Trajectory>& trajectories,
    PlaneCfpfStats& stats) {
  PayloadReader control(fields[0]);
  PayloadReader timestamps(fields[1]);
  PayloadReader point_types(fields[2]);
  PayloadReader cluster_ids(fields[3]);
  PlaneCoordinateReader coordinates(fields);
  const std::uint64_t center_count = control.varint();
  centers.reserve(static_cast<std::size_t>(center_count));
  for (std::uint64_t i = 0; i < center_count; ++i) {
    const auto coordinate = coordinates.coordinate();
    centers.push_back(Center{coordinate.first, coordinate.second});
  }
  const std::uint64_t trajectory_count = control.varint();
  trajectories.reserve(static_cast<std::size_t>(trajectory_count));
  for (std::uint64_t i = 0; i < trajectory_count; ++i) {
    Trajectory trajectory;
    trajectory.id =
        control.string(static_cast<std::size_t>(control.varint()));
    const std::uint64_t point_count = control.varint();
    trajectory.points.reserve(static_cast<std::size_t>(point_count));
    std::int64_t previous_time = 0;
    for (std::uint64_t j = 0; j < point_count; ++j) {
      const std::int64_t delta = unzigzag(timestamps.varint());
      if ((delta > 0 && previous_time >
                            std::numeric_limits<std::int64_t>::max() - delta) ||
          (delta < 0 && previous_time <
                            std::numeric_limits<std::int64_t>::min() - delta)) {
        throw std::runtime_error("plane CFPF timestamp overflow");
      }
      Point point;
      point.time = previous_time + delta;
      previous_time = point.time;
      const std::uint8_t type = point_types.one();
      if (type == 1U) {
        point.cluster_ref = true;
        point.cluster_id = cluster_ids.varint();
        if (point.cluster_id >= center_count) {
          throw std::runtime_error("plane CFPF cluster reference is invalid");
        }
      } else if (type == 0U) {
        const auto coordinate = coordinates.coordinate();
        point.lon = coordinate.first;
        point.lat = coordinate.second;
      } else {
        throw std::runtime_error("invalid plane CFPF point type");
      }
      trajectory.points.push_back(point);
      ++stats.points;
    }
    trajectories.push_back(std::move(trajectory));
  }
  if (!control.finished() || !timestamps.finished() ||
      !point_types.finished() || !cluster_ids.finished() ||
      !coordinates.finished()) {
    throw std::runtime_error("plane CFPF stream has trailing bytes");
  }
  for (std::size_t i = 0; i < fields.size(); ++i) {
    stats.field_bytes[i] = fields[i].size();
    stats.semantic_bytes += fields[i].size();
  }
  stats.centers = centers.size();
  stats.trajectories = trajectories.size();
}

#endif

void write_recovery_csv(const std::string& output_dir,
                        const std::vector<Center>& centers,
                        const std::vector<Trajectory>& trajectories) {
  create_directory_tree(output_dir);
  std::ofstream center_output(join_path(output_dir, "cluster_centers.csv"));
  std::ofstream point_output(join_path(output_dir, "fused_points.csv"));
  if (!center_output || !point_output) {
    throw std::runtime_error("failed to create compact CFPF decoded files");
  }
  center_output << "cluster_id,center_lon,center_lat\n"
                << std::setprecision(17);
  for (std::size_t i = 0; i < centers.size(); ++i) {
    center_output << i << ',' << centers[i].lon << ',' << centers[i].lat
                  << '\n';
  }
  point_output << "trajectory_id,time,record_type,cluster_id,lon,lat\n"
               << std::setprecision(17);
  for (const Trajectory& trajectory : trajectories) {
    for (const Point& point : trajectory.points) {
      if (point.cluster_ref) {
        point_output << trajectory.id << ',' << point.time << ",cluster_ref,"
                     << point.cluster_id << ",,\n";
      } else {
        point_output << trajectory.id << ',' << point.time << ",literal,,"
                     << point.lon << ',' << point.lat << '\n';
      }
    }
  }
}

}  // namespace

CompactCfpfStats encode_compact_cfpf_directory(
    const std::string& input_dir, const std::string& output_file) {
  const std::vector<Center> centers = read_centers(input_dir);
  const std::vector<Trajectory> trajectories =
      read_trajectories(input_dir, centers.size());
  CompactCfpfStats stats;
  const std::vector<std::uint8_t> payload =
      serialize(centers, trajectories, stats);
  std::ostringstream encoded(std::ios::out | std::ios::binary);
  huffman_encode_bytes(payload, encoded, &stats.encoded_bits);
  const std::string encoded_bytes = encoded.str();

  std::ofstream output(output_file, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to create compact CFPF archive");
  }
  output.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
  write_u64(output, payload.size());
  write_u64(output, stats.encoded_bits);
  output.write(encoded_bytes.data(),
               static_cast<std::streamsize>(encoded_bytes.size()));
  if (!output) {
    throw std::runtime_error("failed while writing compact CFPF archive");
  }
  return stats;
}

CompactCfpfStats decode_compact_cfpf_archive(
    const std::string& input_file, const std::string& output_dir) {
  std::ifstream input(input_file, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open compact CFPF archive");
  }
  std::array<char, 8> magic{};
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  if (magic != kMagic) {
    throw std::runtime_error("not a compact CFPF archive");
  }
  const std::uint64_t payload_size = read_u64(input);
  const std::uint64_t encoded_bits = read_u64(input);
  const std::vector<std::uint8_t> payload =
      huffman_decode_bytes(input, payload_size, encoded_bits);
  CompactCfpfStats stats;
  stats.encoded_bits = encoded_bits;
  std::vector<Center> centers;
  std::vector<Trajectory> trajectories;
  deserialize(payload, centers, trajectories, stats);
  write_recovery_csv(output_dir, centers, trajectories);
  return stats;
}

#if 0  // Removed experimental multi-stream and byte-plane codec variants.
CompactCfpfStats encode_field_cfpf_directory(
    const std::string& input_dir, const std::string& output_file) {
  const std::vector<Center> centers = read_centers(input_dir);
  const std::vector<Trajectory> trajectories =
      read_trajectories(input_dir, centers.size());
  CompactCfpfStats stats;
  const auto fields = serialize_fields(centers, trajectories, stats);
  std::ofstream output(output_file, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to create field CFPF archive");
  }
  output.write(kFieldMagic.data(),
               static_cast<std::streamsize>(kFieldMagic.size()));
  for (std::size_t i = 0; i < fields.size(); ++i) {
    std::ostringstream encoded(std::ios::out | std::ios::binary);
    huffman_encode_bytes(fields[i], encoded, &stats.field_encoded_bits[i]);
    stats.encoded_bits += stats.field_encoded_bits[i];
    const std::string encoded_bytes = encoded.str();
    write_u64(output, fields[i].size());
    write_u64(output, stats.field_encoded_bits[i]);
    output.write(encoded_bytes.data(),
                 static_cast<std::streamsize>(encoded_bytes.size()));
  }
  if (!output) {
    throw std::runtime_error("failed while writing field CFPF archive");
  }
  return stats;
}

CompactCfpfStats decode_field_cfpf_archive(
    const std::string& input_file, const std::string& output_dir) {
  std::ifstream input(input_file, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open field CFPF archive");
  }
  std::array<char, 8> magic{};
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  if (magic != kFieldMagic) {
    throw std::runtime_error("not a field CFPF archive");
  }
  CompactCfpfStats stats;
  std::array<std::vector<std::uint8_t>, kFieldCount> fields;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    const std::uint64_t field_size = read_u64(input);
    const std::uint64_t encoded_bits = read_u64(input);
    fields[i] = huffman_decode_bytes(input, field_size, encoded_bits);
    stats.field_encoded_bits[i] = encoded_bits;
    stats.encoded_bits += encoded_bits;
  }
  if (input.peek() != EOF) {
    throw std::runtime_error("field CFPF archive has trailing bytes");
  }
  std::vector<Center> centers;
  std::vector<Trajectory> trajectories;
  deserialize_fields(fields, centers, trajectories, stats);
  write_recovery_csv(output_dir, centers, trajectories);
  return stats;
}

PlaneCfpfStats encode_plane_cfpf_directory(
    const std::string& input_dir, const std::string& output_file) {
  const std::vector<Center> centers = read_centers(input_dir);
  const std::vector<Trajectory> trajectories =
      read_trajectories(input_dir, centers.size());
  PlaneCfpfStats stats;
  const auto fields = serialize_planes(centers, trajectories, stats);
  std::ofstream output(output_file, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to create plane CFPF archive");
  }
  output.write(kPlaneMagic.data(),
               static_cast<std::streamsize>(kPlaneMagic.size()));
  for (std::size_t i = 0; i < fields.size(); ++i) {
    std::ostringstream encoded(std::ios::out | std::ios::binary);
    huffman_encode_bytes(fields[i], encoded, &stats.field_encoded_bits[i]);
    stats.encoded_bits += stats.field_encoded_bits[i];
    const std::string encoded_bytes = encoded.str();
    write_u64(output, fields[i].size());
    write_u64(output, stats.field_encoded_bits[i]);
    output.write(encoded_bytes.data(),
                 static_cast<std::streamsize>(encoded_bytes.size()));
  }
  if (!output) {
    throw std::runtime_error("failed while writing plane CFPF archive");
  }
  return stats;
}

PlaneCfpfStats decode_plane_cfpf_archive(
    const std::string& input_file, const std::string& output_dir) {
  std::ifstream input(input_file, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open plane CFPF archive");
  }
  std::array<char, 8> magic{};
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  if (magic != kPlaneMagic) {
    throw std::runtime_error("not a plane CFPF archive");
  }
  PlaneCfpfStats stats;
  std::array<std::vector<std::uint8_t>, kPlaneFieldCount> fields;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    const std::uint64_t field_size = read_u64(input);
    const std::uint64_t encoded_bits = read_u64(input);
    fields[i] = huffman_decode_bytes(input, field_size, encoded_bits);
    stats.field_encoded_bits[i] = encoded_bits;
    stats.encoded_bits += encoded_bits;
  }
  if (input.peek() != EOF) {
    throw std::runtime_error("plane CFPF archive has trailing bytes");
  }
  std::vector<Center> centers;
  std::vector<Trajectory> trajectories;
  deserialize_planes(fields, centers, trajectories, stats);
  write_recovery_csv(output_dir, centers, trajectories);
  return stats;
}
#endif
