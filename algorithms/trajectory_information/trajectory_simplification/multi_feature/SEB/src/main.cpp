#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#define _mkdir(path) mkdir(path, 0777)
#endif
#include <dirent.h>

#include "preprocessing.h"
#include "seb_algorithm.h"

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kEarthRadiusMeters = 6378137.0;
constexpr double kEps = 1e-12;

struct Config {
  std::string input_dir;
  std::string output_dir;
  std::string export_preprocessed_dir;
  std::string projection_metadata_dir;
  double budget = 0.2;
  std::int64_t max_time_gap_seconds = 5 * 60;
  std::int64_t max_interpolation_gap_seconds = 0;
  double max_spike_speed_mps = 50.0;
  double max_average_sampling_interval_seconds = 0.0;
  std::string scoring_distance = "degrees";
  std::string dtw_mode = "both";
  std::size_t min_trajectory_points = 50;
  std::size_t max_trajectory_points = 0;
  bool exclude_short_trajectories = true;
  bool preprocess_only = false;
  bool input_is_preprocessed = false;
  bool evaluate = false;
  bool detailed_evaluation = false;
  ScoreWeights weights;
};

struct ReadStats {
  std::size_t raw_points = 0;
  std::size_t invalid_points = 0;
  std::size_t duplicate_times = 0;
  std::size_t valid_points = 0;
};

struct ParsedTrajectory {
  std::string trajectory_id;
  std::string object_id;
  std::vector<RawPoint> points;
  ReadStats stats;
};

struct TrajectorySummary {
  std::string trajectory_id;
  std::string object_id;
  ReadStats stats;
  std::size_t kept_points = 0;
  std::string status;
};

std::vector<std::vector<RawPoint>> apply_max_trajectory_points(
    const std::vector<std::vector<RawPoint>>& parts,
    std::size_t max_trajectory_points) {
  std::vector<std::vector<RawPoint>> result;
  for (const std::vector<RawPoint>& part : parts) {
    std::vector<std::vector<RawPoint>> chunks =
        split_by_max_points(part, max_trajectory_points);
    result.insert(result.end(),
                  std::make_move_iterator(chunks.begin()),
                  std::make_move_iterator(chunks.end()));
  }
  return result;
}

struct ErrorMetrics {
  double average_ped = 0.0;
  double maximum_ped = 0.0;
  double average_sed = 0.0;
  double maximum_sed = 0.0;
  double average_sad = 0.0;
  double maximum_sad = 0.0;
  double average_dad = 0.0;
  double maximum_dad = 0.0;
  double average_speed_error = 0.0;
  double maximum_speed_error = 0.0;
  double average_direction_error = 0.0;
  double maximum_direction_error = 0.0;
  double retained_dtw = 0.0;
  double reconstructed_dtw = 0.0;
  double retained_dtw_per_path_step = 0.0;
  double reconstructed_dtw_per_path_step = 0.0;
  std::size_t retained_dtw_path_length = 0;
  std::size_t reconstructed_dtw_path_length = 0;
  double selected_score_sum = 0.0;
  double maximum_selected_score = 0.0;
  std::size_t scored_feature_count = 0;
  std::size_t point_count = 0;
};

struct Projection {
  std::string name = "local-equirectangular";
  double ref_lon = 0.0;
  double ref_lat = 0.0;
  double ref_lon_rad = 0.0;
  double ref_lat_rad = 0.0;
};

std::string trim(const std::string& value) {
  const std::size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const std::size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::vector<std::string> split_csv(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t begin = 0;
  while (true) {
    const std::size_t comma = line.find(',', begin);
    fields.push_back(trim(line.substr(begin, comma - begin)));
    if (comma == std::string::npos) {
      return fields;
    }
    begin = comma + 1;
  }
}

double parse_finite_double(const std::string& text) {
  std::size_t consumed = 0;
  const double value = std::stod(text, &consumed);
  if (consumed != text.size() || !std::isfinite(value)) {
    throw std::invalid_argument("not a finite number");
  }
  return value;
}

// Converts a civil date to days since 1970-01-01.
std::int64_t days_from_civil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
  const unsigned adjusted_month =
      month > 2 ? month - 3 : month + 9;
  const unsigned day_of_year =
      (153 * adjusted_month + 2) / 5 + day - 1;
  const unsigned day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return static_cast<std::int64_t>(era) * 146097 +
         static_cast<std::int64_t>(day_of_era) - 719468;
}

bool is_leap_year(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

std::int64_t parse_china_local_unix_seconds(const std::string& text) {
  std::size_t integer_consumed = 0;
  try {
    const std::int64_t unix_seconds = std::stoll(text, &integer_consumed);
    if (integer_consumed == text.size()) {
      return unix_seconds;
    }
  } catch (const std::exception&) {
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  double second_value = 0.0;
  char trailing = '\0';
  const int matched = std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%lf%c", &year,
                                  &month, &day, &hour, &minute, &second_value,
                                  &trailing);
  if (matched != 6 || month < 1 || month > 12 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second_value < 0.0 || second_value >= 60.0) {
    throw std::invalid_argument("invalid local date-time");
  }
  static const int days_per_month[] = {31, 28, 31, 30, 31, 30,
                                       31, 31, 30, 31, 30, 31};
  int max_day = days_per_month[month - 1];
  if (month == 2 && is_leap_year(year)) {
    max_day = 29;
  }
  if (day < 1 || day > max_day) {
    throw std::invalid_argument("invalid calendar day");
  }

  const std::int64_t seconds = static_cast<std::int64_t>(std::floor(second_value));
  constexpr std::int64_t kChinaUtcOffsetSeconds = 8 * 60 * 60;
  return days_from_civil(year, static_cast<unsigned>(month),
                         static_cast<unsigned>(day)) *
             86400 +
         hour * 3600 + minute * 60 + seconds - kChinaUtcOffsetSeconds;
}

std::string join_path(const std::string& directory, const std::string& name) {
  if (directory.empty() || directory.back() == '/' || directory.back() == '\\') {
    return directory + name;
  }
  return directory + "/" + name;
}

std::string file_stem(const std::string& path) {
  const std::size_t slash = path.find_last_of("/\\");
  const std::size_t begin = slash == std::string::npos ? 0 : slash + 1;
  const std::size_t dot = path.find_last_of('.');
  const std::size_t end = dot == std::string::npos || dot < begin ? path.size() : dot;
  return path.substr(begin, end - begin);
}

void create_directory_tree(const std::string& path) {
  std::string current;
  for (std::size_t i = 0; i < path.size(); ++i) {
    current.push_back(path[i]);
    const bool boundary = path[i] == '/' || path[i] == '\\' || i + 1 == path.size();
    if (!boundary) {
      continue;
    }
    while (!current.empty() && (current.back() == '/' || current.back() == '\\')) {
      current.pop_back();
    }
    if (!current.empty() && current.back() != ':') {
      _mkdir(current.c_str());
    }
    if (i + 1 < path.size()) {
      current.push_back('/');
    }
  }
}

ParsedTrajectory read_geolife_trajectory(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open input file: " + path);
  }
  ParsedTrajectory trajectory;
  trajectory.trajectory_id = file_stem(path);
  const std::size_t separator = trajectory.trajectory_id.find('_');
  trajectory.object_id = trajectory.trajectory_id.substr(0, separator);

  std::string line;
  for (int i = 0; i < 6 && std::getline(input, line); ++i) {
  }
  std::map<std::int64_t, RawPoint> by_time;
  std::size_t original_index = 0;
  while (std::getline(input, line)) {
    const std::string cleaned = trim(line);
    if (cleaned.empty()) {
      continue;
    }
    ++trajectory.stats.raw_points;
    try {
      const std::vector<std::string> fields = split_csv(cleaned);
      if (fields.size() < 7) {
        throw std::invalid_argument("invalid GeoLife row");
      }
      RawPoint point;
      point.lat = parse_finite_double(fields[0]);
      point.lon = parse_finite_double(fields[1]);
      point.time = parse_china_local_unix_seconds(fields[5] + " " + fields[6]);
      point.original_index = original_index;
      if (point.lon < -180.0 || point.lon > 180.0 || point.lat < -90.0 ||
          point.lat > 90.0 ||
          (std::fabs(point.lon) <= kEps && std::fabs(point.lat) <= kEps)) {
        throw std::invalid_argument("invalid GeoLife coordinate");
      }
      if (by_time.find(point.time) != by_time.end()) {
        ++trajectory.stats.duplicate_times;
      }
      by_time[point.time] = point;
    } catch (const std::exception&) {
      ++trajectory.stats.invalid_points;
    }
    ++original_index;
  }
  trajectory.points.reserve(by_time.size());
  for (const auto& entry : by_time) {
    trajectory.points.push_back(entry.second);
  }
  trajectory.stats.valid_points = trajectory.points.size();
  return trajectory;
}

ParsedTrajectory read_trajectory(const std::string& path) {
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".plt") {
    return read_geolife_trajectory(path);
  }
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open input file: " + path);
  }

  ParsedTrajectory trajectory;
  trajectory.trajectory_id = file_stem(path);

  std::string line;
  if (!std::getline(input, line)) {
    return trajectory;
  }
  std::map<std::int64_t, RawPoint> by_time;
  std::size_t original_index = 0;

  auto process_data_line = [&](const std::string& data_line, bool tdrive_format) {
    const std::string cleaned = trim(data_line);
    if (cleaned.empty()) {
      return;
    }
    ++trajectory.stats.raw_points;
    try {
      const std::vector<std::string> fields = split_csv(cleaned);
      const std::size_t offset = tdrive_format ? 1 : 0;
      if (fields.size() != offset + 3) {
        throw std::invalid_argument("unexpected trajectory columns");
      }
      if (tdrive_format && trajectory.object_id.empty()) {
        trajectory.object_id = fields[0];
      }
      RawPoint point;
      point.time = parse_china_local_unix_seconds(fields[offset]);
      point.lon = parse_finite_double(fields[offset + 1]);
      point.lat = parse_finite_double(fields[offset + 2]);
      point.original_index = original_index;
      if (point.lon < -180.0 || point.lon > 180.0 || point.lat < -90.0 ||
          point.lat > 90.0) {
        throw std::invalid_argument("coordinate outside geographic range");
      }
      if (std::fabs(point.lon) <= kEps && std::fabs(point.lat) <= kEps) {
        throw std::invalid_argument("missing coordinate represented by zero pair");
      }
      if (tdrive_format &&
          (point.lon < 115.0 || point.lon > 118.0 || point.lat < 39.0 ||
           point.lat > 42.0)) {
        throw std::invalid_argument("T-Drive coordinate outside Beijing region");
      }

      const auto found = by_time.find(point.time);
      if (found != by_time.end()) {
        ++trajectory.stats.duplicate_times;
      }
      by_time[point.time] = point;  // Keep the last point for a duplicate timestamp.
    } catch (const std::exception&) {
      ++trajectory.stats.invalid_points;
    }
    ++original_index;
  };

  const std::string first_line = trim(line);
  const bool tdrive_format = split_csv(first_line).size() == 4;
  if (tdrive_format) {
    process_data_line(first_line, true);
  } else {
    trajectory.object_id = first_line;
  }
  while (std::getline(input, line)) {
    process_data_line(line, tdrive_format);
  }

  trajectory.points.reserve(by_time.size());
  for (const auto& entry : by_time) {
    trajectory.points.push_back(entry.second);
  }
  trajectory.stats.valid_points = trajectory.points.size();
  return trajectory;
}

std::vector<std::string> list_trajectory_files(const std::string& input_dir) {
  DIR* directory = opendir(input_dir.c_str());
  if (directory == nullptr) {
    throw std::runtime_error("input directory not found: " + input_dir);
  }
  std::vector<std::string> files;
  while (const dirent* entry = readdir(directory)) {
    const std::string name = entry->d_name;
    if (name.size() >= 4 &&
        (name.substr(name.size() - 4) == ".txt" ||
         name.substr(name.size() - 4) == ".plt")) {
      files.push_back(join_path(input_dir, name));
    }
  }
  closedir(directory);
  std::sort(files.begin(), files.end());
  if (files.empty()) {
    throw std::runtime_error("no .txt or .plt trajectory files found in: " +
                             input_dir);
  }
  return files;
}

double degrees_to_radians(double degrees) {
  return degrees * kPi / 180.0;
}

Projection calculate_projection(const std::vector<std::string>& files,
                                 double max_spike_speed_mps,
                                 std::int64_t max_time_gap_seconds,
                                 double max_average_sampling_interval_seconds) {
  long double lon_sum = 0.0;
  long double lat_sum = 0.0;
  std::uint64_t point_count = 0;
  for (const std::string& path : files) {
    const ParsedTrajectory trajectory = read_trajectory(path);
    std::size_t removed_spikes = 0;
    const std::vector<std::vector<RawPoint>> parts = preprocess_trajectory(
        trajectory.points, max_time_gap_seconds, max_spike_speed_mps,
        removed_spikes);
    for (const std::vector<RawPoint>& part : parts) {
      if (max_average_sampling_interval_seconds > 0.0 &&
          average_sampling_interval_seconds(part) >
              max_average_sampling_interval_seconds) {
        continue;
      }
      for (const RawPoint& point : part) {
        lon_sum += point.lon;
        lat_sum += point.lat;
        ++point_count;
      }
    }
  }
  if (point_count == 0) {
    throw std::runtime_error("dataset contains no valid trajectory points");
  }

  Projection projection;
  projection.ref_lon = static_cast<double>(lon_sum / point_count);
  projection.ref_lat = static_cast<double>(lat_sum / point_count);
  projection.ref_lon_rad = degrees_to_radians(projection.ref_lon);
  projection.ref_lat_rad = degrees_to_radians(projection.ref_lat);
  return projection;
}

Projection read_preprocessed_projection(const std::string& input_dir) {
  const std::string path = join_path(input_dir, "preprocessing.meta");
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error(
        "preprocessed input is missing preprocessing.meta: " + path);
  }
  Projection projection;
  bool has_lon = false;
  bool has_lat = false;
  std::string line;
  while (std::getline(input, line)) {
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    const std::string key = trim(line.substr(0, separator));
    const std::string value = trim(line.substr(separator + 1));
    if (key == "ref_lon") {
      projection.ref_lon = parse_finite_double(value);
      has_lon = true;
    } else if (key == "ref_lat") {
      projection.ref_lat = parse_finite_double(value);
      has_lat = true;
    }
  }
  if (!has_lon || !has_lat) {
    throw std::runtime_error(
        "preprocessing.meta must contain ref_lon and ref_lat");
  }
  projection.ref_lon_rad = degrees_to_radians(projection.ref_lon);
  projection.ref_lat_rad = degrees_to_radians(projection.ref_lat);
  return projection;
}

std::vector<MetricPoint> project_points(const std::vector<RawPoint>& raw_points,
                                        const Projection& projection) {
  std::vector<MetricPoint> points;
  points.reserve(raw_points.size());
  for (const RawPoint& raw : raw_points) {
    MetricPoint point;
    static_cast<RawPoint&>(point) = raw;
    const double lon_rad = degrees_to_radians(raw.lon);
    const double lat_rad = degrees_to_radians(raw.lat);
    point.x = (lon_rad - projection.ref_lon_rad) *
              std::cos(projection.ref_lat_rad) * kEarthRadiusMeters;
    point.y = (lat_rad - projection.ref_lat_rad) * kEarthRadiusMeters;
    points.push_back(point);
  }
  return points;
}

std::vector<MetricPoint> lonlat_coordinate_points(
    const std::vector<RawPoint>& raw_points) {
  std::vector<MetricPoint> points;
  points.reserve(raw_points.size());
  for (const RawPoint& raw : raw_points) {
    MetricPoint point;
    static_cast<RawPoint&>(point) = raw;
    point.x = raw.lon;
    point.y = raw.lat;
    points.push_back(point);
  }
  return points;
}

std::vector<MetricPoint> kilometer_coordinate_points(
    const std::vector<MetricPoint>& meter_points) {
  std::vector<MetricPoint> points = meter_points;
  for (MetricPoint& point : points) {
    point.x /= 1000.0;
    point.y /= 1000.0;
  }
  return points;
}

bool directory_contains_trajectory_files(const std::string& directory_path) {
  DIR* directory = opendir(directory_path.c_str());
  if (directory == nullptr) {
    return false;
  }
  bool found = false;
  while (const dirent* entry = readdir(directory)) {
    const std::string name = entry->d_name;
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".txt") {
      found = true;
      break;
    }
  }
  closedir(directory);
  return found;
}

void export_preprocessed_dataset(const std::vector<std::string>& files,
                                 const Config& config) {
  create_directory_tree(config.export_preprocessed_dir);
  if (directory_contains_trajectory_files(config.export_preprocessed_dir)) {
    throw std::runtime_error(
        "preprocessed output already contains .txt trajectories; use an empty directory");
  }

  const Projection projection = config.projection_metadata_dir.empty()
                                    ? calculate_projection(
                                          files, config.max_spike_speed_mps,
                                          config.max_time_gap_seconds,
                                          config.max_average_sampling_interval_seconds)
                                    : read_preprocessed_projection(
                                          config.projection_metadata_dir);
  std::ofstream summary(
      join_path(config.export_preprocessed_dir, "preprocessing_summary.csv"));
  if (!summary) {
    throw std::runtime_error("failed to create preprocessing_summary.csv");
  }
  summary << "trajectory_id,object_id,points\n";

  std::uint64_t exported_trajectories = 0;
  std::uint64_t exported_points = 0;
  std::size_t excluded_short = 0;
  std::size_t excluded_sampling_interval = 0;
  std::uint64_t removed_spikes = 0;
  for (const std::string& path : files) {
    const ParsedTrajectory trajectory = read_trajectory(path);
    std::size_t trajectory_removed_spikes = 0;
    std::vector<std::vector<RawPoint>> parts =
        apply_max_trajectory_points(
            preprocess_trajectory(trajectory.points,
                                  config.max_time_gap_seconds,
                                  config.max_spike_speed_mps,
                                  trajectory_removed_spikes),
            config.max_trajectory_points);
    removed_spikes += trajectory_removed_spikes;
    const std::vector<PreprocessedTrajectoryPart> finalized =
        finalize_preprocessed_parts(
            trajectory.trajectory_id, std::move(parts),
            config.max_average_sampling_interval_seconds,
            config.min_trajectory_points, true,
            excluded_sampling_interval, excluded_short);
    for (const PreprocessedTrajectoryPart& part : finalized) {
      const std::vector<RawPoint>& points = part.points;
      const std::string& trajectory_id = part.trajectory_id;
      std::ofstream output(join_path(config.export_preprocessed_dir,
                                     trajectory_id + ".txt"));
      if (!output) {
        throw std::runtime_error("failed to create preprocessed trajectory: " +
                                 trajectory_id);
      }
      output << trajectory.object_id << '\n' << std::setprecision(17);
      for (const RawPoint& point : points) {
        output << point.time << ',' << point.lon << ',' << point.lat << '\n';
      }
      summary << trajectory_id << ',' << trajectory.object_id << ','
              << points.size() << '\n';
      ++exported_trajectories;
      exported_points += points.size();
    }
  }

  std::ofstream metadata(
      join_path(config.export_preprocessed_dir, "preprocessing.meta"));
  if (!metadata) {
    throw std::runtime_error("failed to create preprocessing.meta");
  }
  metadata << std::setprecision(17)
           << "format=seb-preprocessed-v1\n"
           << "max_time_gap_seconds=" << config.max_time_gap_seconds << '\n'
           << "max_spike_speed_mps=" << config.max_spike_speed_mps << '\n'
           << "max_average_sampling_interval_seconds="
           << config.max_average_sampling_interval_seconds << '\n'
           << "min_trajectory_points=" << config.min_trajectory_points << '\n'
           << "max_trajectory_points=" << config.max_trajectory_points << '\n'
           << "ref_lon=" << projection.ref_lon << '\n'
           << "ref_lat=" << projection.ref_lat << '\n'
           << "exported_trajectories=" << exported_trajectories << '\n'
           << "exported_points=" << exported_points << '\n'
           << "excluded_short_trajectories=" << excluded_short << '\n'
           << "excluded_sampling_interval_trajectories="
           << excluded_sampling_interval << '\n'
           << "removed_isolated_spikes=" << removed_spikes << '\n';

  std::cout << "Preprocessed trajectories: " << exported_trajectories << '\n'
            << "Preprocessed points: " << exported_points << '\n'
            << "Short trajectories excluded: " << excluded_short << '\n'
            << "Sampling-interval filtered trajectories: "
            << excluded_sampling_interval << '\n'
            << "Preprocessed output directory: "
            << config.export_preprocessed_dir << '\n';
}

struct DtwResult {
  double distance = 0.0;
  std::size_t path_length = 0;
};

DtwResult exact_dtw_distance(const std::vector<MetricPoint>& original,
                             const std::vector<int>& kept_indices) {
  if (original.empty() || kept_indices.empty()) {
    return {};
  }
  const std::size_t m = kept_indices.size();
  const double infinity = std::numeric_limits<double>::infinity();
  std::vector<double> previous(m + 1, infinity);
  std::vector<double> current(m + 1, infinity);
  std::vector<std::size_t> previous_length(m + 1, 0);
  std::vector<std::size_t> current_length(m + 1, 0);
  previous[0] = 0.0;

  for (const MetricPoint& point : original) {
    current[0] = infinity;
    for (std::size_t j = 1; j <= m; ++j) {
      const double cost = planar_distance(point, original[kept_indices[j - 1]]);
      double predecessor = previous[j - 1];
      std::size_t predecessor_length = previous_length[j - 1];
      if (previous[j] < predecessor) {
        predecessor = previous[j];
        predecessor_length = previous_length[j];
      }
      if (current[j - 1] < predecessor) {
        predecessor = current[j - 1];
        predecessor_length = current_length[j - 1];
      }
      current[j] = cost + predecessor;
      current_length[j] = predecessor_length + 1;
    }
    std::swap(previous, current);
    std::swap(previous_length, current_length);
  }
  return DtwResult{previous[m], previous_length[m]};
}

DtwResult exact_dtw_distance(const std::vector<MetricPoint>& lhs,
                             const std::vector<MetricPoint>& rhs) {
  if (lhs.empty() || rhs.empty()) {
    return {};
  }
  const double infinity = std::numeric_limits<double>::infinity();
  std::vector<double> previous(rhs.size() + 1, infinity);
  std::vector<double> current(rhs.size() + 1, infinity);
  std::vector<std::size_t> previous_length(rhs.size() + 1, 0);
  std::vector<std::size_t> current_length(rhs.size() + 1, 0);
  previous[0] = 0.0;
  for (const MetricPoint& left : lhs) {
    current[0] = infinity;
    for (std::size_t j = 1; j <= rhs.size(); ++j) {
      const double cost = planar_distance(left, rhs[j - 1]);
      double predecessor = previous[j - 1];
      std::size_t predecessor_length = previous_length[j - 1];
      if (previous[j] < predecessor) {
        predecessor = previous[j];
        predecessor_length = previous_length[j];
      }
      if (current[j - 1] < predecessor) {
        predecessor = current[j - 1];
        predecessor_length = current_length[j - 1];
      }
      current[j] = cost + predecessor;
      current_length[j] = predecessor_length + 1;
    }
    std::swap(previous, current);
    std::swap(previous_length, current_length);
  }
  return DtwResult{previous[rhs.size()], previous_length[rhs.size()]};
}

std::vector<MetricPoint> reconstruct_trajectory(
    const std::vector<MetricPoint>& points, const std::vector<int>& kept_indices) {
  std::vector<MetricPoint> reconstructed = points;
  for (std::size_t segment = 1; segment < kept_indices.size(); ++segment) {
    const int start_index = kept_indices[segment - 1];
    const int end_index = kept_indices[segment];
    const MetricPoint& start = points[start_index];
    const MetricPoint& end = points[end_index];
    const double dt = static_cast<double>(end.time - start.time);
    for (int i = start_index; i <= end_index; ++i) {
      const double ratio =
          dt <= 0.0 ? 0.0 : static_cast<double>(points[i].time - start.time) / dt;
      reconstructed[i].x = start.x + ratio * (end.x - start.x);
      reconstructed[i].y = start.y + ratio * (end.y - start.y);
    }
  }
  return reconstructed;
}

ErrorMetrics evaluate_trajectory(const std::vector<MetricPoint>& points,
                                 const std::vector<int>& kept_indices,
                                 const std::string& dtw_mode) {
  ErrorMetrics metrics;
  metrics.point_count = points.size();
  if (points.empty()) {
    return metrics;
  }

  long double ped_sum = 0.0;
  long double sed_sum = 0.0;
  long double sad_sum = 0.0;
  long double dad_sum = 0.0;
  long double speed_error_sum = 0.0;
  long double direction_error_sum = 0.0;
  for (std::size_t segment = 1; segment < kept_indices.size(); ++segment) {
    const int start_index = kept_indices[segment - 1];
    const int end_index = kept_indices[segment];
    const MetricPoint& start = points[start_index];
    const MetricPoint& end = points[end_index];
    const double segment_dt = static_cast<double>(end.time - start.time);
    const double segment_distance = planar_distance(start, end);
    const double segment_speed = segment_dt > 0.0 ? segment_distance / segment_dt : 0.0;
    const double segment_direction = direction_angle(start, end);

    for (int i = start_index; i < end_index; ++i) {
      const MetricPoint& point = points[i];
      const MetricPoint& next = points[i + 1];
      const double ratio = segment_dt > 0.0
                               ? static_cast<double>(point.time - start.time) / segment_dt
                               : 0.0;
      const double interpolated_x = start.x + ratio * (end.x - start.x);
      const double interpolated_y = start.y + ratio * (end.y - start.y);
      const double ped = perpendicular_line_distance(point, start, end);
      const double sed = std::hypot(point.x - interpolated_x,
                                    point.y - interpolated_y);
      const double local_dt = static_cast<double>(next.time - point.time);
      const double local_distance = planar_distance(point, next);
      const double local_speed = local_dt > 0.0 ? local_distance / local_dt : 0.0;
      const double speed_error = std::fabs(local_speed - segment_speed);
      const double direction_error =
          angle_difference(direction_angle(point, next), segment_direction);
      const double sad = speed_error * local_dt;
      const double dad = local_distance * std::sin(direction_error);
      ped_sum += ped;
      sed_sum += sed;
      sad_sum += sad;
      dad_sum += dad;
      speed_error_sum += speed_error;
      direction_error_sum += direction_error;
      metrics.maximum_ped = std::max(metrics.maximum_ped, ped);
      metrics.maximum_sed = std::max(metrics.maximum_sed, sed);
      metrics.maximum_sad = std::max(metrics.maximum_sad, sad);
      metrics.maximum_dad = std::max(metrics.maximum_dad, dad);
      metrics.maximum_speed_error =
          std::max(metrics.maximum_speed_error, speed_error);
      metrics.maximum_direction_error =
          std::max(metrics.maximum_direction_error, direction_error);
    }
  }

  const long double denominator = static_cast<long double>(points.size());
  metrics.average_ped = static_cast<double>(ped_sum / denominator);
  metrics.average_sed = static_cast<double>(sed_sum / denominator);
  metrics.average_sad = static_cast<double>(sad_sum / denominator);
  metrics.average_dad = static_cast<double>(dad_sum / denominator);
  metrics.average_speed_error =
      static_cast<double>(speed_error_sum / denominator);
  metrics.average_direction_error =
      static_cast<double>(direction_error_sum / denominator);
  DtwResult retained_dtw;
  if (dtw_mode == "both" || dtw_mode == "retained") {
    retained_dtw = exact_dtw_distance(points, kept_indices);
  }
  DtwResult reconstructed_dtw;
  if (dtw_mode == "both" || dtw_mode == "reconstructed") {
    const std::vector<MetricPoint> reconstructed =
        reconstruct_trajectory(points, kept_indices);
    reconstructed_dtw = exact_dtw_distance(points, reconstructed);
  }
  metrics.retained_dtw = retained_dtw.distance;
  metrics.reconstructed_dtw = reconstructed_dtw.distance;
  metrics.retained_dtw_path_length = retained_dtw.path_length;
  metrics.reconstructed_dtw_path_length = reconstructed_dtw.path_length;
  metrics.retained_dtw_per_path_step =
      retained_dtw.path_length == 0
          ? 0.0
          : retained_dtw.distance / retained_dtw.path_length;
  metrics.reconstructed_dtw_per_path_step =
      reconstructed_dtw.path_length == 0
          ? 0.0
          : reconstructed_dtw.distance / reconstructed_dtw.path_length;
  return metrics;
}

void write_compressed_points(std::ostream& output,
                             const ParsedTrajectory& trajectory,
                             const SimplificationResult& simplification) {
  for (int index : simplification.kept_indices) {
    const RawPoint& point = trajectory.points[index];
    output << trajectory.trajectory_id << ',' << trajectory.object_id << ','
           << point.time << ',' << point.lon << ',' << point.lat << '\n';
  }
}

void write_projection_metadata(const std::string& output_dir,
                               const Projection& projection,
                               std::int64_t max_time_gap_seconds,
                               double max_spike_speed_mps,
                               double max_average_sampling_interval_seconds,
                               const std::string& scoring_distance,
                               bool exclude_short_trajectories,
                               std::size_t min_trajectory_points,
                               const std::string& dtw_mode,
                               std::int64_t max_interpolation_gap_seconds,
                               bool input_is_preprocessed) {
  std::ofstream output(join_path(output_dir, "projection.meta"));
  if (!output) {
    throw std::runtime_error("failed to create projection.meta");
  }
  output << std::setprecision(17)
         << "projection=" << projection.name << '\n'
         << "ref_lon=" << projection.ref_lon << '\n'
         << "ref_lat=" << projection.ref_lat << '\n'
         << "earth_radius_m=" << kEarthRadiusMeters << '\n'
         << "input_coordinates=lonlat\n"
         << "scoring_distance=" << scoring_distance << '\n'
         << "max_time_gap_seconds=" << max_time_gap_seconds << '\n'
         << "max_spike_speed_mps=" << max_spike_speed_mps << '\n'
         << "max_average_sampling_interval_seconds="
         << max_average_sampling_interval_seconds << '\n'
         << "exclude_short_trajectories="
         << (exclude_short_trajectories ? "true" : "false") << '\n'
         << "min_trajectory_points=" << min_trajectory_points << '\n'
         << "dtw_mode=" << dtw_mode << '\n'
         << "max_interpolation_gap_seconds="
         << max_interpolation_gap_seconds << '\n'
         << "pipeline_stage="
         << (input_is_preprocessed ? "seb-only" : "integrated") << '\n'
         << "unit=meter\n";
}

void write_summary(const std::string& output_dir,
                   const std::vector<TrajectorySummary>& summaries) {
  std::ofstream output(join_path(output_dir, "summary.csv"));
  if (!output) {
    throw std::runtime_error("failed to create summary.csv");
  }
  output << "trajectory_id,object_id,raw_points,invalid_points,duplicate_times,"
            "valid_points,kept_points,status\n";
  for (const TrajectorySummary& summary : summaries) {
    output << summary.trajectory_id << ',' << summary.object_id << ','
           << summary.stats.raw_points << ',' << summary.stats.invalid_points << ','
           << summary.stats.duplicate_times << ',' << summary.stats.valid_points << ','
           << summary.kept_points << ',' << summary.status << '\n';
  }
}

void write_metrics(const std::string& output_dir, const std::string& filename,
                   const std::string& distance_unit,
                   const std::string& speed_unit,
                   const std::string& cumulative_dtw_unit,
                   double cumulative_dtw_scale,
                   const std::vector<std::pair<std::string, ErrorMetrics>>& all_metrics) {
  std::ofstream output(join_path(output_dir, filename));
  if (!output) {
    throw std::runtime_error("failed to create " + filename);
  }
  output << "trajectory_id,points,scored_feature_count,average_selected_score,"
            "maximum_selected_score,average_ped_" << distance_unit
         << ",maximum_ped_" << distance_unit << ",average_sed_" << distance_unit
         << ",maximum_sed_" << distance_unit << ",average_speed_error_"
         << speed_unit << ",maximum_speed_error_" << speed_unit
         << ",average_direction_error_rad,maximum_direction_error_rad,average_sad_"
         << distance_unit << ",maximum_sad_" << distance_unit << ",average_dad_"
         << distance_unit << ",maximum_dad_" << distance_unit
         << ",retained_cumulative_dtw_" << cumulative_dtw_unit
         << ",reconstructed_cumulative_dtw_" << cumulative_dtw_unit
         << ",retained_dtw_path_length,reconstructed_dtw_path_length"
         << ",retained_dtw_per_path_step_" << distance_unit
         << ",reconstructed_dtw_per_path_step_" << distance_unit << "\n";
  output << std::setprecision(17);
  for (const auto& entry : all_metrics) {
    const ErrorMetrics& m = entry.second;
    output << entry.first << ',' << m.point_count << ',' << m.scored_feature_count
           << ','
           << (m.scored_feature_count == 0
                   ? 0.0
                   : m.selected_score_sum / m.scored_feature_count)
           << ',' << m.maximum_selected_score << ',' << m.average_ped << ','
           << m.maximum_ped << ',' << m.average_sed << ',' << m.maximum_sed << ','
           << m.average_speed_error << ',' << m.maximum_speed_error << ','
           << m.average_direction_error << ',' << m.maximum_direction_error << ','
           << m.average_sad << ',' << m.maximum_sad << ',' << m.average_dad << ','
           << m.maximum_dad << ',' << m.retained_dtw * cumulative_dtw_scale << ','
           << m.reconstructed_dtw * cumulative_dtw_scale << ','
           << m.retained_dtw_path_length << ','
           << m.reconstructed_dtw_path_length << ','
           << m.retained_dtw_per_path_step << ','
           << m.reconstructed_dtw_per_path_step << '\n';
  }
}

void write_dataset_metrics(
    const std::string& output_dir, const std::string& filename,
    const std::string& distance_unit, const std::string& speed_unit,
    const std::string& cumulative_dtw_unit, double cumulative_dtw_scale,
    const std::vector<std::pair<std::string, ErrorMetrics>>& all_metrics,
    std::uint64_t valid_points, std::uint64_t evaluated_points,
    std::uint64_t kept_points,
    double compression_seconds, double evaluation_seconds) {
  long double ped_sum = 0.0;
  long double sed_sum = 0.0;
  long double sad_sum = 0.0;
  long double dad_sum = 0.0;
  long double speed_error_sum = 0.0;
  long double direction_error_sum = 0.0;
  long double retained_dtw_sum = 0.0;
  long double reconstructed_dtw_sum = 0.0;
  long double retained_path_dtw_sum = 0.0;
  long double reconstructed_path_dtw_sum = 0.0;
  long double trajectory_ped_sum = 0.0;
  long double trajectory_sed_sum = 0.0;
  long double trajectory_speed_sum = 0.0;
  long double trajectory_direction_sum = 0.0;
  long double selected_score_sum = 0.0;
  std::uint64_t scored_feature_count = 0;
  double maximum_selected_score = 0.0;
  double max_ped = 0.0;
  double max_sed = 0.0;
  double max_sad = 0.0;
  double max_dad = 0.0;
  double max_speed_error = 0.0;
  double max_direction_error = 0.0;
  for (const auto& entry : all_metrics) {
    const ErrorMetrics& m = entry.second;
    ped_sum += static_cast<long double>(m.average_ped) * m.point_count;
    sed_sum += static_cast<long double>(m.average_sed) * m.point_count;
    sad_sum += static_cast<long double>(m.average_sad) * m.point_count;
    dad_sum += static_cast<long double>(m.average_dad) * m.point_count;
    speed_error_sum +=
        static_cast<long double>(m.average_speed_error) * m.point_count;
    direction_error_sum +=
        static_cast<long double>(m.average_direction_error) * m.point_count;
    retained_dtw_sum += m.retained_dtw;
    reconstructed_dtw_sum += m.reconstructed_dtw;
    retained_path_dtw_sum += m.retained_dtw_per_path_step;
    reconstructed_path_dtw_sum += m.reconstructed_dtw_per_path_step;
    trajectory_ped_sum += m.average_ped;
    trajectory_sed_sum += m.average_sed;
    trajectory_speed_sum += m.average_speed_error;
    trajectory_direction_sum += m.average_direction_error;
    selected_score_sum += m.selected_score_sum;
    scored_feature_count += m.scored_feature_count;
    maximum_selected_score =
        std::max(maximum_selected_score, m.maximum_selected_score);
    max_ped = std::max(max_ped, m.maximum_ped);
    max_sed = std::max(max_sed, m.maximum_sed);
    max_sad = std::max(max_sad, m.maximum_sad);
    max_dad = std::max(max_dad, m.maximum_dad);
    max_speed_error = std::max(max_speed_error, m.maximum_speed_error);
    max_direction_error =
        std::max(max_direction_error, m.maximum_direction_error);
  }
  const long double point_denominator =
      evaluated_points == 0 ? 1.0L : evaluated_points;
  const long double trajectory_denominator =
      all_metrics.empty() ? 1.0L : static_cast<long double>(all_metrics.size());

  std::ofstream output(join_path(output_dir, filename));
  if (!output) {
    throw std::runtime_error("failed to create " + filename);
  }
  output << "trajectories,valid_points,evaluated_points,kept_points,keep_ratio,compression_ratio,"
            "scored_feature_count,average_selected_score,maximum_selected_score,"
         << "average_ped_" << distance_unit << ",maximum_ped_" << distance_unit
         << ",average_sed_" << distance_unit << ",maximum_sed_" << distance_unit
         << ",average_speed_error_" << speed_unit << ",maximum_speed_error_"
         << speed_unit
         << ",average_direction_error_rad,maximum_direction_error_rad,average_sad_"
         << distance_unit << ",maximum_sad_" << distance_unit << ",average_dad_"
         << distance_unit << ",maximum_dad_" << distance_unit
         << ",trajectory_mean_ped_" << distance_unit
         << ",trajectory_mean_sed_" << distance_unit
         << ",trajectory_mean_speed_error_" << speed_unit
         << ",trajectory_mean_direction_error_rad"
         << ",average_retained_cumulative_dtw_" << cumulative_dtw_unit
         << ",average_reconstructed_cumulative_dtw_" << cumulative_dtw_unit
         << ",average_retained_dtw_per_path_step_" << distance_unit
         << ",average_reconstructed_dtw_per_path_step_" << distance_unit
         << ",compression_seconds,evaluation_seconds\n";
  output << std::setprecision(17) << all_metrics.size() << ',' << valid_points << ','
         << evaluated_points << ',' << kept_points << ','
         << (valid_points == 0 ? 0.0
                               : static_cast<double>(kept_points) / valid_points)
         << ','
         << (kept_points == 0 ? 0.0
                              : static_cast<double>(valid_points) / kept_points)
         << ',' << scored_feature_count << ','
         << (scored_feature_count == 0
                 ? 0.0
                 : static_cast<double>(selected_score_sum / scored_feature_count))
         << ',' << maximum_selected_score
         << ',' << static_cast<double>(ped_sum / point_denominator) << ',' << max_ped
         << ',' << static_cast<double>(sed_sum / point_denominator) << ',' << max_sed
         << ',' << static_cast<double>(speed_error_sum / point_denominator) << ','
         << max_speed_error << ','
         << static_cast<double>(direction_error_sum / point_denominator) << ','
         << max_direction_error
         << ',' << static_cast<double>(sad_sum / point_denominator) << ',' << max_sad
         << ',' << static_cast<double>(dad_sum / point_denominator) << ',' << max_dad
         << ',' << static_cast<double>(trajectory_ped_sum / trajectory_denominator)
         << ',' << static_cast<double>(trajectory_sed_sum / trajectory_denominator)
         << ',' << static_cast<double>(trajectory_speed_sum / trajectory_denominator)
         << ',' << static_cast<double>(trajectory_direction_sum / trajectory_denominator)
         << ',' << static_cast<double>(retained_dtw_sum / trajectory_denominator) *
                        cumulative_dtw_scale
         << ',' << static_cast<double>(reconstructed_dtw_sum / trajectory_denominator) *
                        cumulative_dtw_scale
         << ',' << static_cast<double>(retained_path_dtw_sum / trajectory_denominator)
         << ',' << static_cast<double>(reconstructed_path_dtw_sum /
                                       trajectory_denominator)
         << ',' << compression_seconds << ',' << evaluation_seconds << '\n';
}

Config parse_args(int argc, char** argv) {
  Config config;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](const std::string& name) {
      if (++i >= argc) {
        throw std::runtime_error("missing value for " + name);
      }
      return std::string(argv[i]);
    };

    if (arg == "--input-dir") {
      config.input_dir = value(arg);
    } else if (arg == "--output-dir") {
      config.output_dir = value(arg);
    } else if (arg == "--export-preprocessed") {
      config.export_preprocessed_dir = value(arg);
    } else if (arg == "--projection-metadata-dir") {
      config.projection_metadata_dir = value(arg);
    } else if (arg == "--input-is-preprocessed") {
      config.input_is_preprocessed = true;
    } else if (arg == "--budget") {
      config.budget = parse_finite_double(value(arg));
    } else if (arg == "--max-time-gap") {
      config.max_time_gap_seconds = std::stoll(value(arg));
    } else if (arg == "--max-interpolation-gap") {
      config.max_interpolation_gap_seconds = std::stoll(value(arg));
    } else if (arg == "--max-spike-speed") {
      config.max_spike_speed_mps = parse_finite_double(value(arg));
    } else if (arg == "--max-average-sampling-interval") {
      config.max_average_sampling_interval_seconds =
          parse_finite_double(value(arg));
    } else if (arg == "--scoring-distance") {
      config.scoring_distance = value(arg);
    } else if (arg == "--dtw-mode") {
      config.dtw_mode = value(arg);
    } else if (arg == "--exclude-short-trajectories") {
      config.exclude_short_trajectories = true;
    } else if (arg == "--include-short-trajectories") {
      config.exclude_short_trajectories = false;
    } else if (arg == "--min-trajectory-points") {
      config.min_trajectory_points =
          static_cast<std::size_t>(std::stoull(value(arg)));
    } else if (arg == "--max-trajectory-points") {
      config.max_trajectory_points =
          static_cast<std::size_t>(std::stoull(value(arg)));
    } else if (arg == "--preprocess-only") {
      config.preprocess_only = true;
    } else if (arg == "--evaluate") {
      config.evaluate = true;
      config.detailed_evaluation = true;
    } else if (arg == "--no-evaluate") {
      config.evaluate = false;
      config.detailed_evaluation = false;
    } else if (arg == "--ped-weight") {
      config.weights.ped = parse_finite_double(value(arg));
    } else if (arg == "--sed-weight") {
      config.weights.sed = parse_finite_double(value(arg));
    } else if (arg == "--sad-weight") {
      config.weights.sad = parse_finite_double(value(arg));
    } else if (arg == "--dad-weight") {
      config.weights.dad = parse_finite_double(value(arg));
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: seb_cli --input-dir DIR --output-dir DIR [options]\n"
                << "Chengdu input: first line object id; remaining lines time,lon,lat\n"
                << "Options:\n"
                << "  --export-preprocessed DIR clean and export trajectories, then exit\n"
                << "  --projection-metadata-dir DIR reuse ref_lon/ref_lat from preprocessing.meta\n"
                << "  --input-is-preprocessed skip cleaning and read preprocessing.meta\n"
                << "  --budget W       retained-point budget in (0,1] (default 0.2)\n"
                << "  --max-time-gap S split when the gap exceeds S seconds (default 300; 0 disables)\n"
                << "  --max-interpolation-gap S force both sides of larger gaps to be retained (default 0)\n"
                << "  --max-spike-speed V remove isolated spikes and split jumps above V m/s (default 50; 0 disables)\n"
                << "  --max-average-sampling-interval S keep segments averaging at most S seconds\n"
                << "  --scoring-distance MODE degrees (default), meters, or kilometers\n"
                << "  --dtw-mode MODE both (default), retained, reconstructed, or none\n"
                << "  --exclude-short-trajectories omit short segments (default)\n"
                << "  --include-short-trajectories retain short segments without SEB\n"
                << "  --min-trajectory-points N minimum segment size (default 50)\n"
                << "  --max-trajectory-points N balanced maximum segment size (default 0, unlimited)\n"
                << "  --preprocess-only report cleaning and split counts without compression\n"
                << "  --evaluate        also write detailed per-trajectory and lon/lat metrics\n"
                << "  --no-evaluate     disable reconstructed-trajectory metric calculation\n"
                << "  --ped-weight W   default 0.3\n"
                << "  --sed-weight W   default 0.5\n"
                << "  --sad-weight W   default 0.1\n"
                << "  --dad-weight W   default 0.1\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }

  if (config.input_dir.empty()) {
    throw std::runtime_error("--input-dir is required");
  }
  if (!config.preprocess_only && config.export_preprocessed_dir.empty() &&
      config.output_dir.empty()) {
    throw std::runtime_error(
        "--output-dir is required unless preprocessing-only output is requested");
  }
  if (config.input_is_preprocessed &&
      (!config.export_preprocessed_dir.empty() || config.preprocess_only)) {
    throw std::runtime_error(
        "--input-is-preprocessed cannot be combined with preprocessing modes");
  }
  if (config.input_is_preprocessed &&
      !config.projection_metadata_dir.empty()) {
    throw std::runtime_error(
        "--projection-metadata-dir cannot be combined with --input-is-preprocessed");
  }
  if (!(config.budget > 0.0 && config.budget <= 1.0)) {
    throw std::runtime_error("--budget must be in (0,1]");
  }
  if (config.max_time_gap_seconds < 0) {
    throw std::runtime_error("--max-time-gap must be non-negative");
  }
  if (config.max_interpolation_gap_seconds < 0) {
    throw std::runtime_error("--max-interpolation-gap must be non-negative");
  }
  if (config.max_spike_speed_mps < 0.0) {
    throw std::runtime_error("--max-spike-speed must be non-negative");
  }
  if (config.max_average_sampling_interval_seconds < 0.0) {
    throw std::runtime_error(
        "--max-average-sampling-interval must be non-negative");
  }
  if (config.scoring_distance != "meters" &&
      config.scoring_distance != "kilometers" &&
      config.scoring_distance != "degrees") {
    throw std::runtime_error(
        "--scoring-distance must be meters, kilometers, or degrees");
  }
  if (config.dtw_mode != "both" && config.dtw_mode != "retained" &&
      config.dtw_mode != "reconstructed" && config.dtw_mode != "none") {
    throw std::runtime_error(
        "--dtw-mode must be both, retained, reconstructed, or none");
  }
  if (config.min_trajectory_points < 2) {
    throw std::runtime_error("--min-trajectory-points must be at least 2");
  }
  if (config.max_trajectory_points > 0 &&
      config.max_trajectory_points < 2 * config.min_trajectory_points) {
    throw std::runtime_error(
        "--max-trajectory-points must be 0 or at least twice "
        "--min-trajectory-points");
  }
  if (config.weights.ped < 0.0 || config.weights.sed < 0.0 ||
      config.weights.sad < 0.0 || config.weights.dad < 0.0) {
    throw std::runtime_error("score weights must be non-negative");
  }
  return config;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Config config = parse_args(argc, argv);
    const std::vector<std::string> files = list_trajectory_files(config.input_dir);
    if (!config.export_preprocessed_dir.empty()) {
      export_preprocessed_dataset(files, config);
      return 0;
    }
    if (config.preprocess_only) {
      std::uint64_t raw_points = 0;
      std::uint64_t invalid_points = 0;
      std::uint64_t duplicate_times = 0;
      std::uint64_t valid_points = 0;
      std::uint64_t trajectories = 0;
      std::uint64_t short_trajectories = 0;
      std::uint64_t sampling_interval_filtered = 0;
      for (const std::string& path : files) {
        const ParsedTrajectory trajectory = read_trajectory(path);
        std::size_t removed_spikes = 0;
        const std::vector<std::vector<RawPoint>> parts =
            apply_max_trajectory_points(
                preprocess_trajectory(trajectory.points,
                                      config.max_time_gap_seconds,
                                      config.max_spike_speed_mps,
                                      removed_spikes),
                config.max_trajectory_points);
        raw_points += trajectory.stats.raw_points;
        invalid_points += trajectory.stats.invalid_points + removed_spikes;
        duplicate_times += trajectory.stats.duplicate_times;
        for (const std::vector<RawPoint>& part : parts) {
          if (config.max_average_sampling_interval_seconds > 0.0 &&
              average_sampling_interval_seconds(part) >
                  config.max_average_sampling_interval_seconds) {
            ++sampling_interval_filtered;
            continue;
          }
          valid_points += part.size();
          ++trajectories;
          if (part.size() < config.min_trajectory_points) {
            ++short_trajectories;
          }
        }
      }
      std::cout << "Input files: " << files.size() << '\n'
                << "Raw points: " << raw_points << '\n'
                << "Invalid or speed-filtered points: " << invalid_points << '\n'
                << "Duplicate timestamps: " << duplicate_times << '\n'
                << "Valid points: " << valid_points << '\n'
                << "Split trajectories: " << trajectories << '\n'
                << "Trajectories below " << config.min_trajectory_points
                << " points: " << short_trajectories << '\n'
                << "Sampling-interval filtered trajectories: "
                << sampling_interval_filtered << '\n';
      return 0;
    }
    const Projection projection =
        config.input_is_preprocessed
            ? read_preprocessed_projection(config.input_dir)
        : !config.projection_metadata_dir.empty()
            ? read_preprocessed_projection(config.projection_metadata_dir)
            : calculate_projection(
                  files, config.max_spike_speed_mps,
                  config.max_time_gap_seconds,
                  config.max_average_sampling_interval_seconds);

    create_directory_tree(config.output_dir);
    write_projection_metadata(config.output_dir, projection,
                              config.max_time_gap_seconds,
                              config.max_spike_speed_mps,
                              config.max_average_sampling_interval_seconds,
                              config.scoring_distance,
                              config.exclude_short_trajectories,
                              config.min_trajectory_points, config.dtw_mode,
                              config.max_interpolation_gap_seconds,
                              config.input_is_preprocessed);

    std::ofstream compressed_points(
        join_path(config.output_dir, "compressed_points.csv"));
    std::ofstream trajectory_index;
    if (config.detailed_evaluation) {
      trajectory_index.open(join_path(config.output_dir, "trajectory_index.csv"));
    }
    if (!compressed_points ||
        (config.detailed_evaluation && !trajectory_index)) {
      throw std::runtime_error("failed to create SEB trajectory outputs");
    }
    compressed_points << "trajectory_id,object_id,time,lon,lat\n"
                      << std::setprecision(17);
    if (config.detailed_evaluation) {
      trajectory_index
          << "trajectory_id,object_id,start_row,kept_points,original_points,status,"
             "scored_feature_count,average_selected_score,maximum_selected_score\n";
    }

    std::vector<TrajectorySummary> summaries;
    summaries.reserve(files.size());
    std::vector<std::pair<std::string, ErrorMetrics>> all_metrics;
    all_metrics.reserve(files.size());
    std::vector<std::pair<std::string, ErrorMetrics>> all_lonlat_metrics;
    all_lonlat_metrics.reserve(files.size());
    std::uint64_t valid_points = 0;
    std::uint64_t evaluated_points = 0;
    std::uint64_t kept_points = 0;
    std::uint64_t compressed_row = 0;
    std::size_t sampling_interval_filtered = 0;
    std::size_t short_trajectories_excluded = 0;
    double compression_seconds = 0.0;
    double evaluation_seconds = 0.0;

    for (const std::string& path : files) {
      ParsedTrajectory trajectory = read_trajectory(path);
      if (trajectory.points.empty()) {
        TrajectorySummary summary{trajectory.trajectory_id, trajectory.object_id,
                                  trajectory.stats, 0, "no_valid_points"};
        summaries.push_back(summary);
        continue;
      }

      std::size_t removed_spikes = 0;
      std::vector<PreprocessedTrajectoryPart> finalized;
      if (config.input_is_preprocessed) {
        finalized.push_back(
            PreprocessedTrajectoryPart{trajectory.trajectory_id,
                                       trajectory.points});
      } else {
        finalized = finalize_preprocessed_parts(
            trajectory.trajectory_id,
            apply_max_trajectory_points(
                preprocess_trajectory(trajectory.points,
                                      config.max_time_gap_seconds,
                                      config.max_spike_speed_mps,
                                      removed_spikes),
                config.max_trajectory_points),
            config.max_average_sampling_interval_seconds,
            config.min_trajectory_points, config.exclude_short_trajectories,
            sampling_interval_filtered, short_trajectories_excluded);
      }
      for (const PreprocessedTrajectoryPart& finalized_part : finalized) {
        ParsedTrajectory part;
        part.trajectory_id = finalized_part.trajectory_id;
        part.object_id = trajectory.object_id;
        part.points = finalized_part.points;
        part.stats.raw_points = part.points.size();
        part.stats.valid_points = part.points.size();
        const bool too_short = !config.input_is_preprocessed &&
                               part.points.size() < config.min_trajectory_points;
        TrajectorySummary summary{part.trajectory_id, part.object_id, part.stats, 0,
                                  too_short ? "too_short" : "ok"};

        const std::vector<MetricPoint> meter_points =
            project_points(part.points, projection);
        const std::vector<MetricPoint> lonlat_points =
            lonlat_coordinate_points(part.points);
        const std::vector<MetricPoint> kilometer_points =
            config.scoring_distance == "kilometers"
                ? kilometer_coordinate_points(meter_points)
                : std::vector<MetricPoint>();
        const auto compression_start = std::chrono::steady_clock::now();
        SimplificationResult simplification;
        if (too_short) {
          simplification.kept_indices.reserve(part.points.size());
          for (std::size_t i = 0; i < part.points.size(); ++i) {
            simplification.kept_indices.push_back(static_cast<int>(i));
          }
        } else {
          const std::vector<MetricPoint>* scoring_points = &lonlat_points;
          if (config.scoring_distance == "meters") {
            scoring_points = &meter_points;
          } else if (config.scoring_distance == "kilometers") {
            scoring_points = &kilometer_points;
          }
          simplification =
              simplify_trajectory(*scoring_points, config.budget, config.weights,
                                  config.max_interpolation_gap_seconds);
        }
        const std::vector<int>& kept = simplification.kept_indices;
        const auto compression_end = std::chrono::steady_clock::now();
        compression_seconds +=
            std::chrono::duration<double>(compression_end - compression_start).count();

        if (config.evaluate && !too_short) {
          const auto evaluation_start = std::chrono::steady_clock::now();
          ErrorMetrics metrics =
              evaluate_trajectory(meter_points, kept, config.dtw_mode);
          for (const auto& selected : simplification.selected_features) {
            const double score = selected.second.candidate.score;
            metrics.selected_score_sum += score;
            metrics.maximum_selected_score =
                std::max(metrics.maximum_selected_score, score);
            ++metrics.scored_feature_count;
          }
          if (config.detailed_evaluation) {
            ErrorMetrics lonlat_metrics =
                evaluate_trajectory(lonlat_points, kept, config.dtw_mode);
            lonlat_metrics.selected_score_sum = metrics.selected_score_sum;
            lonlat_metrics.maximum_selected_score =
                metrics.maximum_selected_score;
            lonlat_metrics.scored_feature_count = metrics.scored_feature_count;
            all_lonlat_metrics.emplace_back(part.trajectory_id,
                                            lonlat_metrics);
          }
          const auto evaluation_end = std::chrono::steady_clock::now();
          evaluation_seconds +=
              std::chrono::duration<double>(evaluation_end - evaluation_start).count();
          all_metrics.emplace_back(part.trajectory_id, metrics);
          evaluated_points += meter_points.size();
        }
        write_compressed_points(compressed_points, part, simplification);
        double selected_score_sum = 0.0;
        double maximum_selected_score = 0.0;
        for (const auto& selected : simplification.selected_features) {
          selected_score_sum += selected.second.candidate.score;
          maximum_selected_score =
              std::max(maximum_selected_score, selected.second.candidate.score);
        }
        if (config.detailed_evaluation) {
          trajectory_index << part.trajectory_id << ',' << part.object_id << ','
                           << compressed_row << ',' << kept.size() << ','
                           << part.points.size() << ',' << summary.status << ','
                           << simplification.selected_features.size() << ','
                           << (simplification.selected_features.empty()
                                   ? 0.0
                                   : selected_score_sum /
                                         static_cast<double>(
                                             simplification.selected_features.size()))
                           << ',' << maximum_selected_score << '\n';
        }
        compressed_row += kept.size();
        summary.kept_points = kept.size();
        summaries.push_back(summary);
        valid_points += meter_points.size();
        kept_points += kept.size();
      }
    }
    if (config.evaluate) {
      write_dataset_metrics(config.output_dir, "dataset_metrics.csv", "m", "mps",
                            "km", 0.001, all_metrics, valid_points,
                            evaluated_points, kept_points, compression_seconds,
                            evaluation_seconds);
      if (config.detailed_evaluation) {
        write_summary(config.output_dir, summaries);
        write_metrics(config.output_dir, "metrics.csv", "m", "mps", "km",
                      0.001, all_metrics);
        write_metrics(config.output_dir, "metrics_lonlat.csv", "deg", "degps",
                      "deg", 1.0, all_lonlat_metrics);
        write_dataset_metrics(config.output_dir, "dataset_metrics_lonlat.csv",
                              "deg", "degps", "deg", 1.0,
                              all_lonlat_metrics, valid_points, evaluated_points,
                              kept_points, compression_seconds,
                              evaluation_seconds);
      }
    }

    std::cout << "Split trajectories: " << summaries.size() << '\n'
              << "SEB trajectories: " << summaries.size() << '\n'
              << "Sampling-interval filtered trajectories: "
              << sampling_interval_filtered << '\n'
              << "Short trajectories excluded: " << short_trajectories_excluded
              << '\n'
              << "Valid points: " << valid_points << '\n'
              << "Kept points: " << kept_points << '\n'
              << "Keep ratio: " << std::setprecision(8)
              << (valid_points == 0
                      ? 0.0
                      : static_cast<double>(kept_points) /
                            static_cast<double>(valid_points))
              << '\n'
              << "Compression seconds: " << compression_seconds << '\n'
              << "Evaluation seconds: " << evaluation_seconds << '\n'
              << "Output directory: " << config.output_dir << '\n';
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "seb_cli error: " << ex.what() << '\n';
    return 1;
  }
}
