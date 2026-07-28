#include "cfpf_evaluation.h"

#include <algorithm>
#include <cstdint>
#include <direct.h>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::vector<std::string> split_csv(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t begin = 0;
  while (true) {
    const std::size_t comma = line.find(',', begin);
    fields.push_back(line.substr(begin, comma - begin));
    if (comma == std::string::npos) {
      return fields;
    }
    begin = comma + 1;
  }
}

std::string join_path(const std::string& directory, const std::string& name) {
  if (directory.empty() || directory.back() == '/' || directory.back() == '\\') {
    return directory + name;
  }
  // The evaluator is compiled and executed on Linux as well as Windows.
  // A backslash is not a directory separator for Linux file streams.
  return directory + "/" + name;
}

std::string file_stem(const std::string& path) {
  const std::size_t slash = path.find_last_of("/\\");
  const std::size_t begin = slash == std::string::npos ? 0 : slash + 1;
  const std::size_t dot = path.find_last_of('.');
  const std::size_t end = dot == std::string::npos || dot < begin ? path.size() : dot;
  return path.substr(begin, end - begin);
}

std::vector<std::string> trajectory_files(const std::string& directory) {
  DIR* handle = opendir(directory.c_str());
  if (handle == nullptr) {
    throw std::runtime_error("original trajectory directory not found");
  }
  std::vector<std::string> files;
  while (const dirent* entry = readdir(handle)) {
    const std::string name = entry->d_name;
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".txt") {
      files.push_back(join_path(directory, name));
    }
  }
  closedir(handle);
  std::sort(files.begin(), files.end());
  return files;
}

CfpfTrajectory read_original(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open original trajectory: " + path);
  }
  CfpfTrajectory trajectory;
  trajectory.trajectory_id = file_stem(path);
  std::string line;
  std::getline(input, line);
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    const auto fields = split_csv(line);
    if (fields.size() != 3) {
      throw std::runtime_error("invalid original trajectory row");
    }
    trajectory.points.push_back(
        CfpfFeaturePoint{std::stoll(fields[0]), std::stod(fields[1]),
                         std::stod(fields[2])});
  }
  return trajectory;
}

std::pair<double, double> read_projection(const std::string& original_dir) {
  std::ifstream input(join_path(original_dir, "preprocessing.meta"));
  if (!input) {
    throw std::runtime_error("preprocessing.meta is required for CFPF evaluation");
  }
  double lon = 0.0;
  double lat = 0.0;
  bool has_lon = false;
  bool has_lat = false;
  std::string line;
  while (std::getline(input, line)) {
    const std::size_t equal = line.find('=');
    if (equal == std::string::npos) {
      continue;
    }
    const std::string key = line.substr(0, equal);
    if (key == "ref_lon") {
      lon = std::stod(line.substr(equal + 1));
      has_lon = true;
    } else if (key == "ref_lat") {
      lat = std::stod(line.substr(equal + 1));
      has_lat = true;
    }
  }
  if (!has_lon || !has_lat) {
    throw std::runtime_error("preprocessing.meta lacks projection reference");
  }
  return {lon, lat};
}

std::unordered_map<std::int64_t, std::pair<double, double>> read_centers(
    const std::string& cfpf_dir) {
  std::ifstream input(join_path(cfpf_dir, "cluster_centers.csv"));
  if (!input) {
    throw std::runtime_error("cluster_centers.csv not found");
  }
  std::unordered_map<std::int64_t, std::pair<double, double>> centers;
  std::string line;
  std::getline(input, line);
  while (std::getline(input, line)) {
    const auto fields = split_csv(line);
    if (fields.size() != 3) {
      throw std::runtime_error("invalid cluster center row");
    }
    centers.emplace(std::stoll(fields[0]),
                    std::make_pair(std::stod(fields[1]), std::stod(fields[2])));
  }
  return centers;
}

std::unordered_map<std::string, CfpfTrajectory> read_fused_trajectories(
    const std::string& cfpf_dir,
    const std::unordered_map<std::int64_t, std::pair<double, double>>& centers) {
  std::ifstream input(join_path(cfpf_dir, "fused_points.csv"));
  if (!input) {
    throw std::runtime_error("fused_points.csv not found");
  }
  std::unordered_map<std::string, CfpfTrajectory> trajectories;
  std::string line;
  std::getline(input, line);
  while (std::getline(input, line)) {
    const auto fields = split_csv(line);
    if (fields.size() != 6) {
      throw std::runtime_error("invalid fused point row");
    }
    double lon = 0.0;
    double lat = 0.0;
    if (fields[2] == "cluster_ref") {
      const auto found = centers.find(std::stoll(fields[3]));
      if (found == centers.end()) {
        throw std::runtime_error("fused point references a missing center");
      }
      lon = found->second.first;
      lat = found->second.second;
    } else if (fields[2] == "literal") {
      lon = std::stod(fields[4]);
      lat = std::stod(fields[5]);
    } else {
      throw std::runtime_error("unknown fused point record type");
    }
    CfpfTrajectory& trajectory = trajectories[fields[0]];
    trajectory.trajectory_id = fields[0];
    trajectory.points.push_back(
        CfpfFeaturePoint{std::stoll(fields[1]), lon, lat});
  }
  return trajectories;
}

void write_metrics(const std::string& cfpf_dir,
                   const std::vector<std::pair<std::string, CfpfErrorMetrics>>& metrics) {
  std::ofstream per_trajectory(join_path(cfpf_dir, "cfpf_metrics.csv"));
  std::ofstream dataset(join_path(cfpf_dir, "cfpf_dataset_metrics.csv"));
  if (!per_trajectory || !dataset) {
    throw std::runtime_error("failed to create CFPF metric files");
  }
  per_trajectory << "trajectory_id,original_points,cfpf_feature_points,"
                    "average_ped_m,maximum_ped_m,average_sed_m,maximum_sed_m,"
                    "average_sad_m,maximum_sad_m,average_dad_m,maximum_dad_m,"
                    "average_speed_error_mps,maximum_speed_error_mps,"
                    "average_direction_error_rad,maximum_direction_error_rad\n"
                 << std::setprecision(17);
  long double ped = 0.0;
  long double sed = 0.0;
  long double sad = 0.0;
  long double dad = 0.0;
  long double speed = 0.0;
  long double direction = 0.0;
  long double trajectory_ped = 0.0;
  long double trajectory_sed = 0.0;
  long double trajectory_speed = 0.0;
  long double trajectory_direction = 0.0;
  std::uint64_t original_points = 0;
  std::uint64_t feature_points = 0;
  double max_ped = 0.0;
  double max_sed = 0.0;
  double max_sad = 0.0;
  double max_dad = 0.0;
  double max_speed = 0.0;
  double max_direction = 0.0;
  for (const auto& entry : metrics) {
    const CfpfErrorMetrics& m = entry.second;
    per_trajectory << entry.first << ',' << m.original_points << ','
                   << m.feature_points << ',' << m.average_ped_m << ','
                   << m.maximum_ped_m << ',' << m.average_sed_m << ','
                   << m.maximum_sed_m << ',' << m.average_sad_m << ','
                   << m.maximum_sad_m << ',' << m.average_dad_m << ','
                   << m.maximum_dad_m << ',' << m.average_speed_error_mps << ','
                   << m.maximum_speed_error_mps << ','
                   << m.average_direction_error_rad << ','
                   << m.maximum_direction_error_rad << '\n';
    original_points += m.original_points;
    feature_points += m.feature_points;
    ped += static_cast<long double>(m.average_ped_m) * m.original_points;
    sed += static_cast<long double>(m.average_sed_m) * m.original_points;
    sad += static_cast<long double>(m.average_sad_m) * m.original_points;
    dad += static_cast<long double>(m.average_dad_m) * m.original_points;
    speed += static_cast<long double>(m.average_speed_error_mps) * m.original_points;
    direction += static_cast<long double>(m.average_direction_error_rad) * m.original_points;
    trajectory_ped += m.average_ped_m;
    trajectory_sed += m.average_sed_m;
    trajectory_speed += m.average_speed_error_mps;
    trajectory_direction += m.average_direction_error_rad;
    max_ped = std::max(max_ped, m.maximum_ped_m);
    max_sed = std::max(max_sed, m.maximum_sed_m);
    max_sad = std::max(max_sad, m.maximum_sad_m);
    max_dad = std::max(max_dad, m.maximum_dad_m);
    max_speed = std::max(max_speed, m.maximum_speed_error_mps);
    max_direction = std::max(max_direction, m.maximum_direction_error_rad);
  }
  const long double point_denominator = original_points == 0 ? 1.0L : original_points;
  const long double trajectory_denominator = metrics.empty() ? 1.0L : metrics.size();
  dataset << "trajectories,original_points,cfpf_feature_points,compression_ratio,"
             "average_ped_m,maximum_ped_m,average_sed_m,maximum_sed_m,"
             "average_sad_m,maximum_sad_m,average_dad_m,maximum_dad_m,"
             "average_speed_error_mps,maximum_speed_error_mps,"
             "average_direction_error_rad,maximum_direction_error_rad,"
             "trajectory_mean_ped_m,trajectory_mean_sed_m,"
             "trajectory_mean_speed_error_mps,trajectory_mean_direction_error_rad\n"
          << std::setprecision(17) << metrics.size() << ',' << original_points << ','
          << feature_points << ','
          << (feature_points == 0 ? 0.0
                                  : static_cast<double>(original_points) / feature_points)
          << ',' << static_cast<double>(ped / point_denominator) << ',' << max_ped
          << ',' << static_cast<double>(sed / point_denominator) << ',' << max_sed
          << ',' << static_cast<double>(sad / point_denominator) << ',' << max_sad
          << ',' << static_cast<double>(dad / point_denominator) << ',' << max_dad
          << ',' << static_cast<double>(speed / point_denominator) << ',' << max_speed
          << ',' << static_cast<double>(direction / point_denominator) << ','
          << max_direction << ','
          << static_cast<double>(trajectory_ped / trajectory_denominator) << ','
          << static_cast<double>(trajectory_sed / trajectory_denominator) << ','
          << static_cast<double>(trajectory_speed / trajectory_denominator) << ','
          << static_cast<double>(trajectory_direction / trajectory_denominator) << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 3) {
      std::cout << "Usage: cfpf_evaluate ORIGINAL_DIR CFPF_OUTPUT_DIR...\n";
      return argc == 1 ? 0 : 1;
    }
    const std::string original_dir = argv[1];
    const auto projection = read_projection(original_dir);
    std::vector<std::string> cfpf_dirs;
    std::vector<std::unordered_map<std::string, CfpfTrajectory>> compressed_sets;
    std::vector<std::vector<std::pair<std::string, CfpfErrorMetrics>>>
        metric_sets(static_cast<std::size_t>(argc - 2));
    for (int i = 2; i < argc; ++i) {
      cfpf_dirs.emplace_back(argv[i]);
      const auto centers = read_centers(argv[i]);
      compressed_sets.push_back(read_fused_trajectories(argv[i], centers));
    }
    for (const std::string& path : trajectory_files(original_dir)) {
      const CfpfTrajectory original = read_original(path);
      for (std::size_t config = 0; config < compressed_sets.size(); ++config) {
        const auto found = compressed_sets[config].find(original.trajectory_id);
        if (found == compressed_sets[config].end()) {
          throw std::runtime_error("missing CFPF trajectory: " +
                                   original.trajectory_id);
        }
        metric_sets[config].emplace_back(
            original.trajectory_id,
            evaluate_cfpf_trajectory(original, found->second, projection.first,
                                     projection.second));
      }
    }
    for (std::size_t config = 0; config < compressed_sets.size(); ++config) {
      if (metric_sets[config].size() != compressed_sets[config].size()) {
        throw std::runtime_error("CFPF output contains unexpected trajectories");
      }
      write_metrics(cfpf_dirs[config], metric_sets[config]);
      std::cout << "Evaluated trajectories: " << metric_sets[config].size()
                << '\n'
                << "Metrics: "
                << join_path(cfpf_dirs[config], "cfpf_metrics.csv") << '\n'
                << "Dataset metrics: "
                << join_path(cfpf_dirs[config], "cfpf_dataset_metrics.csv")
                << '\n';
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "cfpf_evaluate error: " << error.what() << '\n';
    return 1;
  }
}
