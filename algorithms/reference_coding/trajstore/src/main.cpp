#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <direct.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr double kEarthRadiusKm = 6371.0;
constexpr double kDistanceEps = 1e-8;
constexpr double kPi = 3.14159265358979323846;

struct Point {
  double lat = 0.0;
  double lon = 0.0;
  int time = 0;
};

struct Trajectory {
  std::int64_t id = -1;
  std::vector<Point> points;
};

struct MemberEncoding {
  std::int64_t center_id = -1;
  std::int64_t member_id = -1;
  std::vector<int> time_sequence;
  std::vector<Point> original_points;
};

struct CellCompressionResult {
  std::vector<Trajectory> centers;
  std::vector<MemberEncoding> members;
};

struct CompressionStats {
  std::size_t original_point_count = 0;
  std::size_t center_point_count = 0;
  std::size_t member_time_count = 0;
  std::size_t original_size_bytes = 0;
  double compress_time_s = 0.0;
};

enum class InputFormat {
  kTimeLatLon,
  kLatLonTime,
};

struct Config {
  std::string input_dir;
  std::string output_dir;
  double max_error_km = 0.03;
  int lat_divs = 20;
  int lon_divs = 20;
  InputFormat format = InputFormat::kTimeLatLon;
};

struct GridBounds {
  double min_lat = std::numeric_limits<double>::infinity();
  double max_lat = -std::numeric_limits<double>::infinity();
  double min_lon = std::numeric_limits<double>::infinity();
  double max_lon = -std::numeric_limits<double>::infinity();
};

struct CellTrajectoryKey {
  std::string grid_id;
  std::int64_t traj_id = -1;

  bool operator<(const CellTrajectoryKey& other) const {
    return std::tie(grid_id, traj_id) < std::tie(other.grid_id, other.traj_id);
  }
};

std::string trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::vector<std::string> split_numeric_fields(std::string line) {
  for (char& ch : line) {
    if (ch == ',') {
      ch = ' ';
    }
  }

  std::istringstream iss(line);
  std::vector<std::string> tokens;
  std::string token;
  while (iss >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

std::string join_path(const std::string& base, const std::string& leaf) {
  if (base.empty()) {
    return leaf;
  }
  const char last = base.back();
  if (last == '\\' || last == '/') {
    return base + leaf;
  }
  return base + "\\" + leaf;
}

void ensure_directory(const std::string& dir) {
  if (dir.empty()) {
    return;
  }

  std::string current;
  for (std::size_t i = 0; i < dir.size(); ++i) {
    current.push_back(dir[i]);
    const bool is_separator = dir[i] == '\\' || dir[i] == '/';
    const bool is_last = i + 1 == dir.size();
    if (!is_separator && !is_last) {
      continue;
    }
    if (!current.empty() && (current.back() == '\\' || current.back() == '/')) {
      current.pop_back();
    }
    if (!current.empty()) {
      _mkdir(current.c_str());
    }
  }
}

double haversine_km(double lat1, double lon1, double lat2, double lon2) {
  const double lat1_rad = lat1 * kPi / 180.0;
  const double lat2_rad = lat2 * kPi / 180.0;
  const double lon1_rad = lon1 * kPi / 180.0;
  const double lon2_rad = lon2 * kPi / 180.0;

  const double dlat = lat2_rad - lat1_rad;
  const double dlon = lon2_rad - lon1_rad;
  const double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
                   std::sin(dlon / 2.0) * std::sin(dlon / 2.0) *
                       std::cos(lat1_rad) * std::cos(lat2_rad);
  const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
  return kEarthRadiusKm * c;
}

double point_distance_km(const Point& a, const Point& b) {
  return haversine_km(a.lat, a.lon, b.lat, b.lon);
}

Point interpolate_spatial_point(const Point& start, const Point& end, double ratio) {
  Point result;
  result.lat = start.lat + (end.lat - start.lat) * ratio;
  result.lon = start.lon + (end.lon - start.lon) * ratio;
  result.time = start.time;
  return result;
}

Point interpolate_by_time(const Point& start, const Point& end, int target_time) {
  if (end.time <= start.time) {
    return start;
  }

  const double ratio =
      static_cast<double>(target_time - start.time) /
      static_cast<double>(end.time - start.time);
  return interpolate_spatial_point(
      start, end, std::max(0.0, std::min(1.0, ratio)));
}

int interpolate_time(const Point& start, const Point& end, double ratio) {
  const double time = static_cast<double>(start.time) +
                      (static_cast<double>(end.time - start.time) * ratio);
  return static_cast<int>(time);
}

Point aligned_point_at_distance(const std::vector<Point>& trajectory, double target_distance) {
  if (trajectory.empty()) {
    return {};
  }
  if (trajectory.size() == 1) {
    return trajectory.front();
  }

  double traversed = 0.0;
  for (std::size_t i = 1; i < trajectory.size(); ++i) {
    const double segment = point_distance_km(trajectory[i - 1], trajectory[i]);
    if (traversed + segment + kDistanceEps >= target_distance) {
      if (segment <= kDistanceEps) {
        return trajectory[i];
      }
      const double ratio = std::max(
          0.0, std::min(1.0, (target_distance - traversed) / segment));
      return interpolate_spatial_point(trajectory[i - 1], trajectory[i], ratio);
    }
    traversed += segment;
  }

  return trajectory.back();
}

int aligned_time_at_distance(const std::vector<Point>& trajectory, double target_distance) {
  if (trajectory.empty()) {
    return 0;
  }
  if (trajectory.size() == 1) {
    return trajectory.front().time;
  }

  double traversed = 0.0;
  for (std::size_t i = 1; i < trajectory.size(); ++i) {
    const double segment = point_distance_km(trajectory[i - 1], trajectory[i]);
    if (traversed + segment + kDistanceEps >= target_distance) {
      if (segment <= kDistanceEps) {
        return trajectory[i].time;
      }
      const double ratio = std::max(
          0.0, std::min(1.0, (target_distance - traversed) / segment));
      return interpolate_time(trajectory[i - 1], trajectory[i], ratio);
    }
    traversed += segment;
  }

  return trajectory.back().time;
}

double max_aligned_distance_km(const std::vector<Point>& reference,
                               const std::vector<Point>& candidate,
                               double max_error_km) {
  if (reference.empty() || candidate.empty()) {
    return std::numeric_limits<double>::infinity();
  }

  double ref_distance = 0.0;
  double max_distance = 0.0;
  for (std::size_t i = 0; i < reference.size(); ++i) {
    if (i > 0) {
      ref_distance += point_distance_km(reference[i - 1], reference[i]);
    }
    const Point aligned = aligned_point_at_distance(candidate, ref_distance);
    const double distance = haversine_km(reference[i].lat, reference[i].lon,
                                         aligned.lat, aligned.lon);
    max_distance = std::max(max_distance, distance);
    if (max_distance > max_error_km) {
      return max_distance;
    }
  }
  return max_distance;
}

std::vector<int> compute_time_sequence(const std::vector<Point>& center,
                                       const std::vector<Point>& member) {
  std::vector<int> sequence;
  sequence.reserve(center.size());

  double center_distance = 0.0;
  for (std::size_t i = 0; i < center.size(); ++i) {
    if (i > 0) {
      center_distance += point_distance_km(center[i - 1], center[i]);
    }
    sequence.push_back(aligned_time_at_distance(member, center_distance));
  }
  return sequence;
}

bool is_summary_segment(const std::vector<Point>& points, std::size_t start,
                        std::size_t end, double epsilon_km) {
  if (end <= start + 1) {
    return true;
  }

  const Point& first = points[start];
  const Point& last = points[end];
  if (last.time <= first.time) {
    return false;
  }

  for (std::size_t i = start + 1; i < end; ++i) {
    const Point interpolated = interpolate_by_time(first, last, points[i].time);
    if (point_distance_km(interpolated, points[i]) > epsilon_km) {
      return false;
    }
  }
  return true;
}

std::vector<Point> eliminate_extraneous_points(const std::vector<Point>& points,
                                               double epsilon_km) {
  if (points.size() <= 2) {
    return points;
  }

  std::vector<Point> simplified;
  simplified.reserve(points.size());

  std::size_t start = 0;
  simplified.push_back(points[start]);
  while (start + 1 < points.size()) {
    std::size_t best = start + 1;
    for (std::size_t end = start + 2; end < points.size(); ++end) {
      if (is_summary_segment(points, start, end, epsilon_km)) {
        best = end;
      } else {
        break;
      }
    }
    simplified.push_back(points[best]);
    start = best;
  }

  return simplified;
}

void postprocess_cluster_groups(CellCompressionResult& result,
                                double point_elimination_epsilon_km) {
  std::unordered_map<std::int64_t, std::vector<MemberEncoding*>> members_by_center;
  for (MemberEncoding& member : result.members) {
    members_by_center[member.center_id].push_back(&member);
  }

  for (Trajectory& center : result.centers) {
    center.points =
        eliminate_extraneous_points(center.points, point_elimination_epsilon_km);
    auto it = members_by_center.find(center.id);
    if (it == members_by_center.end()) {
      continue;
    }
    for (MemberEncoding* member : it->second) {
      member->time_sequence =
          compute_time_sequence(center.points, member->original_points);
    }
  }
}

CellCompressionResult compress_cell_trajectories(std::vector<Trajectory> trajectories,
                                                 double max_error_km) {
  CellCompressionResult result;
  std::set<std::int64_t> assigned_members;

  for (std::size_t i = 0; i < trajectories.size(); ++i) {
    const Trajectory& outer = trajectories[i];
    if (outer.points.size() == 1) {
      result.centers.push_back(outer);
      continue;
    }

    if (assigned_members.count(outer.id) > 0) {
      continue;
    }

    result.centers.push_back(outer);
    for (std::size_t j = i + 1; j < trajectories.size(); ++j) {
      const Trajectory& inner = trajectories[j];
      if (assigned_members.count(inner.id) > 0) {
        continue;
      }

      const double distance =
          max_aligned_distance_km(outer.points, inner.points, max_error_km);
      if (distance < max_error_km) {
        assigned_members.insert(inner.id);
        result.members.push_back(
            MemberEncoding{outer.id,
                           inner.id,
                           compute_time_sequence(outer.points, inner.points),
                           inner.points});
      }
    }
  }

  return result;
}

int grid_index(double value, double min_value, double max_value, int divisions) {
  if (divisions <= 1 || std::abs(max_value - min_value) <= kDistanceEps) {
    return 0;
  }
  double scaled = (value - min_value) * static_cast<double>(divisions) /
                  (max_value - min_value);
  int cell = static_cast<int>(std::floor(scaled));
  if (cell < 0) {
    cell = 0;
  }
  if (cell >= divisions) {
    cell = divisions - 1;
  }
  return cell;
}

Point parse_point_line(const std::string& line, InputFormat format) {
  const auto fields = split_numeric_fields(line);
  if (fields.size() < 3) {
    throw std::runtime_error("expected three numeric columns per line");
  }

  Point point;
  if (format == InputFormat::kTimeLatLon) {
    point.time = static_cast<int>(std::stod(fields[0]));
    point.lat = std::stod(fields[1]);
    point.lon = std::stod(fields[2]);
  } else {
    point.lat = std::stod(fields[0]);
    point.lon = std::stod(fields[1]);
    point.time = static_cast<int>(std::stod(fields[2]));
  }
  return point;
}

Trajectory read_trajectory_file(const std::string& path, std::int64_t id, InputFormat format) {
  std::ifstream input(path.c_str());
  if (!input) {
    throw std::runtime_error("failed to open " + path);
  }

  Trajectory trajectory;
  trajectory.id = id;

  std::string line;
  while (std::getline(input, line)) {
    const std::string cleaned = trim(line);
    if (cleaned.empty()) {
      continue;
    }
    trajectory.points.push_back(parse_point_line(cleaned, format));
  }

  std::sort(trajectory.points.begin(), trajectory.points.end(),
            [](const Point& a, const Point& b) { return a.time < b.time; });
  return trajectory;
}

std::vector<std::string> list_input_files(const std::string& input_dir) {
  DIR* dir = opendir(input_dir.c_str());
  if (dir == nullptr) {
    throw std::runtime_error("failed to open input directory: " + input_dir);
  }

  std::vector<std::string> files;
  while (dirent* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") {
      continue;
    }
    const std::string path = join_path(input_dir, name);
    files.push_back(path);
  }
  closedir(dir);
  std::sort(files.begin(), files.end());
  return files;
}

GridBounds compute_bounds(const std::vector<Trajectory>& trajectories) {
  GridBounds bounds;
  for (const Trajectory& trajectory : trajectories) {
    for (const Point& point : trajectory.points) {
      bounds.min_lat = std::min(bounds.min_lat, point.lat);
      bounds.max_lat = std::max(bounds.max_lat, point.lat);
      bounds.min_lon = std::min(bounds.min_lon, point.lon);
      bounds.max_lon = std::max(bounds.max_lon, point.lon);
    }
  }
  return bounds;
}

std::map<CellTrajectoryKey, std::vector<Point>> split_into_cell_trajectories(
    const std::vector<Trajectory>& trajectories, const GridBounds& bounds,
    int lat_divs, int lon_divs) {
  std::map<CellTrajectoryKey, std::vector<Point>> grouped;
  for (const Trajectory& trajectory : trajectories) {
    for (const Point& point : trajectory.points) {
      const int lat_idx = grid_index(point.lat, bounds.min_lat, bounds.max_lat, lat_divs);
      const int lon_idx = grid_index(point.lon, bounds.min_lon, bounds.max_lon, lon_divs);
      CellTrajectoryKey key{std::to_string(lat_idx) + "-" + std::to_string(lon_idx),
                            trajectory.id};
      grouped[key].push_back(point);
    }
  }

  for (auto& [key, points] : grouped) {
    std::sort(points.begin(), points.end(),
              [](const Point& a, const Point& b) { return a.time < b.time; });
  }
  return grouped;
}

std::map<std::string, std::vector<Trajectory>> group_by_grid(
    const std::map<CellTrajectoryKey, std::vector<Point>>& split) {
  std::map<std::string, std::vector<Trajectory>> grouped;
  for (const auto& [key, points] : split) {
    grouped[key.grid_id].push_back(Trajectory{key.traj_id, points});
  }
  return grouped;
}

std::string format_error_token(double value) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3) << value;
  std::string text = oss.str();
  while (!text.empty() && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.push_back('0');
  }
  return text;
}

void materialize_output_files(
    const std::string& centers_path, const std::string& members_path,
    const std::map<std::string, CellCompressionResult>& compressed_by_grid) {
  std::ofstream centers_out(centers_path.c_str());
  std::ofstream members_out(members_path.c_str());

  if (!centers_out || !members_out) {
    throw std::runtime_error("failed to create output files");
  }

  for (const auto& [grid_id, result] : compressed_by_grid) {
    for (const Trajectory& center : result.centers) {
      for (const Point& point : center.points) {
        centers_out << grid_id << "," << center.id << "," << point.lat << ","
                    << point.lon << "," << point.time << "\n";
      }
    }

    for (const MemberEncoding& member : result.members) {
      for (int time : member.time_sequence) {
        members_out << grid_id << "," << member.center_id << "," << member.member_id
                    << "," << time << "\n";
      }
    }
  }

  centers_out.flush();
  members_out.flush();
}

void write_outputs(const std::string& output_dir, double max_error_km,
                   const std::map<std::string, CellCompressionResult>& compressed_by_grid,
                   const CompressionStats& stats) {
  ensure_directory(output_dir);

  const std::string error_token = format_error_token(max_error_km);
  const std::string centers_path =
      join_path(output_dir, "centerList-maxError-" + error_token + ".txt");
  const std::string members_path =
      join_path(output_dir, "memberList-maxError-" + error_token + ".txt");
  const std::string ratio_path = join_path(output_dir, "compressionRatio.txt");

  materialize_output_files(centers_path, members_path, compressed_by_grid);

  std::ofstream ratio_out(ratio_path.c_str(), std::ios::trunc);
  if (!ratio_out) {
    throw std::runtime_error("failed to create ratio output file");
  }

  const std::size_t original_size = stats.original_size_bytes;
  const std::size_t logical_compressed_size =
      stats.center_point_count * sizeof(double) * 2 +
      (stats.center_point_count + stats.member_time_count) * sizeof(std::int32_t);
  const double compression_ratio =
      logical_compressed_size == 0 ? 0.0
                                   : static_cast<double>(original_size) /
                                         static_cast<double>(logical_compressed_size);
  const double input_points_per_second =
      stats.compress_time_s <= 0.0
          ? 0.0
          : static_cast<double>(stats.original_point_count) / stats.compress_time_s;

  ratio_out << "maxError,orig_doubles,orig_ints,orig_size_bytes,compressed_doubles,"
               "compressed_ints,compressed_size_bytes,compression_ratio,"
               "compress_time_s,input_points_per_second\n";

  ratio_out << error_token << "," << stats.original_point_count * 2 << ","
            << stats.original_point_count << "," << original_size << ","
            << stats.center_point_count * 2 << ","
            << (stats.center_point_count + stats.member_time_count) << ","
            << logical_compressed_size << "," << compression_ratio << ","
            << std::fixed << std::setprecision(6) << stats.compress_time_s << ","
            << input_points_per_second << "\n";
}

Config parse_args(int argc, char** argv) {
  Config config;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto consume_value = [&](const std::string& name) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("missing value for " + name);
      }
      return argv[++i];
    };

    if (arg == "--input-dir") {
      config.input_dir = consume_value(arg);
    } else if (arg == "--output-dir") {
      config.output_dir = consume_value(arg);
    } else if (arg == "--max-error") {
      config.max_error_km = std::stod(consume_value(arg));
    } else if (arg == "--lat-divs") {
      config.lat_divs = std::stoi(consume_value(arg));
    } else if (arg == "--lon-divs") {
      config.lon_divs = std::stoi(consume_value(arg));
    } else if (arg == "--format") {
      const std::string format = consume_value(arg);
      if (format == "time_lat_lon") {
        config.format = InputFormat::kTimeLatLon;
      } else if (format == "lat_lon_time") {
        config.format = InputFormat::kLatLonTime;
      } else {
        throw std::runtime_error("unsupported format: " + format);
      }
    } else if (arg == "--help" || arg == "-h") {
      std::cout
          << "Usage: trajstore --input-dir DIR --output-dir DIR [options]\n"
          << "Options:\n"
          << "  --max-error KM      Maximum aligned spatial error in kilometers\n"
          << "  --lat-divs N        Number of latitude grid divisions\n"
          << "  --lon-divs N        Number of longitude grid divisions\n"
          << "  --format NAME       time_lat_lon or lat_lon_time\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }

  if (config.input_dir.empty() || config.output_dir.empty()) {
    throw std::runtime_error("--input-dir and --output-dir are required");
  }
  return config;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto compress_start = std::chrono::steady_clock::now();
    const Config config = parse_args(argc, argv);
    const double cluster_error_km = config.max_error_km;
    const double point_elimination_error_km = config.max_error_km / 2.0;
    const std::vector<std::string> files = list_input_files(config.input_dir);
    if (files.empty()) {
      throw std::runtime_error("no input files found");
    }

    std::vector<Trajectory> trajectories;
    trajectories.reserve(files.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
      trajectories.push_back(
          read_trajectory_file(files[i], static_cast<std::int64_t>(i), config.format));
    }

    const GridBounds bounds = compute_bounds(trajectories);
    const auto split = split_into_cell_trajectories(trajectories, bounds, config.lat_divs,
                                                    config.lon_divs);
    const auto grouped = group_by_grid(split);

    std::map<std::string, CellCompressionResult> compressed_by_grid;
    CompressionStats stats;
    for (const Trajectory& trajectory : trajectories) {
      stats.original_point_count += trajectory.points.size();
      stats.original_size_bytes += trajectory.points.size() *
                                   (sizeof(std::int32_t) + sizeof(double) * 2);
    }

    for (const auto& [grid_id, cell_trajectories] : grouped) {
      CellCompressionResult result =
          compress_cell_trajectories(cell_trajectories, cluster_error_km);
      postprocess_cluster_groups(result, point_elimination_error_km);
      for (const Trajectory& center : result.centers) {
        stats.center_point_count += center.points.size();
      }
      for (const MemberEncoding& member : result.members) {
        stats.member_time_count += member.time_sequence.size();
      }
      compressed_by_grid.emplace(grid_id, std::move(result));
    }

    const auto compress_end = std::chrono::steady_clock::now();
    stats.compress_time_s =
        std::chrono::duration<double>(compress_end - compress_start).count();

    write_outputs(config.output_dir, config.max_error_km, compressed_by_grid, stats);

    std::cout << "Processed " << trajectories.size() << " trajectories across "
              << grouped.size() << " grid cells.\n";
    std::cout << "Original points: " << stats.original_point_count << "\n";
    std::cout << "Center points: " << stats.center_point_count << "\n";
    std::cout << "Member timestamps: " << stats.member_time_count << "\n";
    std::cout << "Compress time (s): " << stats.compress_time_s << "\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "trajstore error: " << ex.what() << "\n";
    return 1;
  }
}
