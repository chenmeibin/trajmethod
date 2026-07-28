#include "cfpf_preprocessing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kEarthRadiusMeters = 6378137.0;

double radians(double degrees) { return degrees * kPi / 180.0; }

struct GridCell {
  std::int64_t x = 0;
  std::int64_t y = 0;

  bool operator==(const GridCell& other) const {
    return x == other.x && y == other.y;
  }
};

struct GridCellHash {
  std::size_t operator()(const GridCell& cell) const {
    const std::uint64_t x = static_cast<std::uint64_t>(cell.x);
    const std::uint64_t y = static_cast<std::uint64_t>(cell.y);
    return static_cast<std::size_t>((x * 0x9E3779B185EBCA87ULL) ^
                                    (y + 0x9E3779B97F4A7C15ULL));
  }
};

struct IndexedLcssPoint {
  GridCell cell;
  double unit_x = 0.0;
  double unit_y = 0.0;
  double unit_z = 0.0;
};

struct TrajectoryLcssIndex {
  std::vector<IndexedLcssPoint> points;
  std::unordered_map<GridCell, std::vector<std::size_t>, GridCellHash>
      points_by_cell;
};

TrajectoryLcssIndex build_lcss_index(const CfpfTrajectory& trajectory,
                                     double point_threshold_m,
                                     double longitude_scale) {
  TrajectoryLcssIndex index;
  index.points.reserve(trajectory.points.size());
  for (std::size_t i = 0; i < trajectory.points.size(); ++i) {
    const CfpfFeaturePoint& point = trajectory.points[i];
    const double lon_rad = radians(point.lon);
    const double lat_rad = radians(point.lat);
    const double cos_lat = std::cos(lat_rad);
    const GridCell cell{
        static_cast<std::int64_t>(std::floor(
            lon_rad * kEarthRadiusMeters * longitude_scale /
            point_threshold_m)),
        static_cast<std::int64_t>(std::floor(
            lat_rad * kEarthRadiusMeters / point_threshold_m))};
    index.points.push_back(IndexedLcssPoint{
        cell, cos_lat * std::cos(lon_rad), cos_lat * std::sin(lon_rad),
        std::sin(lat_rad)});
    index.points_by_cell[cell].push_back(i);
  }
  return index;
}

double indexed_lcss_similarity(const TrajectoryLcssIndex& first,
                               const TrajectoryLcssIndex& second,
                               double point_threshold_m,
                               double minimum_similarity,
                               bool* rejected_by_upper_bound) {
  if (rejected_by_upper_bound != nullptr) {
    *rejected_by_upper_bound = false;
  }
  if (first.points.empty() || second.points.empty()) {
    return 0.0;
  }

  const TrajectoryLcssIndex* left = &first;
  const TrajectoryLcssIndex* right = &second;
  if (left->points.size() > right->points.size()) {
    std::swap(left, right);
  }
  const std::size_t denominator = left->points.size();
  const std::size_t required_matches = static_cast<std::size_t>(
      std::ceil(minimum_similarity * static_cast<double>(denominator) -
                1e-12));
  const double minimum_unit_dot =
      std::cos(point_threshold_m / kEarthRadiusMeters);
  std::vector<std::vector<std::size_t>> matches_by_left(denominator);
  std::size_t left_points_with_match = 0;
  for (std::size_t i = 0; i < left->points.size(); ++i) {
    const IndexedLcssPoint& left_point = left->points[i];
    std::vector<std::size_t>& matches = matches_by_left[i];
    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        const auto found = right->points_by_cell.find(
            GridCell{left_point.cell.x + dx, left_point.cell.y + dy});
        if (found == right->points_by_cell.end()) {
          continue;
        }
        for (std::size_t j : found->second) {
          const IndexedLcssPoint& right_point = right->points[j];
          const double unit_dot = left_point.unit_x * right_point.unit_x +
                                  left_point.unit_y * right_point.unit_y +
                                  left_point.unit_z * right_point.unit_z;
          if (unit_dot >= minimum_unit_dot) {
            matches.push_back(j);
          }
        }
      }
    }
    std::sort(matches.begin(), matches.end(), std::greater<std::size_t>());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    left_points_with_match += !matches.empty();
    const std::size_t remaining = left->points.size() - i - 1;
    if (left_points_with_match + remaining < required_matches) {
      if (rejected_by_upper_bound != nullptr) {
        *rejected_by_upper_bound = true;
      }
      return static_cast<double>(left_points_with_match + remaining) /
             static_cast<double>(denominator);
    }
  }
  if (left_points_with_match < required_matches) {
    if (rejected_by_upper_bound != nullptr) {
      *rejected_by_upper_bound = true;
    }
    return static_cast<double>(left_points_with_match) /
           static_cast<double>(denominator);
  }

  // Hunt-Szymanski computes exact LCSS from the sparse point-match lists.
  std::vector<std::size_t> fenwick(right->points.size() + 1, 0);
  auto query = [&](std::size_t index) {
    std::size_t best = 0;
    while (index > 0) {
      best = std::max(best, fenwick[index]);
      index -= index & (~index + 1);
    }
    return best;
  };
  auto update = [&](std::size_t index, std::size_t value) {
    while (index < fenwick.size()) {
      fenwick[index] = std::max(fenwick[index], value);
      index += index & (~index + 1);
    }
  };
  for (const std::vector<std::size_t>& matches : matches_by_left) {
    for (std::size_t j : matches) {
      update(j + 1, query(j) + 1);
    }
  }
  return static_cast<double>(query(right->points.size())) /
         static_cast<double>(denominator);
}

struct SpatialCandidateIndex {
  std::vector<std::vector<std::size_t>> earlier_neighbors;
  std::uint64_t pair_count = 0;

  bool contains(std::size_t lhs, std::size_t rhs) const {
    if (lhs < rhs) {
      std::swap(lhs, rhs);
    }
    const std::vector<std::size_t>& neighbors = earlier_neighbors[lhs];
    return std::binary_search(neighbors.begin(), neighbors.end(), rhs);
  }
};

SpatialCandidateIndex spatial_candidate_pairs(
    const std::vector<CfpfTrajectory>& trajectories, double threshold_m) {
  if (!(threshold_m > 0.0)) {
    throw std::invalid_argument("LCSS point threshold must be positive");
  }
  double maximum_absolute_latitude = 0.0;
  for (const CfpfTrajectory& trajectory : trajectories) {
    for (const CfpfFeaturePoint& point : trajectory.points) {
      maximum_absolute_latitude =
          std::max(maximum_absolute_latitude, std::fabs(point.lat));
    }
  }
  // This conservative longitude scale cannot remove a true Haversine match.
  const double longitude_scale = std::max(
      1e-6, std::cos(radians(std::min(89.0, maximum_absolute_latitude))));
  std::unordered_map<GridCell, std::vector<std::size_t>, GridCellHash> cells;
  std::vector<std::vector<GridCell>> occupied_by_trajectory(
      trajectories.size());
  for (std::size_t trajectory_index = 0;
       trajectory_index < trajectories.size(); ++trajectory_index) {
    std::unordered_set<GridCell, GridCellHash> occupied;
    for (const CfpfFeaturePoint& point : trajectories[trajectory_index].points) {
      const double x = radians(point.lon) * kEarthRadiusMeters * longitude_scale;
      const double y = radians(point.lat) * kEarthRadiusMeters;
      occupied.insert(GridCell{
          static_cast<std::int64_t>(std::floor(x / threshold_m)),
          static_cast<std::int64_t>(std::floor(y / threshold_m))});
    }
    occupied_by_trajectory[trajectory_index].assign(occupied.begin(),
                                                    occupied.end());
    for (const GridCell& cell : occupied) {
      cells[cell].push_back(trajectory_index);
    }
  }

  SpatialCandidateIndex candidates;
  candidates.earlier_neighbors.resize(trajectories.size());
  for (std::size_t trajectory_index = 0;
       trajectory_index < trajectories.size(); ++trajectory_index) {
    std::unordered_set<std::size_t> earlier;
    for (const GridCell& cell : occupied_by_trajectory[trajectory_index]) {
      for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
          const auto found = cells.find(GridCell{cell.x + dx, cell.y + dy});
          if (found == cells.end()) {
            continue;
          }
          for (std::size_t other : found->second) {
            if (other < trajectory_index) {
              earlier.insert(other);
            }
          }
        }
      }
    }
    std::vector<std::size_t>& neighbors =
        candidates.earlier_neighbors[trajectory_index];
    neighbors.assign(earlier.begin(), earlier.end());
    std::sort(neighbors.begin(), neighbors.end());
    candidates.pair_count += neighbors.size();
  }
  return candidates;
}

}  // namespace

double lcss_similarity(const CfpfTrajectory& lhs, const CfpfTrajectory& rhs,
                       double point_threshold_m) {
  if (lhs.points.empty() || rhs.points.empty()) {
    return 0.0;
  }
  double maximum_absolute_latitude = 0.0;
  for (const CfpfFeaturePoint& point : lhs.points) {
    maximum_absolute_latitude =
        std::max(maximum_absolute_latitude, std::fabs(point.lat));
  }
  for (const CfpfFeaturePoint& point : rhs.points) {
    maximum_absolute_latitude =
        std::max(maximum_absolute_latitude, std::fabs(point.lat));
  }
  const double longitude_scale = std::max(
      1e-6, std::cos(radians(std::min(89.0, maximum_absolute_latitude))));
  const TrajectoryLcssIndex lhs_index =
      build_lcss_index(lhs, point_threshold_m, longitude_scale);
  const TrajectoryLcssIndex rhs_index =
      build_lcss_index(rhs, point_threshold_m, longitude_scale);
  return indexed_lcss_similarity(lhs_index, rhs_index, point_threshold_m, 0.0,
                                 nullptr);
}

std::vector<SimilarTrajectoryGroup> group_similar_trajectories(
    const std::vector<CfpfTrajectory>& trajectories,
    const CfpfPreprocessingConfig& config,
    CfpfPreprocessingStats* stats) {
  const SpatialCandidateIndex spatial_candidates = spatial_candidate_pairs(
      trajectories, config.lcss_point_threshold_m);
  if (stats != nullptr) {
    stats->spatial_candidate_pairs = spatial_candidates.pair_count;
    stats->exact_lcss_comparisons = 0;
    stats->lcss_upper_bound_rejections = 0;
  }

  double maximum_absolute_latitude = 0.0;
  for (const CfpfTrajectory& trajectory : trajectories) {
    for (const CfpfFeaturePoint& point : trajectory.points) {
      maximum_absolute_latitude =
          std::max(maximum_absolute_latitude, std::fabs(point.lat));
    }
  }
  const double longitude_scale = std::max(
      1e-6, std::cos(radians(std::min(89.0, maximum_absolute_latitude))));
  std::vector<TrajectoryLcssIndex> lcss_indices;
  lcss_indices.reserve(trajectories.size());
  for (const CfpfTrajectory& trajectory : trajectories) {
    lcss_indices.push_back(build_lcss_index(
        trajectory, config.lcss_point_threshold_m, longitude_scale));
  }

  auto similarity = [&](std::size_t lhs, std::size_t rhs) {
    bool rejected_by_upper_bound = false;
    const double value = indexed_lcss_similarity(
        lcss_indices[lhs], lcss_indices[rhs],
        config.lcss_point_threshold_m, config.lcss_similarity_threshold,
        &rejected_by_upper_bound);
    if (stats != nullptr) {
      ++stats->exact_lcss_comparisons;
      stats->lcss_upper_bound_rejections += rejected_by_upper_bound;
    }
    return value;
  };

  std::vector<SimilarTrajectoryGroup> groups;
  for (std::size_t trajectory_index = 0;
       trajectory_index < trajectories.size(); ++trajectory_index) {
    std::size_t best_group = groups.size();
    std::size_t best_size = 0;
    double best_average_similarity = -1.0;
    for (std::size_t group_index = 0; group_index < groups.size();
         ++group_index) {
      bool compatible = true;
      double similarity_sum = 0.0;
      for (std::size_t member : groups[group_index].trajectory_indices) {
        if (!spatial_candidates.contains(trajectory_index, member)) {
          compatible = false;
          break;
        }
        const double value = similarity(trajectory_index, member);
        if (value < config.lcss_similarity_threshold) {
          compatible = false;
          break;
        }
        similarity_sum += value;
      }
      if (!compatible) {
        continue;
      }
      const std::size_t group_size =
          groups[group_index].trajectory_indices.size();
      const double average_similarity = similarity_sum / group_size;
      if (group_size > best_size ||
          (group_size == best_size &&
           average_similarity > best_average_similarity)) {
        best_group = group_index;
        best_size = group_size;
        best_average_similarity = average_similarity;
      }
    }
    if (best_group == groups.size()) {
      groups.push_back(SimilarTrajectoryGroup{{trajectory_index}});
    } else {
      groups[best_group].trajectory_indices.push_back(trajectory_index);
    }
  }
  return groups;
}
