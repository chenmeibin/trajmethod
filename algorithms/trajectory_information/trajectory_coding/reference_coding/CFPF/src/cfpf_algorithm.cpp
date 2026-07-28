#include "cfpf_algorithm.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kEarthRadiusMeters = 6378137.0;

double radians(double degrees) { return degrees * kPi / 180.0; }

double accumulation_coefficient(std::size_t trajectory_count) {
  if (trajectory_count < 8) {
    return 1.0;
  }
  const double denominator =
      std::log(static_cast<double>(trajectory_count)) - 1.0;
  return denominator > 0.0 ? std::min(1.0, 1.0 / denominator) : 1.0;
}

struct WorkingCluster {
  bool active = true;
  std::vector<CfpfClusterMember> members;
  // An edge represents complete-link distance compatibility and disjoint
  // trajectory membership between two active clusters.
  std::unordered_set<std::size_t> compatible_clusters;
};

struct MergeCandidate {
  std::size_t lhs = 0;
  std::size_t rhs = 0;
};

bool has_unique_trajectory_members(
    const std::vector<CfpfClusterMember>& members) {
  std::vector<std::size_t> trajectory_indices;
  trajectory_indices.reserve(members.size());
  for (const CfpfClusterMember& member : members) {
    trajectory_indices.push_back(member.trajectory_index);
  }
  std::sort(trajectory_indices.begin(), trajectory_indices.end());
  return std::adjacent_find(trajectory_indices.begin(),
                            trajectory_indices.end()) ==
         trajectory_indices.end();
}

std::pair<double, double> arithmetic_cluster_center(
    const std::vector<CfpfTrajectory>& trajectories,
    const std::vector<CfpfClusterMember>& members) {
  long double lon_sum = 0.0;
  long double lat_sum = 0.0;
  for (const CfpfClusterMember& member : members) {
    const CfpfFeaturePoint& point =
        trajectories[member.trajectory_index].points[member.point_index];
    lon_sum += point.lon;
    lat_sum += point.lat;
  }
  const long double count = static_cast<long double>(members.size());
  return {static_cast<double>(lon_sum / count),
          static_cast<double>(lat_sum / count)};
}

}  // namespace

double planar_distance_m(double lon1, double lat1, double lon2, double lat2,
                         double reference_latitude) {
  const double x = radians(lon1 - lon2) *
                   std::cos(radians(reference_latitude)) * kEarthRadiusMeters;
  const double y = radians(lat1 - lat2) * kEarthRadiusMeters;
  return std::hypot(x, y);
}

double cfpf_revised_distance_threshold_m(std::size_t trajectory_count,
                                         double distance_threshold_m) {
  return accumulation_coefficient(trajectory_count) * distance_threshold_m;
}

CfpfResult run_cfpf(
    const std::vector<CfpfTrajectory>& trajectories,
    const std::vector<SimilarTrajectoryGroup>& groups,
    const CfpfConfig& config) {
  CfpfResult result;

  for (const SimilarTrajectoryGroup& group : groups) {
    const double revised_threshold = cfpf_revised_distance_threshold_m(
        group.trajectory_indices.size(), config.fusion_distance_threshold_m);
    if (group.trajectory_indices.size() < 2) {
      continue;
    }

    long double latitude_sum = 0.0;
    std::size_t latitude_count = 0;
    for (std::size_t trajectory_index : group.trajectory_indices) {
      if (trajectory_index >= trajectories.size()) {
        throw std::runtime_error("CFPF group contains an invalid trajectory index");
      }
      for (const CfpfFeaturePoint& point : trajectories[trajectory_index].points) {
        latitude_sum += point.lat;
        ++latitude_count;
      }
    }
    const double reference_latitude =
        latitude_count == 0 ? 0.0
                            : static_cast<double>(latitude_sum / latitude_count);

    std::vector<WorkingCluster> clusters;
    for (std::size_t trajectory_index : group.trajectory_indices) {
      if (trajectory_index >= trajectories.size()) {
        throw std::runtime_error("CFPF group contains an invalid trajectory index");
      }
      for (std::size_t point_index = 0;
           point_index < trajectories[trajectory_index].points.size();
           ++point_index) {
        clusters.push_back(
            WorkingCluster{true, {{trajectory_index, point_index}}, {}});
      }
    }
    const std::size_t initial_cluster_count = clusters.size();
    if (initial_cluster_count < 2) {
      continue;
    }

    // Complete-link compatibility is preserved by intersecting the two source
    // clusters' neighbor sets after each merge.
    for (std::size_t i = 0; i < initial_cluster_count; ++i) {
      const CfpfClusterMember& left = clusters[i].members.front();
      const CfpfFeaturePoint& left_point =
          trajectories[left.trajectory_index].points[left.point_index];
      for (std::size_t j = 0; j < i; ++j) {
        const CfpfClusterMember& right = clusters[j].members.front();
        if (left.trajectory_index == right.trajectory_index) {
          continue;
        }
        const CfpfFeaturePoint& right_point =
            trajectories[right.trajectory_index].points[right.point_index];
        if (planar_distance_m(left_point.lon, left_point.lat,
                              right_point.lon, right_point.lat,
                              reference_latitude) <=
            revised_threshold) {
          clusters[i].compatible_clusters.insert(j);
          clusters[j].compatible_clusters.insert(i);
        }
      }
    }

    const std::size_t maximum_benefit = group.trajectory_indices.size() - 1;
    std::vector<std::deque<MergeCandidate>> benefit_buckets(
        maximum_benefit + 1);
    auto add_candidate = [&](std::size_t lhs, std::size_t rhs) {
      if (!clusters[lhs].active || !clusters[rhs].active ||
          clusters[lhs].compatible_clusters.count(rhs) == 0) {
        return;
      }
      const std::size_t merged_size = clusters[lhs].members.size() +
                                      clusters[rhs].members.size();
      const std::size_t benefit = merged_size - 1;
      benefit_buckets[benefit].push_back({lhs, rhs});
    };
    for (std::size_t i = 0; i < initial_cluster_count; ++i) {
      for (std::size_t j = 0; j < i; ++j) {
        add_candidate(i, j);
      }
    }

    while (true) {
      std::size_t benefit = maximum_benefit;
      MergeCandidate selected;
      bool found = false;
      while (benefit > 0 && !found) {
        auto& bucket = benefit_buckets[benefit];
        while (!bucket.empty()) {
          const MergeCandidate candidate = bucket.front();
          bucket.pop_front();
          if (clusters[candidate.lhs].active &&
              clusters[candidate.rhs].active &&
              clusters[candidate.lhs].compatible_clusters.count(
                  candidate.rhs) != 0) {
            selected = candidate;
            found = true;
            break;
          }
        }
        if (!found) {
          --benefit;
        }
      }
      if (!found) {
        break;
      }

      WorkingCluster merged;
      merged.members = clusters[selected.lhs].members;
      merged.members.insert(merged.members.end(),
                            clusters[selected.rhs].members.begin(),
                            clusters[selected.rhs].members.end());
      clusters[selected.lhs].active = false;
      clusters[selected.rhs].active = false;
      const std::size_t merged_index = clusters.size();
      clusters.push_back(std::move(merged));

      const auto& lhs_neighbors =
          clusters[selected.lhs].compatible_clusters;
      const auto& rhs_neighbors =
          clusters[selected.rhs].compatible_clusters;
      const auto* smaller = &lhs_neighbors;
      const auto* larger = &rhs_neighbors;
      if (smaller->size() > larger->size()) {
        std::swap(smaller, larger);
      }
      std::vector<std::size_t> compatible_with_merged;
      for (std::size_t other : *smaller) {
        if (clusters[other].active && larger->count(other) != 0) {
          compatible_with_merged.push_back(other);
        }
      }
      std::sort(compatible_with_merged.begin(), compatible_with_merged.end());

      for (std::size_t other : lhs_neighbors) {
        clusters[other].compatible_clusters.erase(selected.lhs);
      }
      for (std::size_t other : rhs_neighbors) {
        clusters[other].compatible_clusters.erase(selected.rhs);
      }
      clusters[selected.lhs].compatible_clusters.clear();
      clusters[selected.rhs].compatible_clusters.clear();

      for (std::size_t other : compatible_with_merged) {
        clusters[merged_index].compatible_clusters.insert(other);
        clusters[other].compatible_clusters.insert(merged_index);
        add_candidate(merged_index, other);
      }
    }

    for (const WorkingCluster& cluster : clusters) {
      if (!cluster.active || cluster.members.size() < 2) {
        continue;
      }
      if (!has_unique_trajectory_members(cluster.members)) {
        throw std::logic_error(
            "CFPF cluster contains multiple points from the same trajectory");
      }
      const auto center = arithmetic_cluster_center(trajectories, cluster.members);
      result.fused_clusters.push_back(CfpfFusedCluster{
          center.first, center.second, cluster.members});
    }
  }
  return result;
}
