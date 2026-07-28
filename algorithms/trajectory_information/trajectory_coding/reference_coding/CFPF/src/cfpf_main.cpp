#include "cfpf_algorithm.h"
#include "cfpf_preprocessing.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <direct.h>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct CliConfig {
  std::string original_dir;
  std::string seb_points_path;
  std::string output_dir;
  std::string groups_path;
  CfpfPreprocessingConfig preprocessing;
  CfpfConfig cfpf;
};

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
  if (directory.empty()) {
    return name;
  }
  const char last = directory.back();
  return last == '/' || last == '\\' ? directory + name
                                      : directory + "\\" + name;
}

std::string file_stem(const std::string& path) {
  const std::size_t slash = path.find_last_of("/\\");
  const std::size_t begin = slash == std::string::npos ? 0 : slash + 1;
  const std::size_t dot = path.find_last_of('.');
  const std::size_t end = dot == std::string::npos || dot < begin ? path.size()
                                                                  : dot;
  return path.substr(begin, end - begin);
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

std::vector<std::string> list_original_files(const std::string& directory_path) {
  DIR* directory = opendir(directory_path.c_str());
  if (directory == nullptr) {
    throw std::runtime_error("original trajectory directory not found");
  }
  std::vector<std::string> files;
  while (const dirent* entry = readdir(directory)) {
    const std::string name = entry->d_name;
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".txt") {
      files.push_back(join_path(directory_path, name));
    }
  }
  closedir(directory);
  std::sort(files.begin(), files.end());
  return files;
}

CfpfTrajectory read_original_trajectory(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open original trajectory: " + path);
  }
  CfpfTrajectory trajectory;
  trajectory.trajectory_id = file_stem(path);
  std::string line;
  std::getline(input, line);  // Object ID is not needed by CFPF grouping.
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    const std::vector<std::string> fields = split_csv(line);
    if (fields.size() != 3) {
      throw std::runtime_error("invalid preprocessed trajectory row: " + path);
    }
    trajectory.points.push_back(CfpfFeaturePoint{
        std::stoll(fields[0]), std::stod(fields[1]), std::stod(fields[2])});
  }
  return trajectory;
}

std::unordered_map<std::string, CfpfTrajectory> read_seb_points(
    const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open SEB compressed points: " + path);
  }
  std::unordered_map<std::string, CfpfTrajectory> trajectories;
  std::string line;
  std::getline(input, line);
  while (std::getline(input, line)) {
    const std::vector<std::string> fields = split_csv(line);
    if (fields.size() < 5) {
      throw std::runtime_error("invalid SEB compressed point row");
    }
    CfpfTrajectory& trajectory = trajectories[fields[0]];
    trajectory.trajectory_id = fields[0];
    trajectory.points.push_back(CfpfFeaturePoint{
        std::stoll(fields[2]), std::stod(fields[3]), std::stod(fields[4])});
  }
  return trajectories;
}

CliConfig parse_args(int argc, char** argv) {
  CliConfig config;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&]() {
      if (++i >= argc) {
        throw std::runtime_error("missing value for " + arg);
      }
      return std::string(argv[i]);
    };
    if (arg == "--original-dir") {
      config.original_dir = value();
    } else if (arg == "--seb-points") {
      config.seb_points_path = value();
    } else if (arg == "--output-dir") {
      config.output_dir = value();
    } else if (arg == "--groups-file") {
      config.groups_path = value();
    } else if (arg == "--lcss-point-threshold") {
      config.preprocessing.lcss_point_threshold_m = std::stod(value());
    } else if (arg == "--lcss-similarity") {
      config.preprocessing.lcss_similarity_threshold = std::stod(value());
    } else if (arg == "--fusion-distance") {
      config.cfpf.fusion_distance_threshold_m = std::stod(value());
    } else if (arg == "--help" || arg == "-h") {
      std::cout
          << "Usage: cfpf_cli --original-dir DIR --seb-points FILE "
             "--output-dir DIR [options]\n"
          << "  --groups-file FILE         provide precomputed similar groups\n"
          << "  --lcss-point-threshold M  default 75 meters\n"
          << "  --lcss-similarity R       default 0.6\n"
          << "  --fusion-distance M       default 50 meters\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (config.original_dir.empty() || config.seb_points_path.empty() ||
      config.output_dir.empty()) {
    throw std::runtime_error(
        "--original-dir, --seb-points, and --output-dir are required");
  }
  if (!(config.preprocessing.lcss_similarity_threshold > 0.0 &&
        config.preprocessing.lcss_similarity_threshold <= 1.0)) {
    throw std::runtime_error("LCSS similarity must be in (0,1]");
  }
  return config;
}

std::vector<SimilarTrajectoryGroup> read_groups(
    const std::string& path,
    const std::unordered_map<std::string, std::size_t>& trajectory_indices,
    std::size_t trajectory_count) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open trajectory groups: " + path);
  }
  std::vector<SimilarTrajectoryGroup> groups;
  std::vector<unsigned char> assigned(trajectory_count, 0);
  std::string line;
  std::getline(input, line);
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    const std::vector<std::string> fields = split_csv(line);
    if (fields.size() != 3) {
      throw std::runtime_error("invalid trajectory group row");
    }
    const std::size_t group_index = static_cast<std::size_t>(std::stoull(fields[0]));
    const auto found = trajectory_indices.find(fields[1]);
    if (found == trajectory_indices.end()) {
      throw std::runtime_error("group contains unknown trajectory: " + fields[1]);
    }
    if (assigned[found->second] != 0) {
      throw std::runtime_error("trajectory appears in multiple groups: " + fields[1]);
    }
    if (group_index > groups.size()) {
      throw std::runtime_error("trajectory group IDs must be contiguous");
    }
    if (group_index == groups.size()) {
      groups.push_back(SimilarTrajectoryGroup{});
    }
    groups[group_index].trajectory_indices.push_back(found->second);
    assigned[found->second] = 1;
  }
  if (std::find(assigned.begin(), assigned.end(), 0) != assigned.end()) {
    throw std::runtime_error("trajectory groups do not cover every trajectory");
  }
  return groups;
}

void write_outputs(const std::string& output_dir,
                   const std::vector<CfpfTrajectory>& trajectories,
                   const CfpfResult& result) {
  create_directory_tree(output_dir);
  std::ofstream centers(join_path(output_dir, "cluster_centers.csv"));
  std::ofstream points(join_path(output_dir, "fused_points.csv"));
  if (!centers || !points) {
    throw std::runtime_error("failed to create CFPF output files");
  }
  std::vector<std::vector<std::int64_t>> point_cluster_ids;
  point_cluster_ids.reserve(trajectories.size());
  for (const CfpfTrajectory& trajectory : trajectories) {
    point_cluster_ids.emplace_back(trajectory.points.size(), -1);
  }
  centers << "cluster_id,center_lon,center_lat\n"
          << std::setprecision(17);
  for (std::size_t cluster_id = 0;
       cluster_id < result.fused_clusters.size(); ++cluster_id) {
    const CfpfFusedCluster& cluster = result.fused_clusters[cluster_id];
    centers << cluster_id << ',' << cluster.center_lon << ','
             << cluster.center_lat << '\n';
    for (const CfpfClusterMember& member : cluster.members) {
      std::int64_t& reference =
          point_cluster_ids[member.trajectory_index][member.point_index];
      if (reference >= 0) {
        throw std::logic_error("CFPF point belongs to multiple fused clusters");
      }
      reference = static_cast<std::int64_t>(cluster_id);
    }
  }
  points << "trajectory_id,time,record_type,cluster_id,lon,lat\n"
         << std::setprecision(17);
  for (std::size_t trajectory_index = 0;
       trajectory_index < trajectories.size(); ++trajectory_index) {
    const CfpfTrajectory& trajectory = trajectories[trajectory_index];
    for (std::size_t point_index = 0; point_index < trajectory.points.size();
         ++point_index) {
      const CfpfFeaturePoint& point = trajectory.points[point_index];
      const std::int64_t cluster_id =
          point_cluster_ids[trajectory_index][point_index];
      if (cluster_id >= 0) {
        points << trajectory.trajectory_id << ',' << point.time
               << ",cluster_ref," << cluster_id << ",,\n";
      } else {
        points << trajectory.trajectory_id << ',' << point.time << ",literal,,"
               << point.lon << ',' << point.lat << '\n';
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const CliConfig config = parse_args(argc, argv);
    const auto seb_by_id = read_seb_points(config.seb_points_path);
    const std::vector<std::string> files =
        list_original_files(config.original_dir);
    std::vector<CfpfTrajectory> originals;
    std::vector<CfpfTrajectory> seb_trajectories;
    std::unordered_map<std::string, std::size_t> trajectory_indices;
    originals.reserve(files.size());
    seb_trajectories.reserve(files.size());
    for (const std::string& path : files) {
      const std::string trajectory_id = file_stem(path);
      const auto found = seb_by_id.find(trajectory_id);
      if (found == seb_by_id.end()) {
        throw std::runtime_error("missing SEB trajectory: " + trajectory_id);
      }
      trajectory_indices.emplace(trajectory_id, seb_trajectories.size());
      if (config.groups_path.empty()) {
        originals.push_back(read_original_trajectory(path));
      }
      seb_trajectories.push_back(found->second);
    }
    CfpfPreprocessingStats grouping_stats;
    const auto grouping_start = std::chrono::steady_clock::now();
    const auto groups = config.groups_path.empty()
                            ? group_similar_trajectories(
                                  originals, config.preprocessing,
                                  &grouping_stats)
                            : read_groups(config.groups_path, trajectory_indices,
                                          seb_trajectories.size());
    const auto grouping_end = std::chrono::steady_clock::now();
    const auto fusion_start = grouping_end;
    const CfpfResult result = run_cfpf(seb_trajectories, groups, config.cfpf);
    const auto fusion_end = std::chrono::steady_clock::now();
    write_outputs(config.output_dir, seb_trajectories, result);
    std::cout << "Original trajectories: " << seb_trajectories.size() << '\n'
              << "Similar trajectory groups: " << groups.size() << '\n'
              << "Spatial candidate pairs: "
              << grouping_stats.spatial_candidate_pairs << '\n'
              << "Exact LCSS comparisons: "
              << grouping_stats.exact_lcss_comparisons << '\n'
              << "LCSS upper-bound rejections: "
              << grouping_stats.lcss_upper_bound_rejections << '\n'
              << "Grouping seconds: "
              << std::chrono::duration<double>(grouping_end - grouping_start)
                     .count()
              << '\n'
              << "Fusion seconds: "
              << std::chrono::duration<double>(fusion_end - fusion_start).count()
              << '\n'
              << "Fused clusters: " << result.fused_clusters.size() << '\n'
              << "Output directory: " << config.output_dir << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "cfpf_cli error: " << error.what() << '\n';
    return 1;
  }
}
